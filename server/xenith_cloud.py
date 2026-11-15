#!/usr/bin/env python3
"""XENITH Cloud: the server side of XENITH for the Xbox 360.

Standard library only. Plain HTTP, because the console has no TLS or DNS.
Every reply is text/plain with simple line formats the console parses in C.

  GET  /                              the XENITH website (web/index.html)
  GET  /v1/ping                       pong
  GET  /v1/status                     key=value server stats (does not log a console)
  GET  /v1/hello?name=OWEN&ver=0.3    key=value lines: motd + live server stats
  GET  /v1/inbox?key=<console key>    id|unix_time|text   (newest first)
  GET  /v1/store                      id|kind|name|version|size|sha256|file|description
  GET  /v1/store/file/<id>            the file bytes
  GET  /v1/scores/<game>              rank|name|score
  POST /v1/scores/<game>              form: name=..&score=..  -> rank|name|score

Admin (header X-Xenith-Key: <XENITH_ADMIN_KEY>):
  POST /v1/inbox                      body is the message text
  POST /v1/inbox/clear
  POST /v1/motd                       body is the new message of the day

Command line (on the VPS, no key needed):
  xenith_cloud.py send "text"         put a message in the console inbox
  xenith_cloud.py motd "text"         set the message of the day
  xenith_cloud.py --selftest          run the built-in tests (no network, temp dir)

Environment:
  XENITH_PORT        default 8795
  XENITH_STATE_DIR   default ./state      (state.json lives here)
  XENITH_STORE_DIR   default ./store      (store.json + the files it lists)
  XENITH_WEB_DIR     default ./web        (index.html for the website)
  XENITH_ADMIN_KEY   admin key; admin HTTP routes are disabled when unset
  XENITH_CONSOLE_KEY when set, reading the inbox needs ?key=<this> (the console's config.txt key=)
"""
import hashlib
import hmac
import json
import os
import re
import shutil
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

VERSION = "0.3"
GAMES = {"volt"}
MAX_SCORE = 1_000_000
TOP_KEEP = 50
INBOX_KEEP = 20
MAX_BODY = 4096
SCORE_COOLDOWN_S = 5
NAME_RE = re.compile(r"^[A-Z0-9]{1,12}$")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,31}$")
KINDS = {"app", "data", "system"}

# ---------------------------------------------------------------------- state


def clean(text, limit=200):
    """One line the console font can draw: no pipes, no newlines, printable ASCII."""
    text = "".join(ch if 32 <= ord(ch) < 127 and ch != "|" else " " for ch in str(text))
    return " ".join(text.split())[:limit]


class State:
    def __init__(self, state_dir):
        self.path = os.path.join(state_dir, "state.json")
        os.makedirs(state_dir, exist_ok=True)
        self.lock = threading.Lock()
        self.data = {"motd": "WELCOME TO XENITH CLOUD", "inbox": [], "scores": {}, "next_id": 1,
                     "consoles": {}}
        if os.path.exists(self.path):
            with open(self.path, encoding="utf-8") as f:
                self.data.update(json.load(f))

    def save(self):
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.data, f)
        os.replace(tmp, self.path)

    def set_motd(self, text):
        with self.lock:
            self.data["motd"] = clean(text, 120)
            self.save()

    def add_message(self, text):
        text = clean(text, 200)
        if not text:
            return None
        with self.lock:
            msg = {"id": self.data["next_id"], "t": int(time.time()), "text": text}
            self.data["next_id"] += 1
            self.data["inbox"].insert(0, msg)
            del self.data["inbox"][INBOX_KEEP:]
            self.save()
            return msg

    def clear_inbox(self):
        with self.lock:
            self.data["inbox"] = []
            self.save()

    def seen(self, name, ip):
        with self.lock:
            self.data["consoles"][name] = {"t": int(time.time()), "ip": ip}
            self.save()

    def top(self, game):
        with self.lock:
            return list(self.data["scores"].get(game, []))

    def submit(self, game, name, score):
        """Keep each name's best score. Returns (rank or None, entry)."""
        with self.lock:
            board = self.data["scores"].setdefault(game, [])
            existing = next((e for e in board if e["name"] == name), None)
            if existing:
                if score <= existing["score"]:
                    rank = board.index(existing) + 1
                    return rank, existing
                board.remove(existing)
            entry = {"name": name, "score": score, "t": int(time.time())}
            board.append(entry)
            board.sort(key=lambda e: (-e["score"], e["t"]))
            del board[TOP_KEEP:]
            self.save()
            rank = board.index(entry) + 1 if entry in board else None
            return rank, entry


class Store:
    """store.json: [{"id","kind","name","version","file","description"}], files next to it."""

    def __init__(self, store_dir):
        self.dir = store_dir
        self.cache = {}   # path -> (mtime, size, sha256)
        self.lock = threading.Lock()

    def _digest(self, path):
        st = os.stat(path)
        with self.lock:
            hit = self.cache.get(path)
            if hit and hit[0] == st.st_mtime:
                return hit[1], hit[2]
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        with self.lock:
            self.cache[path] = (st.st_mtime, st.st_size, h.hexdigest())
        return st.st_size, h.hexdigest()

    def items(self):
        manifest = os.path.join(self.dir, "store.json")
        if not os.path.exists(manifest):
            return []
        with open(manifest, encoding="utf-8") as f:
            raw = json.load(f)
        out = []
        for item in raw:
            iid, kind = str(item.get("id", "")), str(item.get("kind", ""))
            fname = os.path.basename(str(item.get("file", "")))
            path = os.path.join(self.dir, fname)
            if not ID_RE.match(iid) or kind not in KINDS or not fname or not os.path.isfile(path):
                continue
            size, sha = self._digest(path)
            out.append({"id": iid, "kind": kind, "name": clean(item.get("name", iid), 24),
                        "version": clean(item.get("version", "1"), 12), "size": size,
                        "sha256": sha, "description": clean(item.get("description", ""), 90),
                        "file": fname, "path": path})
        return out

    def find(self, iid):
        return next((i for i in self.items() if i["id"] == iid), None)


# ---------------------------------------------------------------------- stats


def server_stats():
    stats = {}
    try:
        with open("/proc/uptime") as f:
            up = int(float(f.read().split()[0]))
        stats["uptime"] = f"{up // 86400}D {up % 86400 // 3600}H {up % 3600 // 60}M"
    except OSError:
        stats["uptime"] = "?"
    try:
        with open("/proc/loadavg") as f:
            stats["load"] = " ".join(f.read().split()[:3])
    except OSError:
        stats["load"] = "?"
    try:
        mem = {}
        with open("/proc/meminfo") as f:
            for line in f:
                k, v = line.split(":", 1)
                mem[k] = int(v.split()[0])
        total, avail = mem["MemTotal"] // 1024, mem.get("MemAvailable", mem["MemFree"]) // 1024
        stats["mem_used_mb"] = str(total - avail)
        stats["mem_total_mb"] = str(total)
    except (OSError, KeyError, ValueError):
        stats["mem_used_mb"] = stats["mem_total_mb"] = "0"
    du = shutil.disk_usage("/")
    stats["disk_used_gb"] = str(du.used // 2**30)
    stats["disk_total_gb"] = str(du.total // 2**30)
    stats["cpus"] = str(os.cpu_count() or 1)
    stats["time"] = time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime())
    return stats


# ---------------------------------------------------------------------- http


def make_handler(state, store, admin_key, web_dir="web", console_key=""):
    last_score = {}
    last_score_lock = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        server_version = "XENITH-Cloud/" + VERSION
        protocol_version = "HTTP/1.0"

        def log_message(self, fmt, *args):
            sys.stderr.write("%s %s\n" % (self.client_address[0], fmt % args))

        def reply(self, code, text="", ctype="text/plain; charset=ascii"):
            body = text.encode("ascii", "replace") if isinstance(text, str) else text
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)

        def body(self):
            n = int(self.headers.get("Content-Length") or 0)
            if n < 0 or n > MAX_BODY:
                return None
            return self.rfile.read(n).decode("utf-8", "replace")

        def is_admin(self):
            key = self.headers.get("X-Xenith-Key", "")
            return bool(admin_key) and hmac.compare_digest(key.encode(), admin_key.encode())

        # -------------------------------------------------------------- GET
        def do_GET(self):
            url = urlparse(self.path)
            parts = [p for p in url.path.split("/") if p]
            q = parse_qs(url.query)

            if parts == ["v1", "ping"]:
                return self.reply(200, "pong\n")

            if parts == ["v1", "status"]:
                lines = {"motd": state.data["motd"], "server": "XENITH CLOUD " + VERSION,
                         "consoles": str(len(state.data["consoles"])),
                         "apps": str(len(store.items()))}
                lines.update(server_stats())
                return self.reply(200, "".join(f"{k}={clean(v)}\n" for k, v in lines.items()))

            if parts == ["v1", "hello"]:
                name = clean(q.get("name", ["PLAYER"])[0], 12).upper()
                if NAME_RE.match(name):
                    state.seen(name, self.client_address[0])
                lines = {"motd": state.data["motd"], "server": "XENITH CLOUD " + VERSION,
                         "messages": str(len(state.data["inbox"]))}
                lines.update(server_stats())
                return self.reply(200, "".join(f"{k}={clean(v)}\n" for k, v in lines.items()))

            if parts == ["v1", "inbox"]:
                if console_key and not hmac.compare_digest(
                        q.get("key", [""])[0].encode(), console_key.encode()):
                    return self.reply(401, "unauthorized\n")
                return self.reply(200, "".join(f"{m['id']}|{m['t']}|{m['text']}\n"
                                               for m in state.data["inbox"]))

            if parts == ["v1", "store"]:
                return self.reply(200, "".join(
                    f"{i['id']}|{i['kind']}|{i['name']}|{i['version']}|{i['size']}|"
                    f"{i['sha256']}|{i['file']}|{i['description']}\n" for i in store.items()))

            if len(parts) == 4 and parts[:3] == ["v1", "store", "file"]:
                item = store.find(parts[3])
                if not item:
                    return self.reply(404, "no such item\n")
                with open(item["path"], "rb") as f:
                    data = f.read()
                return self.reply(200, data, "application/octet-stream")

            if len(parts) == 3 and parts[:2] == ["v1", "scores"] and parts[2] in GAMES:
                board = state.top(parts[2])[:10]
                return self.reply(200, "".join(f"{n}|{e['name']}|{e['score']}\n"
                                               for n, e in enumerate(board, 1)))

            if parts == [] or parts == ["index.html"]:
                page = os.path.join(web_dir, "index.html")
                if os.path.isfile(page):
                    with open(page, "rb") as f:
                        return self.reply(200, f.read(), "text/html; charset=utf-8")
                return self.reply(200, "XENITH Cloud " + VERSION + "\n")

            return self.reply(404, "not found\n")

        # -------------------------------------------------------------- POST
        def do_POST(self):
            parts = [p for p in urlparse(self.path).path.split("/") if p]
            body = self.body()
            if body is None:
                return self.reply(413, "too large\n")

            if len(parts) == 3 and parts[:2] == ["v1", "scores"] and parts[2] in GAMES:
                form = parse_qs(body)
                name = form.get("name", [""])[0].strip().upper()
                try:
                    score = int(form.get("score", ["-1"])[0])
                except ValueError:
                    score = -1
                if not NAME_RE.match(name) or not 0 <= score <= MAX_SCORE:
                    return self.reply(400, "bad score\n")
                ip = self.client_address[0]
                with last_score_lock:
                    now = time.monotonic()
                    if now - last_score.get(ip, -1e9) < SCORE_COOLDOWN_S:
                        return self.reply(429, "slow down\n")
                    last_score[ip] = now
                rank, entry = state.submit(parts[2], name, score)
                return self.reply(200, f"{rank or 0}|{entry['name']}|{entry['score']}\n")

            if parts[:2] in (["v1", "inbox"], ["v1", "motd"]):
                if not self.is_admin():
                    return self.reply(401, "unauthorized\n")
                if parts == ["v1", "inbox"]:
                    msg = state.add_message(body)
                    return self.reply(200 if msg else 400, f"{msg['id']}\n" if msg else "empty\n")
                if parts == ["v1", "inbox", "clear"]:
                    state.clear_inbox()
                    return self.reply(200, "cleared\n")
                if parts == ["v1", "motd"]:
                    state.set_motd(body)
                    return self.reply(200, "ok\n")

            return self.reply(404, "not found\n")

    return Handler


def serve():
    port = int(os.environ.get("XENITH_PORT", "8795"))
    state = State(os.environ.get("XENITH_STATE_DIR", "state"))
    store = Store(os.environ.get("XENITH_STORE_DIR", "store"))
    admin_key = os.environ.get("XENITH_ADMIN_KEY", "")
    console_key = os.environ.get("XENITH_CONSOLE_KEY", "")
    web_dir = os.environ.get("XENITH_WEB_DIR", "web")
    httpd = ThreadingHTTPServer(("0.0.0.0", port),
                                make_handler(state, store, admin_key, web_dir, console_key))
    httpd.daemon_threads = True
    print(f"XENITH Cloud {VERSION} on :{port} (admin routes {'on' if admin_key else 'off'})",
          flush=True)
    httpd.serve_forever()


# ---------------------------------------------------------------------- selftest


def selftest():
    import tempfile
    import urllib.request
    import urllib.error

    tmp = tempfile.mkdtemp()
    store_dir = os.path.join(tmp, "store")
    os.makedirs(store_dir)
    with open(os.path.join(store_dir, "game.elf32"), "wb") as f:
        f.write(b"\x7fELF" + bytes(range(256)) * 10)
    with open(os.path.join(store_dir, "store.json"), "w") as f:
        json.dump([{"id": "game", "kind": "app", "name": "Game|X", "version": "1.0",
                    "file": "game.elf32", "description": "line\nbreak"},
                   {"id": "../evil", "kind": "app", "file": "game.elf32"},
                   {"id": "missing", "kind": "app", "file": "nope.bin"},
                   {"id": "weird", "kind": "exe", "file": "game.elf32"}], f)

    state = State(os.path.join(tmp, "state"))
    store = Store(store_dir)
    web_dir = os.path.join(tmp, "web")
    os.makedirs(web_dir)
    with open(os.path.join(web_dir, "index.html"), "w") as f:
        f.write("<!doctype html><title>XENITH</title>")
    httpd = ThreadingHTTPServer(("127.0.0.1", 0),
                                make_handler(state, store, "k3y", web_dir, "c0nsole"))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{httpd.server_address[1]}"

    def req(method, path, body=None, key=None):
        r = urllib.request.Request(base + path, data=body.encode() if body is not None else None,
                                   method=method)
        if key:
            r.add_header("X-Xenith-Key", key)
        try:
            with urllib.request.urlopen(r) as resp:
                return resp.status, resp.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    checks = []

    def check(name, cond):
        checks.append((name, bool(cond)))

    check("ping", req("GET", "/v1/ping") == (200, b"pong\n"))
    check("website served", req("GET", "/") == (200, b"<!doctype html><title>XENITH</title>"))
    code, body = req("GET", "/v1/status")
    check("status ok", code == 200 and b"uptime=" in body and b"apps=" in body)
    check("status does not log a console", state.data["consoles"] == {})
    code, body = req("GET", "/v1/hello?name=owen&ver=0.3")
    text = body.decode()
    check("hello ok", code == 200 and "motd=WELCOME TO XENITH CLOUD" in text)
    check("hello stats", "uptime=" in text and "disk_total_gb=" in text and "load=" in text)
    check("hello records console", "OWEN" in state.data["consoles"])

    code, body = req("GET", "/v1/store")
    lines = body.decode().strip().split("\n")
    check("store lists only valid items", code == 200 and len(lines) == 1)
    f = lines[0].split("|")
    with open(os.path.join(store_dir, "game.elf32"), "rb") as fh:
        payload = fh.read()
    check("store line has 8 fields", len(f) == 8)
    check("store file name", f[6] == "game.elf32")
    check("store pipe cleaned from name", f[2] == "Game X")
    check("store sha256 matches", f[5] == hashlib.sha256(payload).hexdigest())
    check("store size matches", f[4] == str(len(payload)))
    check("store description one line", "\n" not in f[7] and f[7] == "line break")
    check("download bytes", req("GET", "/v1/store/file/game") == (200, payload))
    check("download unknown 404", req("GET", "/v1/store/file/nope")[0] == 404)
    check("download traversal 404", req("GET", "/v1/store/file/..%2Fevil")[0] == 404)

    check("score accepted", req("POST", "/v1/scores/volt", "name=owen&score=120")[1] == b"1|OWEN|120\n")
    check("score cooldown", req("POST", "/v1/scores/volt", "name=owen&score=130")[0] == 429)
    state.submit("volt", "ZED", 300)
    state.submit("volt", "OWEN", 50)     # lower than best: kept at 120
    code, body = req("GET", "/v1/scores/volt")
    check("scores ordered, best kept", body == b"1|ZED|300\n2|OWEN|120\n")
    last = None
    for bad in ("name=&score=5", "name=a b&score=5", "name=OK&score=-1",
                "name=OK&score=99999999", "name=TOOLONGNAME123&score=5"):
        last = req("POST", "/v1/scores/volt", bad)[0]
        check(f"score rejected: {bad}", last == 400)
    check("unknown game 404", req("GET", "/v1/scores/nope")[0] == 404)

    check("inbox needs key", req("POST", "/v1/inbox", "hi")[0] == 401)
    check("inbox wrong key", req("POST", "/v1/inbox", "hi", key="nope")[0] == 401)
    check("inbox add", req("POST", "/v1/inbox", "Hello|Xbox\nfrom phone", key="k3y")[0] == 200)
    check("inbox read without console key refused", req("GET", "/v1/inbox")[0] == 401)
    check("inbox read with wrong console key refused", req("GET", "/v1/inbox?key=nope")[0] == 401)
    code, body = req("GET", "/v1/inbox?key=c0nsole")
    check("inbox read cleaned", code == 200 and body.decode().split("|", 2)[2] == "Hello Xbox from phone\n")
    check("motd set", req("POST", "/v1/motd", "Game night 9PM", key="k3y")[0] == 200)
    check("motd shown", "motd=Game night 9PM" in req("GET", "/v1/hello")[1].decode())
    check("inbox clear", req("POST", "/v1/inbox/clear", "", key="k3y")[0] == 200
          and req("GET", "/v1/inbox?key=c0nsole")[1] == b"")
    check("body too large", req("POST", "/v1/motd", "x" * (MAX_BODY + 1), key="k3y")[0] == 413)

    reloaded = State(os.path.join(tmp, "state"))
    check("state persists", reloaded.data["motd"] == "Game night 9PM"
          and reloaded.data["scores"]["volt"][0]["name"] == "ZED")

    httpd.shutdown()
    shutil.rmtree(tmp)
    for name, ok in checks:
        print(("PASS " if ok else "FAIL ") + name)
    failed = sum(not ok for _, ok in checks)
    print(f"{len(checks) - failed}/{len(checks)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    args = sys.argv[1:]
    if args == ["--selftest"]:
        sys.exit(selftest())
    if len(args) == 2 and args[0] in ("send", "motd"):
        # Talk to the running server so its in-memory state stays the source of truth.
        import urllib.request
        import urllib.error
        key = os.environ.get("XENITH_ADMIN_KEY", "")
        if not key:
            sys.exit("XENITH_ADMIN_KEY is not set (on the VPS use the `xenith` wrapper)")
        port = os.environ.get("XENITH_PORT", "8795")
        path = "/v1/inbox" if args[0] == "send" else "/v1/motd"
        r = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=args[1].encode(),
                                   method="POST", headers={"X-Xenith-Key": key})
        try:
            with urllib.request.urlopen(r, timeout=5) as resp:
                print("sent" if args[0] == "send" else "motd set", resp.read().decode().strip())
        except (urllib.error.URLError, OSError) as e:
            sys.exit(f"server not reachable: {e}")
        sys.exit(0)
    if args:
        print(__doc__)
        sys.exit(2)
    serve()
