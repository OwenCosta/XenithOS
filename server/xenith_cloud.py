#!/usr/bin/env python3
"""XenithOS Cloud: the free online service behind XenithOS (Xbox 360, PCs, Raspberry Pis).

Standard library only (plus yt-dlp and ffmpeg for Tube, if installed). Plain HTTP, because
the Xbox has no TLS or DNS. Replies are text/plain: key=value lines, or rows split by '|'.

Public
  GET  /                              the XenithOS website (web/index.html)
  GET  /static/<file>                 website files (web/static/)
  GET  /v1/ping                       pong
  GET  /v1/lan                        key=value: id (this server's), and lan, its address on its
                                      own network when XENITH_LAN_ADDR is set. Consoles there use
                                      that instead, once it answers with the same id. And https,
                                      its HTTPS address when XENITH_PUBLIC_HTTPS is set, which PCs
                                      and Raspberry Pis use the same way (the Xbox has no TLS).
  GET  /v1/status                     key=value server stats (does not log a console)
  GET  /v1/hello?name=Owen&ver=0.5    key=value: motd, players, online now (logs the console)
  GET  /v1/news                       id|unix_time|text   (newest first), XenithOS news
  GET  /v1/store                      id|kind|name|version|size|sha256|file|description
  GET  /v1/store/needs                id|data_id,data_id   game data an item comes with
  GET  /v1/store/flathub              id|name|flathub id|processors|download MB|shared MB|arguments|description
                                      apps from Flathub, for PCs and Raspberry Pis (flathub.json)
  GET  /v1/store/steam                id|name|steam app id|1 if free to play|description
                                      games on Steam, for PCs (steam.json)
  GET  /v1/store/all[?pc=1]           all of the above lists and the .ximg thumbnails in one
                                      reply (store_bundle), for 0.5.7 consoles
  GET  /v1/store/file/<id>            the file bytes
  GET  /v1/store/icon/<id>.ximg|.png  the item's thumbnail
                                      With XENITH_STORE_KEY, all of these but the files come with
                                      an X-Xenith-Sig header, XenithOS Cloud's signature, which
                                      consoles check (see "Store signatures" below)
  GET  /v1/scores/<game>              rank|name|score|avatar
  GET  /v1/players                    name|avatar|joined|volt_best   (most recently seen first)
  GET  /v1/players/<name>             key=value
  GET  /v1/inbox?key=<console key>    the news, for 0.4 consoles
  GET  /download/xenithos-xbox360.zip XenithOS, its apps and a config, ready for a USB stick
  GET  /download/config.txt           a config with this server's address
  GET  /download/xenithos-source.tar.gz, /install.sh   for PCs and Raspberry Pis

Tube (YouTube; needs a signed-in profile)
  POST /v1/tube/search                token, q          -> id|title|channel|seconds
  GET  /v1/tube/thumb/<id>.jpg
  POST /v1/tube/start                 token, id, t      -> session, title, duration, fps, rate
                                                        (answers at once; YouTube is asked
                                                        in the background)
  GET  /v1/tube/chunk?s=<session>&off=<bytes>           the next piece of the AVI (204: not
                                                        ready yet, 410: finished, 503 with
                                                        error=: it couldn't be played)
  GET  /v1/tube/stop?s=<session>

Accounts (form posts; errors come back as "error=<message>")
  POST /v1/accounts/create            name, pin, avatar   -> name, token, avatar, joined
  POST /v1/accounts/login             name, pin           -> name, token, avatar, joined
  POST /v1/accounts/me                token               -> name, avatar, joined, volt_best, volt_rank
  POST /v1/accounts/avatar            token, avatar
  POST /v1/accounts/logout            token
  POST /v1/scores/<game>              token, score  (or legacy: name, score, for names without a profile)
  POST /v1/feedback                   kind (bug or idea), text, version, platform, and token if signed
                                      in: kept, and passed on to Discord (feedback-webhooks.txt in the
                                      state folder: "bug=<webhook>", "idea=<webhook>", or one for both)

Admin (header X-Xenith-Key: <XENITH_ADMIN_KEY>)
  POST /v1/news (or /v1/inbox)        body is the news post
  POST /v1/inbox/clear
  POST /v1/motd                       body is the new message of the day
  GET  /v1/feedback                   time|kind|from|version|platform|text, newest first

Command line
  xenith_cloud.py store-key new <file>   make the Store's signing key; prints its public half
  xenith_cloud.py store-key show <file>  the public half of a key file
  xenith_cloud.py store-key check <host:port or URL> [public key]   check a running server's
                                      Store signatures (default: the keys in source/store_key.h)
  xenith_cloud.py send "text"         message the console inbox (through the running server)
  xenith_cloud.py motd "text"         set the message of the day
  xenith_cloud.py --selftest          built-in tests (no network, temp dir)

Environment
  XENITH_PORT        default 8795
  XENITH_PROXY_PORT  a second port for a relay (server/xenith-relay.py) that starts every
                     connection with a PROXY protocol v1 line naming the console's own address,
                     so XenithOS Cloud can run behind a home connection while consoles keep a
                     public address. Off when unset; only the relay should reach this port.
  XENITH_EDGE_PORT   a port for Cloudflare's tunnel (cloudflared) to bring HTTPS requests to.
                     Cloudflare names each console's own address in CF-Connecting-IP, which is
                     used the way the relay's PROXY line is; a request without one gets no
                     answer. Off when unset; only cloudflared should reach this port.
  XENITH_STATE_DIR   default ./state      (state.json lives here)
  XENITH_STORE_DIR   default ./store      (store.json + the files it lists, flathub.json, steam.json,
                                          and <id>.ximg / <id>.png thumbnails)
  XENITH_STORE_KEY   the file with the Store's signing key (store-key new). It must be outside the
                     store, web, download and state folders. Unset: Store replies go out unsigned
  XENITH_WEB_DIR     default ./web        (index.html and static/)
  XENITH_ADMIN_KEY   admin key; admin routes are off when unset
  XENITH_CONSOLE_KEY when set, the old /v1/inbox route needs ?key=<this>
  XENITH_PUBLIC_ADDR the "ip:port" consoles should use (goes into downloads); default: the
                     address each download was requested from
  XENITH_LAN_ADDR    the private "a.b.c.d:port" consoles on this server's own network can reach
                     it at directly, skipping the relay (/v1/lan). Off when unset.
  XENITH_PUBLIC_HTTPS  the name (with :port when it isn't 443) PCs and Raspberry Pis can reach
                     this server at over HTTPS, such as a Cloudflare tunnel's hostname whose
                     service is XENITH_EDGE_PORT (/v1/lan). Off when unset.
  XENITH_DOWNLOAD_DIR default ./downloads   (install.sh, xenithos-source.tar.gz)
  XENITH_YTDLP, XENITH_FFMPEG  paths for Tube (default: found on PATH); Tube is off without them
  XENITH_TUBE_STREAMS default 3: videos playing at once, across everyone
  XENITH_DENO        Deno, so yt-dlp can solve YouTube's JavaScript challenges when the quick
                     way is refused (setup-tube.sh installs it)
  XENITH_YTDLP_PLUGINS  a yt-dlp plugin folder: the PO token provider, whose token server is
                     xenith-pot.service
"""
import hashlib
import hmac
import io
import ipaddress
import json
import os
import re
import secrets
import select
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlparse

VERSION = "0.5.0"
GAMES = {"volt"}
MAX_SCORE = 1_000_000
TOP_KEEP = 50
INBOX_KEEP = 20
MAX_BODY = 4096
SCORE_COOLDOWN_S = 5
PIN_ITERATIONS = 120_000
SESSIONS_KEEP = 5
LOCK_AFTER = 5                 # wrong passcodes in a row
LOCK_SECONDS = 15 * 60
CREATE_PER_HOUR = 5            # per IP
LOGIN_PER_10MIN = 20           # per IP

LEGACY_NAME_RE = re.compile(r"^[A-Z0-9]{1,12}$")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,31}$")
AVATAR_RE = re.compile(r"^(1[0-9a-z]{9}|2[0-9a-z]{13})$")
AVATAR_COUNTS_V1 = [8, 8, 8, 5, 5, 4, 10, 6, 8]                   # "1" + 9 digits, the first format
AVATAR_COUNTS = [10, 14, 13, 8, 10, 8, 12, 11, 12, 6, 5, 5, 3]     # "2" + 13 digits (shared/avatar.c)
ONLINE_WINDOW = 15 * 60        # "online now": seen in the last 15 minutes
PUBLIC_ADDR_RE = re.compile(r"^[A-Za-z0-9.-]{1,64}:[0-9]{1,5}$")
LAN_ADDR_RE = re.compile(r"^([0-9]{1,3}(?:\.[0-9]{1,3}){3}):([0-9]{1,5})$")
HTTPS_ADDR_RE = re.compile(r"^([A-Za-z0-9.-]{1,64})(?::([0-9]{1,5}))?$")
PIN_RE = re.compile(r"^[0-9]{4,8}$")
STATIC_RE = re.compile(r"^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}\.(png|js|css|svg|ico|webp|wav)$")
KINDS = {"app", "data", "system", "pc"}      # pc: XenithOS's source, which PCs and Pis build themselves
# Flathub's app ids ("tv.kodi.Kodi"), the processors it builds for, and the words the Store may
# add after an app's id when starting it (never options for Flatpak itself).
FLATHUB_REF_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]*(\.[A-Za-z0-9_-]+){2,}$")
FLATHUB_ARCHES = ("x86_64", "aarch64")
ARG_RE = re.compile(r"^[A-Za-z0-9_=.:/+-]+$")
RESERVED = {"admin", "administrator", "xenith", "xenithos", "system", "guest", "player", "moderator",
            "support", "server", "root", "sim"}
CTYPES = {"png": "image/png", "js": "text/javascript; charset=utf-8", "css": "text/css; charset=utf-8",
          "svg": "image/svg+xml", "ico": "image/x-icon", "webp": "image/webp", "wav": "audio/wav"}

# ---------------------------------------------------------------------- helpers


def int_or(value, default):
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def clean(text, limit=200):
    """One line the console font can draw: no pipes, no newlines, printable ASCII."""
    text = "".join(ch if 32 <= ord(ch) < 127 and ch != "|" else " " for ch in str(text))
    return " ".join(text.split())[:limit]


# ---------------------------------------------------------------------- feedback

# "Got a recommendation or a bug?" from the consoles' Feedback screen. Each one is kept in the state
# and passed on to a Discord webhook. The webhooks' addresses are in feedback-webhooks.txt in the
# state folder, which is never served and never in the repository: "bug=<address>" and
# "idea=<address>", or one address on a line of its own for both.
FEEDBACK_KINDS = {"bug": ("Bug report", 0xE50914), "idea": ("Recommendation", 0x46D369)}
FEEDBACK_KEEP = 500
DISCORD_HOOK_RE = re.compile(r"^https://(?:canary\.|ptb\.)?discord(?:app)?\.com/api/webhooks/\d+/[A-Za-z0-9_-]+$")


def feedback_hooks(state_dir):
    """{"bug": address, "idea": address}, for the ones that are set."""
    hooks = {}
    try:
        with open(os.path.join(state_dir, "feedback-webhooks.txt"), encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                key, sep, url = line.partition("=")
                if not sep or key.startswith("http"):
                    key, url = "all", line
                key, url = key.strip().lower(), url.strip()
                if DISCORD_HOOK_RE.match(url):
                    hooks[key] = url
    except OSError:
        return {}
    return {kind: hooks.get(kind) or hooks.get("all") for kind in FEEDBACK_KINDS if hooks.get(kind) or hooks.get("all")}


def send_feedback(hook, kind, text, who, version, platform):
    """Posts one to Discord. Mentions in what people wrote never ping anyone."""
    title, color = FEEDBACK_KINDS[kind]
    payload = {"username": "XenithOS feedback", "allowed_mentions": {"parse": []},
               "embeds": [{"title": title, "description": text, "color": color,
                           "fields": [{"name": "From", "value": who, "inline": True},
                                      {"name": "XenithOS", "value": f"{version} on {platform}", "inline": True}],
                           "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}]}
    req = urllib.request.Request(hook, data=json.dumps(payload).encode(), method="POST",
                                 headers={"Content-Type": "application/json", "User-Agent": "XenithOS-Cloud/1"})
    try:
        urllib.request.urlopen(req, timeout=15).close()
    except (OSError, ValueError) as e:
        log(f"feedback: Discord didn't take it: {e}")


def check_gamertag(name):
    """Same rules as the console (source/screen_profile.c). Returns an error or None."""
    if len(name) < 3:
        return "Gamertags need at least 3 characters."
    if len(name) > 15:
        return "Gamertags can be 15 characters at most."
    if not name[0].isascii() or not name[0].isalpha():
        return "Gamertags have to start with a letter."
    if name.endswith(" "):
        return "Gamertags can't end with a space."
    if "  " in name:
        return "Use one space at a time."
    if not all(c.isascii() and (c.isalnum() or c == " ") for c in name):
        return "Use letters, numbers and spaces only."
    if name.lower().replace(" ", "") in RESERVED:
        return "That gamertag is reserved."
    return None


def valid_avatar(code):
    if not AVATAR_RE.match(code or ""):
        return False
    counts = AVATAR_COUNTS_V1 if code[0] == "1" else AVATAR_COUNTS
    return all(int(c, 36) < n for c, n in zip(code[1:], counts))


def hash_pin(pin, salt):
    return hashlib.pbkdf2_hmac("sha256", pin.encode(), salt, PIN_ITERATIONS).hex()


def token_hash(token):
    return hashlib.sha256(token.encode()).hexdigest()


class RateLimit:
    def __init__(self, limit, window):
        self.limit, self.window, self.hits, self.lock = limit, window, {}, threading.Lock()

    def allow(self, key):
        now = time.monotonic()
        with self.lock:
            hits = [t for t in self.hits.get(key, []) if now - t < self.window]
            if len(hits) >= self.limit:
                self.hits[key] = hits
                return False
            hits.append(now)
            self.hits[key] = hits
            return True


def rate_key(ip):
    """What rate limits count by: the address, or an IPv6 address's /64, which is one home or
    phone the way an IPv4 address is (and has more addresses than any limit)."""
    try:
        a = ipaddress.ip_address(ip)
    except ValueError:
        return ip
    if a.version == 6 and not a.ipv4_mapped:
        return str(ipaddress.ip_network(f"{a}/64", strict=False))
    return ip


# ---------------------------------------------------------------------- state


class State:
    def __init__(self, state_dir):
        self.path = os.path.join(state_dir, "state.json")
        os.makedirs(state_dir, exist_ok=True)
        self.lock = threading.RLock()
        self.data = {"motd": "WELCOME TO XENITHOS CLOUD", "inbox": [], "scores": {}, "next_id": 1,
                     "consoles": {}, "accounts": {}}
        if os.path.exists(self.path):
            with open(self.path, encoding="utf-8") as f:
                self.data.update(json.load(f))
        self.data.setdefault("accounts", {})

    def save(self):
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.data, f)
        os.replace(tmp, self.path)

    # -------------------------------------------------------------- feedback
    def add_feedback(self, kind, text, who, version, platform):
        with self.lock:
            kept = self.data.setdefault("feedback", [])
            kept.append({"t": int(time.time()), "kind": kind, "text": text, "from": who,
                         "version": version, "platform": platform})
            del kept[:-FEEDBACK_KEEP]
            self.save()

    # -------------------------------------------------------------- inbox, motd
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

    def online(self):
        """How many consoles and machines said hello in the last ONLINE_WINDOW seconds."""
        cutoff = int(time.time()) - ONLINE_WINDOW
        with self.lock:
            return sum(1 for c in self.data["consoles"].values() if c.get("t", 0) >= cutoff)

    def seen(self, name, ip):
        with self.lock:
            self.data["consoles"][name] = {"t": int(time.time()), "ip": ip}
            acc = self.data["accounts"].get(name.lower())
            if acc:
                acc["last_seen"] = int(time.time())
            self.save()

    # -------------------------------------------------------------- accounts
    def account(self, name):
        return self.data["accounts"].get((name or "").lower())

    def by_token(self, token):
        if not token or len(token) > 128:
            return None
        h = token_hash(token)
        with self.lock:
            for acc in self.data["accounts"].values():
                if any(hmac.compare_digest(h, s) for s in acc["sessions"]):
                    return acc
        return None

    def _new_session(self, acc):
        token = secrets.token_hex(24)
        acc["sessions"].insert(0, token_hash(token))
        del acc["sessions"][SESSIONS_KEEP:]
        acc["last_seen"] = int(time.time())
        return token

    def create(self, name, pin, avatar):
        with self.lock:
            if self.account(name):
                return None, "That gamertag is taken. Try another one."
            salt = secrets.token_bytes(16)
            acc = {"name": name, "salt": salt.hex(), "pin": hash_pin(pin, salt), "avatar": avatar,
                   "joined": time.strftime("%Y-%m-%d", time.gmtime()), "sessions": [], "fails": 0,
                   "locked_until": 0, "last_seen": int(time.time())}
            self.data["accounts"][name.lower()] = acc
            token = self._new_session(acc)
            self.save()
            return (acc, token), None

    def login(self, name, pin):
        acc = self.account(name)
        if not acc:
            hash_pin(pin, b"0" * 16)          # same work either way, so timing doesn't reveal names
            return None, "Wrong gamertag or passcode."
        now = time.time()
        if acc["locked_until"] > now:
            return None, "Too many wrong passcodes. Try again in 15 minutes."
        ok = hmac.compare_digest(hash_pin(pin, bytes.fromhex(acc["salt"])), acc["pin"])
        with self.lock:
            if not ok:
                acc["fails"] += 1
                if acc["fails"] >= LOCK_AFTER:
                    acc["fails"] = 0
                    acc["locked_until"] = now + LOCK_SECONDS
                self.save()
                return None, "Wrong gamertag or passcode."
            acc["fails"] = 0
            token = self._new_session(acc)
            self.save()
            return (acc, token), None

    def logout(self, token):
        with self.lock:
            acc = self.by_token(token)
            if acc:
                acc["sessions"] = [s for s in acc["sessions"] if s != token_hash(token)]
                self.save()
            return acc is not None

    def set_avatar(self, acc, avatar):
        with self.lock:
            acc["avatar"] = avatar
            acc["last_seen"] = int(time.time())
            self.save()

    # -------------------------------------------------------------- scores
    def top(self, game):
        with self.lock:
            return list(self.data["scores"].get(game, []))

    def best_of(self, game, name):
        for rank, e in enumerate(self.top(game), 1):
            if e["name"].lower() == name.lower():
                return e["score"], rank
        return 0, 0

    def submit(self, game, name, score):
        """Keep each name's best score. Returns (rank or None, entry)."""
        with self.lock:
            board = self.data["scores"].setdefault(game, [])
            existing = next((e for e in board if e["name"].lower() == name.lower()), None)
            if existing:
                if score <= existing["score"]:
                    return board.index(existing) + 1, existing
                board.remove(existing)
            entry = {"name": name, "score": score, "t": int(time.time())}
            board.append(entry)
            board.sort(key=lambda e: (-e["score"], e["t"]))
            del board[TOP_KEEP:]
            self.save()
            return (board.index(entry) + 1 if entry in board else None), entry


class Store:
    """store.json: [{"id","kind","name","version","file","description"}], files next to it.
    An item may also list "needs": ids of the game data it comes with (DOOM -> Freedoom).
    Consoles install those along with it, and the USB download includes them."""

    def __init__(self, store_dir):
        self.dir = store_dir
        self.cache = {}   # path -> (mtime, size, sha256)
        self.lock = threading.Lock()
        self.key = None   # StoreKey: what the Store serves goes out signed with it

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
            needs = item.get("needs") if isinstance(item.get("needs"), list) else []
            out.append({"id": iid, "kind": kind, "name": clean(item.get("name", iid), 24),
                        "version": clean(item.get("version", "1"), 12), "size": size,
                        "sha256": sha, "description": clean(item.get("description", ""), 96),
                        "file": fname, "path": path,
                        "needs": [str(n) for n in needs if ID_RE.match(str(n))][:8]})
        return out

    def needs(self, items=None):
        """{item id: [ids of the game data it comes with]}, only data that's in the Store."""
        items = self.items() if items is None else items
        data = {i["id"] for i in items if i["kind"] == "data"}
        out = {}
        for i in items:
            have = [n for n in i["needs"] if n in data and n != i["id"]]
            if have:
                out[i["id"]] = have
        return out

    def find(self, iid):
        return next((i for i in self.items() if i["id"] == iid), None)

    def _list(self, name):
        try:
            with open(os.path.join(self.dir, name), encoding="utf-8") as f:
                raw = json.load(f)
        except (OSError, ValueError):
            return []
        return [x for x in raw if isinstance(x, dict)] if isinstance(raw, list) else []

    def flathub(self):
        """flathub.json: apps from Flathub that PCs and Pis install with Flatpak, in the order
        the Store shows them: [{"id", "name", "ref", "arches", "mb", "shared_mb", "args",
        "description"}]. ref is the Flathub id, arches the processors Flathub builds it for,
        mb its download and shared_mb the shared parts (runtime) it may need, in MB."""
        out, seen = [], set()
        for a in self._list("flathub.json"):
            iid, ref = str(a.get("id", "")), str(a.get("ref", ""))
            arches = [x for x in FLATHUB_ARCHES if x in (a.get("arches") or [])]
            if (not ID_RE.match(iid) or iid.startswith("steam-") or iid in seen or not FLATHUB_REF_RE.match(ref)
                    or len(ref) > 63 or not arches):
                continue
            seen.add(iid)
            args = " ".join(w for w in str(a.get("args", "")).split() if ARG_RE.match(w) and not w.startswith("--"))
            out.append({"id": iid, "name": clean(a.get("name", iid), 24), "ref": ref, "arches": arches,
                        "mb": max(0, int_or(a.get("mb"), 0)), "shared_mb": max(0, int_or(a.get("shared_mb"), 0)),
                        "args": args[:47], "description": clean(a.get("description", ""), 96)})
        return out

    def steam(self):
        """steam.json: games on Steam the Store lists for PCs: [{"id", "name", "appid", "free",
        "description"}]. They're installed and played through the Steam app."""
        out, seen = [], set()
        for g in self._list("steam.json"):
            iid, appid = str(g.get("id", "")), int_or(g.get("appid"), 0)
            if not ID_RE.match(iid) or iid in seen or not 0 < appid < 1 << 31:
                continue
            seen.add(iid)
            out.append({"id": iid, "name": clean(g.get("name", iid), 24), "appid": appid,
                         "free": bool(g.get("free")), "description": clean(g.get("description", ""), 96)})
        return out

    def icon(self, iid, ext):
        if not (self.find(iid) or any(a["id"] == iid for a in self.flathub())
                or any(g["id"] == iid for g in self.steam())):
            return None
        path = os.path.join(self.dir, f"{iid}.{ext}")
        return path if os.path.isfile(path) else None


# ---------------------------------------------------------------------- stats


def server_stats():
    stats = {}
    try:
        with open("/proc/uptime") as f:
            up = int(float(f.read().split()[0]))
        stats["uptime"] = f"{up // 86400}d {up % 86400 // 3600}h {up % 3600 // 60}m"
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


# ---------------------------------------------------------------------- tube

TUBE_ID_RE = re.compile(r"^[A-Za-z0-9_-]{11}$")
TUBE_SID_RE = re.compile(r"^[0-9a-f]{16}$")
TUBE_FORMAT = "bv*[height<=360][vcodec^=avc1]+ba[ext=m4a]/bv*[height<=360]+ba/18/b[height<=480]/b"
TUBE_W, TUBE_H, TUBE_FPS, TUBE_RATE = 426, 240, 20, 24000
TUBE_CHUNK = 256 * 1024
TUBE_IDLE_S = 60               # a stream nobody has asked for in this long is dropped
TUBE_SEARCHES_PER_MIN = 10     # per profile
TUBE_STARTS_PER_10MIN = 30     # per profile
TUBE_LINKS_S = 3600            # a video's links are reused this long (skipping, playing it again)
TUBE_FULL_CLIENTS = "youtube:player_client=default,mweb"   # mweb gets a PO token from the token helper
TUBE_BLOCKED = "YouTube is turning XenithOS Cloud away right now. Try again later."
TUBE_NO_VIDEO = "YouTube didn't hand over that video. Try another one."
TUBE_UNPLAYABLE = "That video can't be played here."
TUBE_BROKE = "YouTube stopped sending that video. Try playing it again."
TUBE_UNPLAYABLE_SIGNS = ("Private video", "confirm your age", "members-only", "Join this channel",
                         "live event will begin", "Premieres in", "has been removed")


def log(message):
    sys.stderr.write(message + "\n")
    sys.stderr.flush()


def tube_blocked(stderr):
    """YouTube rate-limits and bot-checks server addresses; yt-dlp says so on stderr."""
    text = (stderr or b"").decode("utf-8", "replace")
    return "429" in text or "not a bot" in text


def tube_unplayable(stderr):
    """Videos no player client gets without an account: private, age-checked, members only,
    not started yet, taken down. Asking again the slow way won't help."""
    text = (stderr or b"").decode("utf-8", "replace")
    return any(sign in text for sign in TUBE_UNPLAYABLE_SIGNS)


def tube_reason(stderr):
    """yt-dlp's (or ffmpeg's) last error line, for the server log."""
    lines = [line for line in (stderr or b"").decode("utf-8", "replace").splitlines() if line.strip()]
    errors = [line for line in lines if line.startswith("ERROR")] or lines[-1:] or ["no message"]
    return clean(errors[-1], 200)


def link_expiry(url):
    """googlevideo links say when they stop working (unix time), as ?expire= or /expire/."""
    m = re.search(r"[?&/]expire[=/](\d{9,11})", url or "")
    return int(m.group(1)) if m else 0


class TubeStream:
    """One video on its way to a console. YouTube is asked for it in the background (that
    can take a while), so the console starts asking for chunks at once and hears "not ready
    yet" until ffmpeg has something. Chunks come in order; asking for the last one again
    (after a network hiccup) gets the same bytes."""

    def __init__(self, vid, owner, title, duration):
        self.vid, self.owner, self.title, self.duration = vid, owner, title, duration
        self.proc = self.errors = self.helper = None   # ffmpeg, its error output, yt-dlp while it works
        self.problem, self.stopped, self.on_empty = "", False, None
        self.ready = threading.Event()                 # ffmpeg runs, or it couldn't start, or it stopped
        self.sent, self.last_off, self.last_data, self.eof = 0, -1, b"", False
        self.touched = time.monotonic()
        self.lock = threading.Lock()                   # one reader at a time
        self.plock = threading.Lock()                  # the processes

    def active(self):
        """Counts towards Tube's limit: still getting ready, or playing."""
        return not self.problem and not self.stopped and not self.eof

    def run(self, cmd, timeout):
        """Run yt-dlp where stop() can end it. Returns (exit code, stdout, stderr)."""
        with self.plock:
            if self.stopped:
                return -1, b"", b""
            p = self.helper = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            out, err = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            out, err = p.communicate()
            err += b"\nERROR: yt-dlp took too long"
        with self.plock:
            self.helper = None
        return p.returncode, out, err

    def attach(self, proc, errors):
        """ffmpeg is running: chunks can flow."""
        with self.plock:
            late = self.stopped
            if not late:
                self.proc, self.errors = proc, errors
        if late:                                       # the console left while YouTube was asked
            proc.kill()
            proc.wait()
            errors.close()
        self.ready.set()

    def fail(self, problem):
        self.problem = problem
        self.ready.set()

    def read(self, off, wait=2.5):
        """Bytes from `off`: b"" if nothing is ready yet, None once the stream is over.
        RuntimeError (with a message for people) when the video couldn't be played."""
        end = time.monotonic() + wait
        self.touched = time.monotonic()
        if not self.ready.wait(wait):
            return b""
        if self.problem:
            raise RuntimeError(self.problem)
        if not self.proc:
            return None
        with self.lock:
            self.touched = time.monotonic()
            if off == self.last_off:
                return self.last_data
            if off != self.sent:
                raise ValueError("out of order")
            data, fd = bytearray(), self.proc.stdout.fileno()
            while len(data) < TUBE_CHUNK and not self.eof:
                left = end - time.monotonic() if not data else 0.05
                if left <= 0:
                    break
                ready, _, _ = select.select([fd], [], [], left)
                if not ready:
                    break
                more = os.read(fd, TUBE_CHUNK - len(data))
                if not more:
                    self.eof = True
                    break
                data += more
            if not data:
                if self.eof and not self.sent:         # ffmpeg gave up before the first byte
                    self.gave_up()
                    raise RuntimeError(self.problem)
                return None if self.eof else b""
            self.last_off, self.last_data = off, bytes(data)
            self.sent += len(data)
            return self.last_data

    def gave_up(self):
        self.problem = TUBE_BROKE
        why = b""
        if self.errors:
            self.errors.seek(0)
            why = self.errors.read()[-400:]
        log(f"tube: {self.vid}: ffmpeg sent nothing: {tube_reason(why)}")
        if self.on_empty:
            self.on_empty(self.vid)                    # its links don't work any more

    def stop(self):
        with self.plock:
            self.stopped = True
            helper, proc = self.helper, self.proc
        self.ready.set()
        if helper and helper.poll() is None:
            helper.kill()
        if proc:
            if proc.poll() is None:
                proc.kill()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
            if proc.stdout:
                proc.stdout.close()
        if self.errors:
            self.errors.close()


class Tube:
    """YouTube for XenithOS. yt-dlp finds videos, ffmpeg turns one into a stream the Xbox can
    play: 426x240 JPEG pictures at 20 a second and 24 kHz mono sound, in an AVI. Profiles
    only, a few streams at a time, so the server can't be used as an open proxy.

    With Deno set up, a video is asked for at most twice: the quick way first (no
    JavaScript, a few seconds), then, if YouTube refuses that, the full way: Deno solves
    YouTube's JavaScript challenges and the token helper (xenith-pot.service) gives the
    mweb client a PO token. A video's links are kept for a while, so skipping ahead or
    playing it again doesn't ask YouTube at all."""

    def __init__(self, ytdlp, ffmpeg, max_streams=3, deno="", plugins=""):
        self.ytdlp, self.ffmpeg, self.max_streams = ytdlp, ffmpeg, max_streams
        self.deno, self.plugins = deno, plugins
        self.lock = threading.Lock()
        self.streams = {}
        self.search_cache = {}
        self.thumbs = {}
        self.known = {}               # video id -> (title, seconds), from searches and plays
        self.links = {}               # video id -> formats, title, duration, until
        self.full_until = 0.0         # YouTube refused a quick ask lately: go straight to the full way
        self.searches = RateLimit(TUBE_SEARCHES_PER_MIN, 60)
        self.starts = RateLimit(TUBE_STARTS_PER_10MIN, 600)

    def available(self):
        return bool(self.ytdlp and self.ffmpeg)

    def command(self, mode, args):
        """yt-dlp for a search, a plain ask (no Deno set up), a quick ask or a full one."""
        cmd = [self.ytdlp, "--no-warnings", "--no-cache-dir"]
        if self.deno and mode in ("search", "quick"):
            cmd.append("--no-js-runtimes")
        if self.plugins and mode in ("plain", "full"):
            cmd += ["--plugin-dirs", self.plugins]
        if mode == "full":
            cmd += ["--js-runtimes", f"deno:{self.deno}", "--extractor-args", TUBE_FULL_CLIENTS]
        return cmd + args

    def search(self, query):
        key = query.lower()
        with self.lock:
            hit = self.search_cache.get(key)
        if hit and time.time() - hit[0] < 600:
            return hit[1]
        out = subprocess.run(self.command("search", ["--flat-playlist", "-J", f"ytsearch16:{query}"]),
                             capture_output=True, timeout=40)
        if out.returncode != 0:
            log(f"tube: search: {tube_reason(out.stderr)}")
            if tube_blocked(out.stderr):
                raise RuntimeError(TUBE_BLOCKED)
        data = json.loads(out.stdout or b"{}")
        rows = []
        for e in data.get("entries") or []:
            vid = e.get("id") or ""
            if not TUBE_ID_RE.match(vid) or e.get("live_status") == "is_upcoming":
                continue
            rows.append((vid, clean(e.get("title") or "", 96) or "Untitled",
                         clean(e.get("channel") or e.get("uploader") or "", 40), int(e.get("duration") or 0)))
        with self.lock:
            if len(self.search_cache) > 500:
                self.search_cache.clear()
            self.search_cache[key] = (time.time(), rows)
            if len(self.known) > 5000:
                self.known.clear()
            for vid, title, _, seconds in rows:
                self.known[vid] = (title, seconds)
        return rows

    def thumb(self, vid):
        with self.lock:
            hit = self.thumbs.get(vid)
        if hit:
            return hit
        req = urllib.request.Request(f"https://i.ytimg.com/vi/{vid}/mqdefault.jpg",
                                     headers={"User-Agent": "XenithOS-Cloud/" + VERSION})
        with urllib.request.urlopen(req, timeout=10) as r:
            data = r.read(512 * 1024)
        with self.lock:
            if len(self.thumbs) > 400:
                self.thumbs.clear()
            self.thumbs[vid] = data
        return data

    def reap(self):
        now = time.monotonic()
        with self.lock:
            dead = [sid for sid, s in self.streams.items()
                    if now - s.touched > (15 if s.problem else TUBE_IDLE_S)]
            gone = [self.streams.pop(sid) for sid in dead]
        for s in gone:
            s.stop()

    def stop(self, sid):
        with self.lock:
            s = self.streams.pop(sid, None)
        if s:
            s.stop()

    def get(self, sid):
        with self.lock:
            return self.streams.get(sid)

    def start(self, vid, at, owner):
        """Returns (session id, title, duration) at once and asks YouTube in the background;
        the console's chunk requests hear how that went. RuntimeError (with a message for
        people) when Tube is full."""
        self.reap()
        with self.lock:
            mine = [sid for sid, s in self.streams.items() if s.owner == owner]
        for sid in mine:                          # one video per profile at a time
            self.stop(sid)
        with self.lock:
            if sum(s.active() for s in self.streams.values()) >= self.max_streams:
                raise RuntimeError("Tube is busy right now. Try again in a few minutes.")
            title, seconds = self.known.get(vid, ("", 0))
            sid = secrets.token_hex(8)
            stream = self.streams[sid] = TubeStream(vid, owner, title, seconds)
        threading.Thread(target=self.prepare, args=(stream, at), daemon=True).start()
        return sid, stream.title, stream.duration

    def prepare(self, stream, at):
        """Background: get the video's links, then start ffmpeg on them."""
        errors = None
        try:
            fmts = self.ask(stream)["formats"]
            args = [self.ffmpeg, "-nostdin", "-loglevel", "error"]
            for f in fmts:
                if at > 0:
                    args += ["-ss", str(at)]
                headers = "".join(f"{k}: {v}\r\n" for k, v in (f.get("http_headers") or {}).items())
                if headers:
                    args += ["-headers", headers]
                args += ["-i", f["url"]]
            vi = next((i for i, f in enumerate(fmts) if f.get("vcodec") not in (None, "none")), 0)
            ai = next((i for i, f in enumerate(fmts) if f.get("acodec") not in (None, "none")), None)
            args += ["-map", f"{vi}:v:0"] + (["-map", f"{ai}:a:0"] if ai is not None else [])
            args += ["-vf", f"scale={TUBE_W}:{TUBE_H}:force_original_aspect_ratio=decrease,"
                            f"pad={TUBE_W}:{TUBE_H}:(ow-iw)/2:(oh-ih)/2:black,fps={TUBE_FPS}",
                     "-c:v", "mjpeg", "-q:v", "5", "-pix_fmt", "yuvj420p", "-threads", "2"]
            if ai is not None:
                args += ["-c:a", "pcm_s16le", "-ar", str(TUBE_RATE), "-ac", "1"]
            args += ["-f", "avi", "pipe:1"]
            nice = shutil.which("nice")            # keep the website and API quick while it works
            errors = tempfile.TemporaryFile()
            proc = subprocess.Popen(([nice, "-n", "10"] if nice else []) + args,
                                    stdout=subprocess.PIPE, stderr=errors)
            stream.on_empty = self.forget
            stream.attach(proc, errors)
        except RuntimeError as e:
            stream.fail(str(e))
        except Exception as e:                     # never leave a console waiting for nothing
            log(f"tube: {stream.vid}: {type(e).__name__}: {e}")
            if errors:
                errors.close()
            stream.fail(TUBE_NO_VIDEO)

    def ask(self, stream):
        """The video's formats: kept from a recent ask, or from YouTube."""
        vid, now = stream.vid, time.time()
        with self.lock:
            link = self.links.get(vid)
        if link and link["until"] > now:
            return link
        if not self.deno:
            modes = ["plain"]
        elif now < self.full_until:
            modes = ["full"]
        else:
            modes = ["quick", "full"]
        blocked = False
        for mode in modes:
            code, out, err = stream.run(self.command(mode, ["-j", "--no-playlist", "-f", TUBE_FORMAT, "--",
                                                            f"https://www.youtube.com/watch?v={vid}"]),
                                        90 if mode == "full" else 45)
            if stream.stopped:
                raise RuntimeError("Stopped.")
            link = self.parse(out) if code == 0 else None
            if link:
                self.remember(vid, link)
                return link
            log(f"tube: {vid} ({mode}): {tube_reason(err) if code else 'no formats with links'}")
            if code and tube_unplayable(err):
                raise RuntimeError(TUBE_UNPLAYABLE)
            if tube_blocked(err):
                blocked = True
                if mode == "quick":
                    self.full_until = time.time() + 600
        raise RuntimeError(TUBE_BLOCKED if blocked else TUBE_NO_VIDEO)

    @staticmethod
    def parse(out):
        """What ffmpeg needs from yt-dlp's -j answer, or None."""
        try:
            info = json.loads(out or b"null")
        except ValueError:
            return None
        if not isinstance(info, dict):
            return None
        fmts = [{k: f.get(k) for k in ("url", "http_headers", "vcodec", "acodec")}
                for f in info.get("requested_formats") or [info]]
        if not all(f["url"] for f in fmts):
            return None
        return {"formats": fmts, "title": clean(info.get("title") or "", 96) or "Untitled",
                "duration": int(info.get("duration") or 0)}

    def remember(self, vid, link):
        now = time.time()
        ends = [link_expiry(f["url"]) for f in link["formats"]]
        link["until"] = min([now + TUBE_LINKS_S] + [end - 300 for end in ends if end])
        with self.lock:
            if len(self.links) > 300:
                self.links.clear()
            if link["until"] > now + 60:
                self.links[vid] = link
            self.known[vid] = (link["title"], link["duration"])

    def forget(self, vid):
        """Its links stopped working: ask YouTube again next time."""
        with self.lock:
            self.links.pop(vid, None)


# ---------------------------------------------------------------------- downloads


def public_addr(handler, configured):
    """The address consoles should use: XENITH_PUBLIC_ADDR, or the one this request came to."""
    if configured:
        return configured
    host = (handler.headers.get("Host") or "").strip()
    return host if PUBLIC_ADDR_RE.match(host) else ""


_zip_cache = {"key": None, "data": b""}
_zip_lock = threading.Lock()


def xbox_zip(store, addr):
    """Everything for a USB stick: XenithOS, the apps with their thumbnails, the game data
    they come with (Freedoom for DOOM), and a config with the XenithOS Cloud address. Built
    from what's in the Store, and kept until the Store or the address changes (with the game
    data it's tens of MB)."""
    items_list = store.items()
    items = {i["id"]: i for i in items_list}
    needs = store.needs(items_list)
    apps = [iid for iid, it in items.items() if it["kind"] == "app"]
    data = sorted({n for a in apps for n in needs.get(a, [])})
    files = [it["path"] for it in items.values() if it["id"] in apps or it["id"] in data or it["kind"] == "system"]
    manifest = os.path.join(store.dir, "store.json")
    key = (addr, os.path.getmtime(manifest) if os.path.exists(manifest) else 0,
           tuple((p, os.path.getmtime(p), os.path.getsize(p)) for p in sorted(files)))
    with _zip_lock:
        if _zip_cache["key"] == key:
            return _zip_cache["data"]
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
            if "xenith" in items:
                z.write(items["xenith"]["path"], "xenon.elf")
            for iid in apps:
                it = items[iid]
                z.write(it["path"], f"apps/{iid}.elf32")
                z.writestr(f"apps/{iid}.txt", f"name={it['name']}\nversion={it['version']}\n")
                icon = store.icon(iid, "ximg")
                if icon:
                    z.write(icon, f"apps/{iid}.ximg")
            for did in data:                      # stored as they are: game data barely compresses
                z.write(items[did]["path"], f"data/{items[did]['file']}", compress_type=zipfile.ZIP_STORED)
            z.writestr("xenith/config.txt", (f"server={addr}\n" if addr else "") + "name=PLAYER\n")
            z.writestr("README.txt",
                       "XenithOS for the Xbox 360\r\n\r\n"
                       "Copy everything here to the root of a FAT32 USB stick, next to ABadAvatar and\r\n"
                       "FreeMyXe. Start the exploit, choose Launch XeLL in FreeMyXe, and XeLL loads\r\n"
                       "xenon.elf. Never write to the NAND while using the exploit.\r\n\r\n"
                       "The data folder holds the game data the apps come with, like Freedoom for DOOM.\r\n")
        _zip_cache["key"], _zip_cache["data"] = key, buf.getvalue()
        return _zip_cache["data"]


# ---------------------------------------------------------------------- the Store's lists


def store_rows(items):
    return "".join(f"{i['id']}|{i['kind']}|{i['name']}|{i['version']}|{i['size']}|"
                   f"{i['sha256']}|{i['file']}|{i['description']}\n" for i in items)


def needs_rows(needs):
    return "".join(f"{iid}|{','.join(ns)}\n" for iid, ns in needs.items())


def flathub_rows(apps):
    return "".join(f"{a['id']}|{a['name']}|{a['ref']}|{','.join(a['arches'])}|{a['mb']}|{a['shared_mb']}|"
                   f"{a['args']}|{a['description']}\n" for a in apps)


def steam_rows(games):
    return "".join(f"{g['id']}|{g['name']}|{g['appid']}|{1 if g['free'] else 0}|{g['description']}\n"
                   for g in games)


def store_bundle(store, pc):
    """/v1/store/all: what the Store screen needs, in one reply instead of one per list and
    thumbnail (each is a trip through the relay). "XSTORE 1\\n", then sections of
    "<name> <length>\\n" followed by that many bytes: store, needs, flathub and steam (with pc),
    then "icon:<id>" for each .ximg thumbnail, and "end 0\\n" last."""
    items = store.items()
    apps, games = (store.flathub(), store.steam()) if pc else ([], [])
    out = [b"XSTORE 1\n"]

    def section(name, data):
        if isinstance(data, str):
            data = data.encode("ascii", "replace")
        out.append(f"{name} {len(data)}\n".encode() + data)

    section("store", store_rows(items))
    section("needs", needs_rows(store.needs(items)))
    if pc:
        section("flathub", flathub_rows(apps))
        section("steam", steam_rows(games))
    for iid in dict.fromkeys([i["id"] for i in items] + [a["id"] for a in apps] + [g["id"] for g in games]):
        path = os.path.join(store.dir, f"{iid}.ximg")
        if os.path.isfile(path):
            with open(path, "rb") as f:
                section(f"icon:{iid}", f.read())
    out.append(b"end 0\n")
    return b"".join(out)


def https_addr_ok(addr):
    """XENITH_PUBLIC_HTTPS: a name, or name:port, for PCs (linux/http_posix.c)."""
    m = HTTPS_ADDR_RE.match(addr or "")
    return bool(m) and not addr.startswith((".", "-")) and (not m.group(2) or 0 < int(m.group(2)) < 65536)


# ---------------------------------------------------------------------- Store signatures

# The Store's replies travel over plain HTTP, and the SHA-256 that checks each download comes in
# them, so someone in between could change a list and the file together. So with a key
# (XENITH_STORE_KEY) every list, the bundle and every thumbnail go out signed with XenithOS Cloud's
# ed25519 key, in a header older consoles never look at:
#     X-Xenith-Sig: 1 <key id> <signature>
# "1" is the format, the key id the first 16 hex digits of the public key (so during a change of
# key both can be sent, and each console uses the one it knows), the signature 128 hex digits over
#     XenithOS Store 1\n<name>\n<SHA-256 of the body, in hex>\n
# where the name says which reply it is (store_sig_name), so one can't stand in for another.
# Consoles built with the public key (source/store_key.h) refuse a list without a good signature;
# the files themselves are checked against the SHA-256s in the signed list.
#
# The standard library has no ed25519, so here it is after RFC 8032. Python's integers don't run in
# constant time; it only ever signs this server's own replies, once for each new content (made),
# so an outsider can't have it sign much, let alone time it.
ED_P = 2 ** 255 - 19
ED_L = 2 ** 252 + 27742317777372353535851937790883648493
ED_D = -121665 * pow(121666, ED_P - 2, ED_P) % ED_P
ED_I = pow(2, (ED_P - 1) // 4, ED_P)                   # a square root of -1
ED_ZERO = (0, 1, 1, 0)
STORE_SIG_HEADER = "X-Xenith-Sig"
STORE_KEY_RE = re.compile(r"^[0-9a-f]{64}$")


def _ed_add(a, b):
    """Points in extended coordinates (X, Y, Z, T): x = X/Z, y = Y/Z, xy = T/Z. Adds and doubles."""
    p = ED_P
    x1, y1, z1, t1 = a
    x2, y2, z2, t2 = b
    e1, e2 = (y1 - x1) * (y2 - x2) % p, (y1 + x1) * (y2 + x2) % p
    c, d = 2 * ED_D * t1 * t2 % p, 2 * z1 * z2 % p
    e, f, g, h = e2 - e1, d - c, d + c, e2 + e1
    return e * f % p, g * h % p, f * g % p, e * h % p


def _ed_mul(s, point):
    """s times point, always the same 256 doublings and additions whatever s is."""
    q = ED_ZERO
    for i in range(255, -1, -1):
        q = _ed_add(q, q)
        added = _ed_add(q, point)
        q = added if (s >> i) & 1 else q
    return q


def _ed_encode(point):
    x, y, z, _ = point
    zi = pow(z, ED_P - 2, ED_P)
    x, y = x * zi % ED_P, y * zi % ED_P
    return (y | (x & 1) << 255).to_bytes(32, "little")


def _ed_decode(data):
    """The point 32 bytes stand for, or None when they aren't one written the one proper way."""
    if len(data) != 32:
        return None
    y = int.from_bytes(data, "little")
    sign, y = y >> 255, y & ((1 << 255) - 1)
    if y >= ED_P:
        return None
    x2 = (y * y - 1) * pow(ED_D * y * y + 1, ED_P - 2, ED_P) % ED_P
    x = pow(x2, (ED_P + 3) // 8, ED_P)
    if (x * x - x2) % ED_P:
        x = x * ED_I % ED_P
    if (x * x - x2) % ED_P or (x == 0 and sign):
        return None
    if x & 1 != sign:
        x = ED_P - x
    return x, y, 1, x * y % ED_P


ED_B = _ed_decode((4 * pow(5, ED_P - 2, ED_P) % ED_P).to_bytes(32, "little"))


def _ed_secret(seed):
    h = hashlib.sha512(seed).digest()
    return int.from_bytes(h[:32], "little") & ((1 << 254) - 8) | 1 << 254, h[32:]


def ed25519_public(seed):
    """The public key (32 bytes) of a 32-byte private key (RFC 8032's seed)."""
    return _ed_encode(_ed_mul(_ed_secret(seed)[0], ED_B))


def ed25519_sign(seed, msg, public=None):
    a, prefix = _ed_secret(seed)
    public = public or _ed_encode(_ed_mul(a, ED_B))
    r = int.from_bytes(hashlib.sha512(prefix + msg).digest(), "little") % ED_L
    big_r = _ed_encode(_ed_mul(r, ED_B))
    k = int.from_bytes(hashlib.sha512(big_r + public + msg).digest(), "little") % ED_L
    return big_r + ((r + k * a) % ED_L).to_bytes(32, "little")


def ed25519_verify(public, msg, sig):
    """RFC 8032's check without the cofactor, as Monocypher and libsodium make it: S below L,
    A a proper point, and [S]B - [k]A written out the same as R."""
    if len(sig) != 64:
        return False
    a, s = _ed_decode(public), int.from_bytes(sig[32:], "little")
    if a is None or s >= ED_L:
        return False
    k = int.from_bytes(hashlib.sha512(sig[:32] + public + msg).digest(), "little") % ED_L
    minus_a = (ED_P - a[0], a[1], a[2], ED_P - a[3])
    return _ed_encode(_ed_add(_ed_mul(s, ED_B), _ed_mul(k, minus_a))) == sig[:32]


def store_sig_name(parts, query):
    """Which signed reply a Store route is ("store", "all pc", "icon doom.ximg"), or None for one
    that isn't signed (the files: the signed list has their SHA-256s)."""
    names = {("v1", "store"): "store", ("v1", "store", "needs"): "needs",
             ("v1", "store", "flathub"): "flathub", ("v1", "store", "steam"): "steam"}
    if tuple(parts) in names:
        return names[tuple(parts)]
    if parts == ["v1", "store", "all"]:
        return "all pc" if query.get("pc", [""])[0] == "1" else "all"
    if len(parts) == 4 and parts[:3] == ["v1", "store", "icon"]:
        return "icon " + parts[3]
    return None


def store_sig_text(name, body):
    """What a signature covers: the reply's name and its body's SHA-256."""
    return f"XenithOS Store 1\n{name}\n{hashlib.sha256(body).hexdigest()}\n".encode()


class StoreKey:
    """XenithOS Cloud's Store signing key, and the X-Xenith-Sig headers made with it."""

    def __init__(self, seed):
        self.seed = seed
        self.public = ed25519_public(seed)
        self.key_id = self.public.hex()[:16]
        self.made = {}                         # signed text -> header (one per content)
        self.lock = threading.Lock()

    def header(self, name, body):
        text = store_sig_text(name, body)
        with self.lock:
            value = self.made.get(text)
        if not value:
            value = f"1 {self.key_id} {ed25519_sign(self.seed, text, self.public).hex()}"
            with self.lock:
                if len(self.made) >= 1024:
                    self.made.clear()
                self.made[text] = value
        return value


def read_store_key(path):
    """The private key in a key file (store-key new): its first line that isn't a # comment."""
    with open(path, encoding="ascii") as f:
        for line in f:
            line = line.strip().lower()
            if line and not line.startswith("#"):
                if STORE_KEY_RE.match(line):
                    return bytes.fromhex(line)
                break
    raise ValueError("it doesn't hold a key (64 hex digits on a line of their own)")


def load_store_key(path, kept_out_of):
    """StoreKey from the file XENITH_STORE_KEY names, or None after saying why not. kept_out_of:
    the folders XenithOS Cloud serves files from or keeps its state in, where a key could leak
    (through store.json, a backup of the state, a stray copy in the website)."""
    real = os.path.realpath(path)
    for folder in kept_out_of:
        inside = os.path.realpath(folder)
        if real == inside or real.startswith(inside.rstrip(os.sep) + os.sep):
            log(f"XENITH_STORE_KEY is in {folder}, which XenithOS Cloud serves or keeps its state in. Move it "
                f"out (to a folder of its own). Until then Store lists go out unsigned, and consoles that "
                f"check refuse them.")
            return None
    try:
        seed = read_store_key(path)
        loose = os.stat(path).st_mode & 0o077
    except (OSError, ValueError, UnicodeDecodeError) as e:
        log(f"XENITH_STORE_KEY {path}: {e}. Until it's fixed Store lists go out unsigned, and consoles that "
            f"check refuse them.")
        return None
    if loose:
        log(f"XENITH_STORE_KEY {path} can be read by others than its owner: chmod 600 it")
    return StoreKey(seed)


def store_keys_in(header_file):
    """The public keys (hex) a console build trusts, from source/store_key.h."""
    try:
        with open(header_file, encoding="utf-8") as f:
            text = re.sub(r"/\*.*?\*/|//[^\n]*", "", f.read(), flags=re.S)
    except OSError:
        return []
    return re.findall(r'"([0-9a-f]{64})"', text)


def store_sig_good(values, name, body, keys):
    """True when one of the X-Xenith-Sig values is a good signature of this reply by one of the
    public keys (hex): what consoles check (source/store_sig.c)."""
    text = store_sig_text(name, body)
    for value in values:
        f = value.split()
        if len(f) != 3 or f[0] != "1" or not re.fullmatch(r"[0-9a-f]{16}", f[1]) \
                or not re.fullmatch(r"[0-9a-f]{128}", f[2]):
            continue
        for key in keys:
            if key.startswith(f[1]) and ed25519_verify(bytes.fromhex(key), text, bytes.fromhex(f[2])):
                return True
    return False


def store_key_command(args):
    """xenith_cloud.py store-key new <file> | show <file> | check <host:port or URL> [public key]"""
    def tell(public):
        print(f"Public half: {public}  (key id {public[:16]})\n"
              f"For source/store_key.h, the line inside store_keys[]:\n    \"{public}\",")

    if len(args) == 2 and args[0] == "new":
        seed = secrets.token_bytes(32)
        public = ed25519_public(seed).hex()
        try:                                    # never over a key that's there: it may be in use
            fd = os.open(args[1], os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except OSError as e:
            print(f"{args[1]}: {e.strerror}. A key file is never replaced like this.", file=sys.stderr)
            return 1
        with os.fdopen(fd, "w", encoding="ascii") as f:
            f.write("# XenithOS Cloud's Store signing key (ed25519). Keep it secret, and keep a copy somewhere\n"
                    "# safe: consoles built with its public half only trust Store lists signed with it.\n"
                    f"# Public half: {public}\n{seed.hex()}\n")
        print(f"Made {args[1]}, which only you can read.")
        tell(public)
        return 0
    if len(args) == 2 and args[0] == "show":
        try:
            tell(ed25519_public(read_store_key(args[1])).hex())
        except (OSError, ValueError, UnicodeDecodeError) as e:
            print(f"{args[1]}: {e}", file=sys.stderr)
            return 1
        return 0
    if len(args) in (2, 3) and args[0] == "check":
        here = os.path.dirname(os.path.abspath(__file__))
        keys = [args[2].lower()] if len(args) == 3 else store_keys_in(os.path.join(here, "..", "source", "store_key.h"))
        if not keys or not all(STORE_KEY_RE.match(k) for k in keys):
            print("No public key to check with: give one (64 hex digits), or put it in source/store_key.h.",
                  file=sys.stderr)
            return 2
        routes, bad, first = ["/v1/store", "/v1/store/needs", "/v1/store/flathub", "/v1/store/steam",
                              "/v1/store/all", "/v1/store/all?pc=1"], 0, ""
        base = (args[1] if args[1].startswith(("http://", "https://")) else "http://" + args[1]).rstrip("/")
        while routes:
            route = routes.pop(0)
            try:
                with urllib.request.urlopen(base + route, timeout=20) as r:
                    body, values = r.read(), r.headers.get_all(STORE_SIG_HEADER) or []
            except urllib.error.HTTPError as e:
                if "/icon/" in route and e.code == 404:
                    print(f"     {route}: no thumbnail to check")
                    continue
                print(f"FAIL {route}: {e}")
                bad += 1
                continue
            except (OSError, ValueError) as e:
                print(f"FAIL {route}: {e}")
                bad += 1
                continue
            url = urlparse(route)
            good = store_sig_good(values, store_sig_name([p for p in url.path.split("/") if p], parse_qs(url.query)),
                                  body, keys)
            print(f"{'PASS' if good else 'FAIL'} {route}: "
                  f"{'signed' if good else 'a signature that does not check out' if values else 'not signed'}")
            bad += not good
            if route == "/v1/store" and body and not first:
                first = body.split(b"|", 1)[0].decode("ascii", "replace")
                if ID_RE.match(first):
                    routes.append(f"/v1/store/icon/{first}.ximg")
        print("Every Store reply checked out." if not bad else f"{bad} didn't check out.")
        return 1 if bad else 0
    print(store_key_command.__doc__, file=sys.stderr)
    return 2


def lan_addr_ok(addr):
    """XENITH_LAN_ADDR: a private IPv4 address and port (the Xbox has no DNS)."""
    m = LAN_ADDR_RE.match(addr or "")
    if not m or not 0 < int(m.group(2)) < 65536:
        return False
    try:
        return ipaddress.ip_address(m.group(1)).is_private
    except ValueError:
        return False


# ---------------------------------------------------------------------- http


def kv(d):
    return "".join(f"{k}={clean(v)}\n" for k, v in d.items())


def make_handler(state, store, admin_key, web_dir="web", console_key="", tube=None, public="",
                 downloads="downloads", lan="", server_id=""):
    last_score = {}
    last_score_lock = threading.Lock()
    creates = RateLimit(CREATE_PER_HOUR, 3600)
    logins = RateLimit(LOGIN_PER_10MIN, 600)
    feedback_by_ip = RateLimit(30, 3600)          # consoles reached through the relay share an address
    feedback_by_name = RateLimit(6, 3600)

    class Handler(BaseHTTPRequestHandler):
        server_version = "XenithOS-Cloud/" + VERSION
        protocol_version = "HTTP/1.0"
        https = ""                                # XENITH_PUBLIC_HTTPS, which serve() sets: for /v1/lan
        secure = False                            # the request came over HTTPS (edge())

        def log_message(self, fmt, *args):
            line = fmt % args
            if "/v1/tube/chunk" in line or "/v1/tube/thumb" in line:
                return                       # several a second while a video plays
            sys.stderr.write("%s %s\n" % (self.client_address[0], line))

        def reply(self, code, text="", ctype="text/plain; charset=ascii", cache=None, headers=None):
            body = text.encode("ascii", "replace") if isinstance(text, str) else text
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            if cache:
                self.send_header("Cache-Control", cache)
            for name, value in (headers or {}).items():
                self.send_header(name, value)
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)

        def error(self, code, message):
            return self.reply(code, f"error={clean(message, 120)}\n")

        def send_file(self, path, ctype, cache="public, max-age=300"):
            with open(path, "rb") as f:
                return self.reply(200, f.read(), ctype, cache)

        def signed(self, name, text, ctype="text/plain; charset=ascii", cache=None):
            """A Store reply, with its X-Xenith-Sig when there's a key."""
            body = text.encode("ascii", "replace") if isinstance(text, str) else text
            key = store.key
            return self.reply(200, body, ctype, cache, {STORE_SIG_HEADER: key.header(name, body)} if key else None)

        def body(self):
            n = int(self.headers.get("Content-Length") or 0)
            if n < 0 or n > MAX_BODY:
                return None
            return self.rfile.read(n).decode("utf-8", "replace")

        def is_admin(self):
            key = self.headers.get("X-Xenith-Key", "")
            return bool(admin_key) and hmac.compare_digest(key.encode(), admin_key.encode())

        def session_reply(self, acc, token):
            return self.reply(200, kv({"name": acc["name"], "token": token, "avatar": acc["avatar"],
                                       "joined": acc["joined"]}))

        # -------------------------------------------------------------- GET
        def do_GET(self):
            url = urlparse(self.path)
            parts = [unquote(p) for p in url.path.split("/") if p]
            q = parse_qs(url.query)

            if parts == ["v1", "ping"]:
                return self.reply(200, "pong\n")

            if parts == ["v1", "lan"]:
                return self.reply(200, kv({"id": server_id, **({"lan": lan} if lan else {}),
                                           **({"https": self.https} if self.https else {})}), cache="no-store")

            if parts == ["v1", "status"]:
                lines = {"motd": state.data["motd"], "server": "XENITHOS CLOUD " + VERSION,
                         "consoles": str(len(state.data["consoles"])),
                         "players": str(len(state.data["accounts"])),
                         "apps": str(len(store.items())),
                         "store_key": store.key.key_id if store.key else "none"}
                lines.update(server_stats())
                return self.reply(200, kv(lines))

            if parts == ["v1", "hello"]:
                name = clean(q.get("name", [""])[0], 15)
                if not check_gamertag(name) or LEGACY_NAME_RE.match(name):
                    state.seen(name, self.client_address[0])
                lines = {"motd": state.data["motd"], "server": "XENITHOS CLOUD " + VERSION,
                         "players": str(len(state.data["accounts"])), "online": str(state.online()),
                         "news": str(len(state.data["inbox"])), "now": str(int(time.time())),
                         "messages": str(len(state.data["inbox"]))}
                lines.update(server_stats())        # 0.4 consoles still show these
                return self.reply(200, kv(lines))

            if parts == ["v1", "news"]:
                return self.reply(200, "".join(f"{m['id']}|{m['t']}|{m['text']}\n"
                                               for m in state.data["inbox"]))

            if parts == ["v1", "feedback"]:
                if not self.is_admin():
                    return self.error(401, "Admin key required.")
                return self.reply(200, "".join(f"{f['t']}|{f['kind']}|{f['from']}|{f['version']}|{f['platform']}|{f['text']}\n"
                                               for f in reversed(state.data.get("feedback", []))))

            if len(parts) == 4 and parts[:3] == ["v1", "tube", "thumb"] and parts[3].endswith(".jpg"):
                vid = parts[3][:-4]
                if not tube or not tube.available():
                    return self.error(503, "Tube isn't available on this server.")
                if not TUBE_ID_RE.match(vid):
                    return self.error(404, "No such video.")
                try:
                    return self.reply(200, tube.thumb(vid), "image/jpeg", "public, max-age=86400")
                except OSError:
                    return self.error(502, "No thumbnail.")

            if parts == ["v1", "tube", "chunk"] or parts == ["v1", "tube", "stop"]:
                sid = q.get("s", [""])[0]
                stream = tube.get(sid) if tube and TUBE_SID_RE.match(sid) else None
                if parts[2] == "stop":
                    if stream:
                        tube.stop(sid)
                    return self.reply(200, "ok=1\n")
                if not stream:
                    return self.error(410, "That video stream has finished.")
                try:
                    data = stream.read(int(q.get("off", ["0"])[0]))
                except ValueError:
                    return self.error(400, "Out of order.")
                except RuntimeError as e:            # kept, so a console that retries hears it again
                    return self.error(503, str(e))
                if data is None:
                    tube.stop(sid)
                    return self.error(410, "The end.")
                if not data:
                    return self.reply(204)
                return self.reply(200, data, "application/octet-stream", "no-store")

            if parts == ["download", "config.txt"]:
                addr = public_addr(self, public)
                return self.reply(200, (f"server={addr}\n" if addr else "") + "name=PLAYER\n", cache="no-store")

            if parts == ["download", "xenithos-xbox360.zip"]:
                return self.reply(200, xbox_zip(store, public_addr(self, public)), "application/zip", "no-store")

            if parts == ["download", "xenithos-source.tar.gz"]:
                path = os.path.join(downloads, "xenithos-source.tar.gz")
                if os.path.isfile(path):
                    return self.send_file(path, "application/gzip")
                return self.error(404, "Not found.")

            if parts == ["install.sh"]:
                path = os.path.join(downloads, "install.sh")
                if not os.path.isfile(path):
                    return self.error(404, "Not found.")
                with open(path, encoding="utf-8") as f:
                    script = f.read()
                addr = public_addr(self, public)
                host = (self.headers.get("Host") or "").strip()
                # The source the way this script came: over HTTPS when it did.
                scheme = "https" if self.secure else "http"
                source = f"{scheme}://{host}/download/xenithos-source.tar.gz" if PUBLIC_ADDR_RE.match(host) or re.match(r"^[A-Za-z0-9.-]{1,64}$", host) else ""
                script = script.replace("@XENITHOS_SERVER@", addr).replace("@XENITHOS_SOURCE@", source)
                return self.reply(200, script.encode(), "text/plain; charset=utf-8", "no-store")

            if parts == ["v1", "inbox"]:
                if console_key and not hmac.compare_digest(
                        q.get("key", [""])[0].encode(), console_key.encode()):
                    return self.error(401, "This console isn't allowed to read the inbox.")
                return self.reply(200, "".join(f"{m['id']}|{m['t']}|{m['text']}\n"
                                               for m in state.data["inbox"]))

            if parts == ["v1", "store"]:
                return self.signed("store", store_rows(store.items()))

            if parts == ["v1", "store", "all"]:
                return self.signed(store_sig_name(parts, q), store_bundle(store, q.get("pc", [""])[0] == "1"),
                                   "application/octet-stream", "no-store")

            if parts == ["v1", "store", "flathub"]:
                # Their own routes: consoles that don't know these kinds never see them.
                return self.signed("flathub", flathub_rows(store.flathub()))

            if parts == ["v1", "store", "steam"]:
                return self.signed("steam", steam_rows(store.steam()))

            if parts == ["v1", "store", "needs"]:
                # Its own route: a 9th field on /v1/store would end up in older consoles' descriptions.
                return self.signed("needs", needs_rows(store.needs()))

            if len(parts) == 4 and parts[:3] == ["v1", "store", "file"]:
                item = store.find(parts[3])
                if not item:
                    return self.error(404, "No such item.")
                return self.send_file(item["path"], "application/octet-stream", "no-store")

            if len(parts) == 4 and parts[:3] == ["v1", "store", "icon"] and "." in parts[3]:
                iid, ext = parts[3].rsplit(".", 1)
                path = store.icon(iid, ext) if ext in ("ximg", "png") else None
                if not path:
                    return self.error(404, "No thumbnail.")
                with open(path, "rb") as f:
                    return self.signed(store_sig_name(parts, q), f.read(),
                                       "image/png" if ext == "png" else "application/octet-stream", "public, max-age=300")

            if len(parts) == 3 and parts[:2] == ["v1", "scores"] and parts[2] in GAMES:
                rows = []
                for n, e in enumerate(state.top(parts[2])[:10], 1):
                    acc = state.account(e["name"])
                    rows.append(f"{n}|{e['name']}|{e['score']}|{acc['avatar'] if acc else ''}\n")
                return self.reply(200, "".join(rows))

            if parts == ["v1", "players"]:
                accs = sorted(state.data["accounts"].values(), key=lambda a: -a.get("last_seen", 0))[:100]
                return self.reply(200, "".join(
                    f"{a['name']}|{a['avatar']}|{a['joined']}|{state.best_of('volt', a['name'])[0]}\n"
                    for a in accs))

            if len(parts) == 3 and parts[:2] == ["v1", "players"]:
                acc = state.account(parts[2])
                if not acc:
                    return self.error(404, "No such player.")
                best, rank = state.best_of("volt", acc["name"])
                return self.reply(200, kv({"name": acc["name"], "avatar": acc["avatar"], "joined": acc["joined"],
                                           "volt_best": best, "volt_rank": rank}))

            if len(parts) == 2 and parts[0] == "static" and STATIC_RE.match(parts[1]):
                path = os.path.join(web_dir, "static", parts[1])
                if os.path.isfile(path):
                    return self.send_file(path, CTYPES[parts[1].rsplit(".", 1)[1]])
                return self.error(404, "Not found.")

            if parts == [] or parts == ["index.html"]:
                page = os.path.join(web_dir, "index.html")
                if os.path.isfile(page):
                    return self.send_file(page, "text/html; charset=utf-8", "no-cache")
                return self.reply(200, "XenithOS Cloud " + VERSION + "\n")

            return self.error(404, "Not found.")

        # -------------------------------------------------------------- POST
        def do_POST(self):
            parts = [p for p in urlparse(self.path).path.split("/") if p]
            body = self.body()
            if body is None:
                return self.error(413, "Too large.")
            form = {k: v[0] for k, v in parse_qs(body, keep_blank_values=True).items()}
            ip = self.client_address[0]
            limit_key = rate_key(ip)

            if parts == ["v1", "accounts", "create"]:
                name, pin, avatar = form.get("name", "").strip(), form.get("pin", ""), form.get("avatar", "")
                problem = check_gamertag(name)
                if problem:
                    return self.error(400, problem)
                if not PIN_RE.match(pin):
                    return self.error(400, "Passcodes are 4 to 8 digits.")
                if not valid_avatar(avatar):
                    return self.error(400, "That avatar code isn't valid.")
                if not creates.allow(limit_key):
                    return self.error(429, "Too many new profiles from this network. Try again later.")
                result, err = state.create(name, pin, avatar)
                if err:
                    return self.error(409, err)
                return self.session_reply(*result)

            if parts == ["v1", "accounts", "login"]:
                if not logins.allow(limit_key):
                    return self.error(429, "Too many sign-in attempts. Wait a few minutes.")
                result, err = state.login(form.get("name", "").strip(), form.get("pin", ""))
                if err:
                    return self.error(429 if "15 minutes" in err else 401, err)
                return self.session_reply(*result)

            if parts == ["v1", "feedback"]:
                kind, text = form.get("kind", ""), clean(form.get("text", ""), 1000)
                if kind not in FEEDBACK_KINDS:
                    return self.error(400, "Say whether it's a bug or an idea.")
                if len(text) < 3:
                    return self.error(400, "Write a little more.")
                acc = state.by_token(form.get("token", ""))
                if not feedback_by_ip.allow(limit_key) or (acc and not feedback_by_name.allow(acc["name"])):
                    return self.error(429, "That's a lot of feedback at once. Try again in a while.")
                who = acc["name"] if acc else "Guest"
                version, platform = clean(form.get("version", ""), 16) or "?", clean(form.get("platform", ""), 32) or "?"
                state.add_feedback(kind, text, who, version, platform)
                hook = feedback_hooks(os.path.dirname(state.path)).get(kind)
                if hook:
                    threading.Thread(target=send_feedback, args=(hook, kind, text, who, version, platform),
                                     daemon=True).start()
                return self.reply(200, "ok=1\n")

            if parts in (["v1", "tube", "search"], ["v1", "tube", "start"]):
                if not tube or not tube.available():
                    return self.error(503, "Tube isn't available on this server.")
                acc = state.by_token(form.get("token", ""))
                if not acc:
                    return self.error(401, "Sign in to a XenithOS profile to use Tube.")
                if parts[2] == "search":
                    query = clean(form.get("q", ""), 80)
                    if not query:
                        return self.error(400, "Type something to search for.")
                    if not tube.searches.allow(acc["name"]):
                        return self.error(429, "That's a lot of searching. Wait a minute.")
                    try:
                        rows = tube.search(query)
                    except RuntimeError as e:
                        return self.error(503, str(e))
                    except (subprocess.TimeoutExpired, ValueError, OSError):
                        return self.error(502, "YouTube didn't answer. Try again.")
                    return self.reply(200, "".join(f"{v}|{t}|{c}|{d}\n" for v, t, c, d in rows))
                vid = form.get("id", "")
                if not TUBE_ID_RE.match(vid):
                    return self.error(400, "That isn't a video.")
                try:
                    at = max(0, min(int(form.get("t", "0")), 24 * 3600))
                except ValueError:
                    at = 0
                if not tube.starts.allow(acc["name"]):
                    return self.error(429, "Too many videos at once. Wait a few minutes.")
                try:
                    sid, title, duration = tube.start(vid, at, acc["name"])
                except RuntimeError as e:
                    return self.error(503, str(e))
                return self.reply(200, kv({"session": sid, "title": title, "duration": duration, "fps": TUBE_FPS,
                                           "rate": TUBE_RATE, "width": TUBE_W, "height": TUBE_H}))

            if parts[:2] == ["v1", "accounts"] and len(parts) == 3 and parts[2] in ("me", "avatar", "logout"):
                acc = state.by_token(form.get("token", ""))
                if not acc:
                    return self.error(401, "Your sign-in expired. Sign in again.")
                if parts[2] == "me":
                    state.seen(acc["name"], ip)
                    best, rank = state.best_of("volt", acc["name"])
                    return self.reply(200, kv({"name": acc["name"], "avatar": acc["avatar"], "joined": acc["joined"],
                                               "volt_best": best, "volt_rank": rank}))
                if parts[2] == "avatar":
                    if not valid_avatar(form.get("avatar", "")):
                        return self.error(400, "That avatar code isn't valid.")
                    state.set_avatar(acc, form["avatar"])
                    return self.reply(200, "ok=1\n")
                state.logout(form["token"])
                return self.reply(200, "ok=1\n")

            if len(parts) == 3 and parts[:2] == ["v1", "scores"] and parts[2] in GAMES:
                try:
                    score = int(form.get("score", "-1"))
                except ValueError:
                    score = -1
                if not 0 <= score <= MAX_SCORE:
                    return self.error(400, "That score isn't valid.")
                if form.get("token"):
                    acc = state.by_token(form["token"])
                    if not acc:
                        return self.error(401, "Your sign-in expired. Sign in again.")
                    name = acc["name"]
                else:
                    name = form.get("name", "").strip().upper()
                    if not LEGACY_NAME_RE.match(name):
                        return self.error(400, "That name isn't valid.")
                    if state.account(name):
                        return self.error(409, "That name belongs to a profile. Sign in to use it.")
                with last_score_lock:
                    now = time.monotonic()
                    if now - last_score.get(limit_key, -1e9) < SCORE_COOLDOWN_S:
                        return self.error(429, "Slow down a little.")
                    last_score[limit_key] = now
                rank, entry = state.submit(parts[2], name, score)
                return self.reply(200, f"{rank or 0}|{entry['name']}|{entry['score']}\n")

            if parts[:2] in (["v1", "inbox"], ["v1", "motd"], ["v1", "news"]):
                if not self.is_admin():
                    return self.error(401, "Admin key required.")
                if parts in (["v1", "inbox"], ["v1", "news"]):
                    msg = state.add_message(body)
                    return self.reply(200, f"{msg['id']}\n") if msg else self.error(400, "Empty message.")
                if parts == ["v1", "inbox", "clear"]:
                    state.clear_inbox()
                    return self.reply(200, "cleared\n")
                if parts == ["v1", "motd"]:
                    state.set_motd(body)
                    return self.reply(200, "ok\n")

            return self.error(404, "Not found.")

    return Handler


# A relay (server/xenith-relay.py) passes consoles on with this first: "PROXY TCP4 <console>
# <relay> <console port> <relay port>" (PROXY protocol version 1).
PROXY_LINE = re.compile(rb"^PROXY (TCP4|TCP6) (\S{2,45}) (\S{2,45}) (\d{1,5}) (\d{1,5})\r\n$")


def proxied(handler):
    """handler, for connections from a relay (XENITH_PROXY_PORT): each starts with a PROXY
    line, and the console address in it is used as client_address (logs, rate limits, who's
    online). A connection without a good one is closed unanswered."""
    class Proxied(handler):
        timeout = 300                         # a relay connection that stalls doesn't keep its thread

        def setup(self):
            super().setup()
            self.proxy_ok = False
            try:
                line = self.rfile.readline(108)
            except OSError:
                return
            m = PROXY_LINE.match(line)
            if not m:
                return
            try:
                ip = ipaddress.ip_address(m.group(2).decode("ascii"))
            except ValueError:
                return
            if (m.group(1) == b"TCP4") != (ip.version == 4) or not 0 < int(m.group(4)) < 65536:
                return
            self.client_address = (str(ip), int(m.group(4)))
            self.proxy_ok = True

        def handle(self):
            if self.proxy_ok:
                super().handle()

    return Proxied


def edge(handler):
    """handler, for requests from Cloudflare's tunnel (XENITH_EDGE_PORT), which came over HTTPS.
    Cloudflare names the console's own address in CF-Connecting-IP, and that's used as
    client_address (logs, rate limits, who's online); every request through the tunnel would
    otherwise come from cloudflared's one address. One without a good address gets no answer."""
    class Edge(handler):
        secure = True
        timeout = 300                         # a connection that stalls doesn't keep its thread

        def parse_request(self):
            if not super().parse_request():
                return False
            try:
                ip = ipaddress.ip_address((self.headers.get("CF-Connecting-IP") or "").strip())
            except ValueError:
                return False
            self.client_address = (str(ip), self.client_address[1])
            return True

    return Edge


def serve():
    port = int(os.environ.get("XENITH_PORT", "8795"))
    proxy_port = int(os.environ.get("XENITH_PROXY_PORT", "0") or 0)
    edge_port = int(os.environ.get("XENITH_EDGE_PORT", "0") or 0)
    state = State(os.environ.get("XENITH_STATE_DIR", "state"))
    store = Store(os.environ.get("XENITH_STORE_DIR", "store"))
    admin_key = os.environ.get("XENITH_ADMIN_KEY", "")
    console_key = os.environ.get("XENITH_CONSOLE_KEY", "")
    web_dir = os.environ.get("XENITH_WEB_DIR", "web")
    deno = os.environ.get("XENITH_DENO", "")
    if deno and not os.access(deno, os.X_OK):
        log(f"XENITH_DENO is set but {deno} can't run; Tube asks YouTube the quick way only")
        deno = ""
    plugins = os.environ.get("XENITH_YTDLP_PLUGINS", "")
    tube = Tube(os.environ.get("XENITH_YTDLP") or shutil.which("yt-dlp") or "",
                os.environ.get("XENITH_FFMPEG") or shutil.which("ffmpeg") or "",
                int(os.environ.get("XENITH_TUBE_STREAMS", "3")), deno, plugins if os.path.isdir(plugins) else "")
    public = os.environ.get("XENITH_PUBLIC_ADDR", "")
    downloads = os.environ.get("XENITH_DOWNLOAD_DIR", "downloads")
    lan = os.environ.get("XENITH_LAN_ADDR", "").strip()
    if lan and not lan_addr_ok(lan):
        log(f"XENITH_LAN_ADDR must be a private a.b.c.d:port, not {lan!r}; consoles won't be told one")
        lan = ""
    https = os.environ.get("XENITH_PUBLIC_HTTPS", "").strip()
    if https and not https_addr_ok(https):
        log(f"XENITH_PUBLIC_HTTPS must be a name or name:port, not {https!r}; PCs won't be told one")
        https = ""
    key_file = os.environ.get("XENITH_STORE_KEY", "")
    if key_file:
        store.key = load_store_key(key_file, [store.dir, web_dir, downloads, os.path.dirname(state.path)])
        if store.key:
            log(f"Store lists go out signed with key {store.key.key_id}")
    else:
        log("XENITH_STORE_KEY isn't set: Store lists go out unsigned, and consoles that check refuse them")
    with state.lock:                          # tells this server apart from another at that address
        if not state.data.get("server_id"):
            state.data["server_id"] = secrets.token_hex(8)
            state.save()
        server_id = state.data["server_id"]

    def reaper():
        while True:
            time.sleep(30)
            tube.reap()
    threading.Thread(target=reaper, daemon=True).start()

    handler = make_handler(state, store, admin_key, web_dir, console_key, tube, public, downloads, lan, server_id)
    httpd = ThreadingHTTPServer(("0.0.0.0", port), handler)
    handler.https = https
    httpd.daemon_threads = True
    if proxy_port:
        relayed = ThreadingHTTPServer(("0.0.0.0", proxy_port), proxied(handler))
        relayed.daemon_threads = True
        threading.Thread(target=relayed.serve_forever, daemon=True).start()
    if edge_port:
        edged = ThreadingHTTPServer(("0.0.0.0", edge_port), edge(handler))
        edged.daemon_threads = True
        threading.Thread(target=edged.serve_forever, daemon=True).start()
        print(f"Cloudflare's tunnel on :{edge_port}", flush=True)
    if https:
        print(f"PCs and Raspberry Pis are told to use https://{https}", flush=True)
    tube_mode = ("on, Deno and PO tokens when needed" if tube.deno and tube.plugins else
                 "on, Deno when needed" if tube.deno else "on") if tube.available() else "off"
    print(f"XenithOS Cloud {VERSION} on :{port}{f' (relay on :{proxy_port})' if proxy_port else ''} "
          f"(admin routes {'on' if admin_key else 'off'}, Tube {tube_mode}"
          f"{', consoles on its network use ' + lan if lan else ''})", flush=True)
    httpd.serve_forever()


# ---------------------------------------------------------------------- selftest


def selftest():
    import tempfile
    import urllib.error
    import urllib.request

    global PIN_ITERATIONS
    PIN_ITERATIONS = 2000                          # keep the test quick

    tmp = tempfile.mkdtemp()
    store_dir = os.path.join(tmp, "store")
    os.makedirs(store_dir)
    with open(os.path.join(store_dir, "game.elf32"), "wb") as f:
        f.write(b"\x7fELF" + bytes(range(256)) * 10)
    with open(os.path.join(store_dir, "game.ximg"), "wb") as f:
        f.write(b"XIMGthumb")
    with open(os.path.join(store_dir, "gamedata.wad"), "wb") as f:
        f.write(b"IWAD" + bytes(range(256)) * 40)
    with open(os.path.join(store_dir, "xenithos-pc.tar.gz"), "wb") as f:
        f.write(b"\x1f\x8b" + bytes(range(256)) * 4)
    store_list = [{"id": "game", "kind": "app", "name": "Game|X", "version": "1.0", "file": "game.elf32",
                   "description": "line\nbreak", "needs": ["gamedata", "missing", "../bad", "game"]},
                  {"id": "gamedata", "kind": "data", "name": "Game data", "version": "1", "file": "gamedata.wad"},
                  {"id": "xenithos-pc", "kind": "pc", "name": "XenithOS update", "version": "9.9",
                   "file": "xenithos-pc.tar.gz"},
                  {"id": "../evil", "kind": "app", "file": "game.elf32"},
                  {"id": "missing", "kind": "app", "file": "nope.bin"},
                  {"id": "weird", "kind": "exe", "file": "game.elf32"}]
    with open(os.path.join(store_dir, "store.json"), "w") as f:
        json.dump(store_list, f)
    flathub_list = [{"id": "kodi", "name": "Kodi|TV", "ref": "tv.kodi.Kodi", "arches": ["aarch64", "x86_64", "riscv"],
                     "mb": 285, "shared_mb": "690", "args": "-gamepadui --command=sh ok;rm", "description": "Media\ncenter"},
                    {"id": "kodi", "name": "Again", "ref": "tv.kodi.Kodi", "arches": ["x86_64"]},
                    {"id": "badref", "ref": "--command=sh", "arches": ["x86_64"]},
                    {"id": "tworef", "ref": "org.example", "arches": ["x86_64"]},
                    {"id": "noarch", "ref": "org.example.App", "arches": ["sparc"]},
                    {"id": "steam-1", "ref": "org.example.App", "arches": ["x86_64"]},
                    {"id": "../x", "ref": "org.example.App", "arches": ["x86_64"]},
                    "not a dict"]
    with open(os.path.join(store_dir, "flathub.json"), "w") as f:
        json.dump(flathub_list, f)
    with open(os.path.join(store_dir, "kodi.ximg"), "wb") as f:
        f.write(b"XIMGkodi")
    steam_list = [{"id": "cs2", "name": "Counter-Strike 2", "appid": 730, "free": True, "description": "Free"},
                  {"id": "portal2", "name": "Portal 2", "appid": "620", "free": False, "description": "Paid"},
                  {"id": "bad", "appid": -5}, {"id": "Bad Id", "appid": 10}, {"id": "cs2", "appid": 731}]
    with open(os.path.join(store_dir, "steam.json"), "w") as f:
        json.dump(steam_list, f)
    web_dir = os.path.join(tmp, "web")
    os.makedirs(os.path.join(web_dir, "static"))
    with open(os.path.join(web_dir, "index.html"), "w") as f:
        f.write("<!doctype html><title>XenithOS</title>")
    with open(os.path.join(web_dir, "static", "app.js"), "w") as f:
        f.write("console.log(1)")

    dl_dir = os.path.join(tmp, "downloads")
    os.makedirs(dl_dir)
    # Stand-ins for yt-dlp and ffmpeg. The yt-dlp one notes every call, and a few video ids
    # act out what YouTube does: refuse the quick ask, refuse everything, private, slow.
    fake_ytdlp, fake_ffmpeg = os.path.join(tmp, "yt-dlp"), os.path.join(tmp, "ffmpeg")
    with open(fake_ytdlp, "w") as f:
        f.write(r'''#!/usr/bin/env python3
import json, os, sys, time
a = sys.argv[1:]
with open(os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "calls.txt"), "a") as calls:
    calls.write(" ".join(a) + "\n")
if "--flat-playlist" in a:
    print(json.dumps({"entries": [{"id": "abcdefghijk", "title": "Test|video\nOne", "channel": "Chan",
                                   "duration": 61}, {"id": "bad"}]}))
    sys.exit()
vid, full = a[-1][-11:], "--js-runtimes" in a
if vid == "slowvideo11":
    time.sleep(20)
if vid == "privatevid1":
    print("ERROR: [youtube] privatevid1: Private video. Sign in if you've been granted access", file=sys.stderr)
    sys.exit(1)
if vid == "blockedall1" or (vid == "quickfails1" and not full):
    print("ERROR: [youtube] " + vid + ": Sign in to confirm you're not a bot", file=sys.stderr)
    sys.exit(1)
url = "http://example.invalid/" + ("empty" if vid == "emptystream" else "v") + "?expire=%d" % (time.time() + 7200)
print(json.dumps({"title": "Test video", "duration": 61, "url": url, "vcodec": "avc1", "acodec": "mp4a"}))
''')
    with open(fake_ffmpeg, "w") as f:
        f.write(r'''#!/usr/bin/env python3
import sys
if any("/empty" in arg for arg in sys.argv):
    print("http://example.invalid/empty: Server returned 403 Forbidden (access denied)", file=sys.stderr)
else:
    sys.stdout.buffer.write(b"RIFF\0\0\0\0AVI LIST\0\0\0\0movi00dc\4\0\0\0\xff\xd8\xff\xd9")
''')
    os.chmod(fake_ytdlp, 0o755)
    os.chmod(fake_ffmpeg, 0o755)

    state = State(os.path.join(tmp, "state"))
    store = Store(store_dir)
    tube = Tube(fake_ytdlp, fake_ffmpeg, 3, deno=fake_ytdlp, plugins=tmp)   # the stand-in never runs "Deno"
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(state, store, "k3y", web_dir, "c0nsole",
                                                               tube, "", dl_dir, "192.168.1.20:8795", "5e4e1d"))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{httpd.server_address[1]}"

    def req(method, path, body=None, key=None):
        r = urllib.request.Request(base + path, data=body.encode() if body is not None else None, method=method)
        if key:
            r.add_header("X-Xenith-Key", key)
        try:
            with urllib.request.urlopen(r) as resp:
                return resp.status, resp.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    def kvparse(b):
        return dict(line.split("=", 1) for line in b.decode().splitlines() if "=" in line)

    checks = []

    def check(name, cond):
        checks.append((name, bool(cond)))

    # Basics
    check("ping", req("GET", "/v1/ping") == (200, b"pong\n"))
    check("website served", req("GET", "/") == (200, b"<!doctype html><title>XenithOS</title>"))
    check("static served", req("GET", "/static/app.js") == (200, b"console.log(1)"))
    check("static traversal refused", req("GET", "/static/..%2Fstate%2Fstate.json")[0] == 404)
    check("static unknown type refused", req("GET", "/static/index.html")[0] == 404)
    code, body = req("GET", "/v1/status")
    check("status ok", code == 200 and b"uptime=" in body and b"players=" in body)
    check("status does not log a console", state.data["consoles"] == {})
    code, body = req("GET", "/v1/hello?name=OWEN&ver=0.4")
    check("hello ok", code == 200 and b"motd=WELCOME TO XENITHOS CLOUD" in body)
    check("hello records console", "OWEN" in state.data["consoles"])

    # Store
    code, body = req("GET", "/v1/store")
    lines = body.decode().strip().split("\n")
    rows = {line.split("|")[0]: line.split("|") for line in lines}
    f = rows.get("game", [])
    with open(os.path.join(store_dir, "game.elf32"), "rb") as fh:
        payload = fh.read()
    check("store lists only valid items", code == 200 and sorted(rows) == ["game", "gamedata", "xenithos-pc"])
    check("store lists the PC update as kind pc", rows.get("xenithos-pc", ["", ""])[1] == "pc")
    check("store lines have 8 fields", all(len(r) == 8 for r in rows.values()))
    check("store needs: only data that's in the Store", req("GET", "/v1/store/needs") == (200, b"game|gamedata\n"))
    check("store pipe cleaned from name", f[2] == "Game X")
    check("store sha256 matches", f[5] == hashlib.sha256(payload).hexdigest())
    check("store description one line", f[7] == "line break")
    check("download bytes", req("GET", "/v1/store/file/game") == (200, payload))
    check("download traversal 404", req("GET", "/v1/store/file/..%2Fevil")[0] == 404)
    check("icon served", req("GET", "/v1/store/icon/game.ximg") == (200, b"XIMGthumb"))
    check("icon missing 404", req("GET", "/v1/store/icon/game.png")[0] == 404)
    check("icon for unknown item 404", req("GET", "/v1/store/icon/nope.ximg")[0] == 404)
    code, body = req("GET", "/v1/store/flathub")
    check("flathub lists only valid apps",
          code == 200 and body == b"kodi|Kodi TV|tv.kodi.Kodi|x86_64,aarch64|285|690|-gamepadui|Media center\n")
    check("flathub kept out of /v1/store", b"kodi" not in req("GET", "/v1/store")[1])
    check("flathub icon served", req("GET", "/v1/store/icon/kodi.ximg") == (200, b"XIMGkodi"))
    check("steam lists only valid games",
          req("GET", "/v1/store/steam") == (200, b"cs2|Counter-Strike 2|730|1|Free\nportal2|Portal 2|620|0|Paid\n"))

    def unbundle(b):
        if not b.startswith(b"XSTORE 1\n"):
            return None
        pos, out = 9, {}
        while True:
            nl = b.index(b"\n", pos)
            name, n = b[pos:nl].decode().rsplit(" ", 1)
            pos = nl + 1
            if name == "end":
                return out if pos == len(b) else None
            out[name] = b[pos:pos + int(n)]
            pos += int(n)

    code, body = req("GET", "/v1/store/all?pc=1")
    pc = unbundle(body) if code == 200 else None
    check("store bundle: the same lists as their own routes", pc is not None
          and pc.get("store") == req("GET", "/v1/store")[1] and pc.get("needs") == req("GET", "/v1/store/needs")[1]
          and pc.get("flathub") == req("GET", "/v1/store/flathub")[1]
          and pc.get("steam") == req("GET", "/v1/store/steam")[1])
    check("store bundle: thumbnails for listed items only",
          pc is not None and pc.get("icon:game") == b"XIMGthumb" and pc.get("icon:kodi") == b"XIMGkodi"
          and not any(k.startswith("icon:") and k[5:] not in ("game", "gamedata", "xenithos-pc", "kodi", "cs2", "portal2")
                      for k in pc))
    xbox = unbundle(req("GET", "/v1/store/all")[1])
    check("store bundle for the Xbox: no Flathub or Steam",
          xbox is not None and "flathub" not in xbox and "steam" not in xbox and "icon:kodi" not in xbox
          and xbox.get("icon:game") == b"XIMGthumb")

    # Store signatures: ed25519 as RFC 8032 has it (its first three test vectors), then the header
    for seed_hex, pub_hex, msg_hex, sig_hex in (
            ("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
             "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
             "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e0652249015"
             "55fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
            ("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
             "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
             "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
             "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"),
            ("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
             "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
             "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
             "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a")):
        seed, msg = bytes.fromhex(seed_hex), bytes.fromhex(msg_hex)
        check(f"ed25519: RFC 8032 test {pub_hex[:6]}: public key, signature and check",
              ed25519_public(seed).hex() == pub_hex and ed25519_sign(seed, msg).hex() == sig_hex
              and ed25519_verify(bytes.fromhex(pub_hex), msg, bytes.fromhex(sig_hex)))
    seed = bytes(range(32))
    pub, text = ed25519_public(seed), store_sig_text("store", b"a|b\n")
    sig = ed25519_sign(seed, text)
    check("ed25519: refuses a changed text, R, S, S plus L, or another key",
          ed25519_verify(pub, text, sig) and not ed25519_verify(pub, text + b"x", sig)
          and not ed25519_verify(pub, text, bytes([sig[0] ^ 1]) + sig[1:])
          and not ed25519_verify(pub, text, sig[:40] + bytes([sig[40] ^ 4]) + sig[41:])
          and not ed25519_verify(pub, text, sig[:32] + (int.from_bytes(sig[32:], "little") + ED_L).to_bytes(32, "little"))
          and not ed25519_verify(ed25519_public(bytes(32)), text, sig) and not ed25519_verify(pub, text, bytes(64)))

    def sig_get(path):
        try:
            with urllib.request.urlopen(base + path) as resp:
                return resp.status, resp.read(), resp.headers.get_all(STORE_SIG_HEADER) or []
        except urllib.error.HTTPError as e:
            return e.code, e.read(), e.headers.get_all(STORE_SIG_HEADER) or []

    unsigned = sig_get("/v1/store")
    check("store signing off: no signature, status says so",
          unsigned[2] == [] and kvparse(req("GET", "/v1/status")[1]).get("store_key") == "none")
    store.key = StoreKey(seed)
    keys = [pub.hex()]
    signed_routes = {"/v1/store": "store", "/v1/store/needs": "needs", "/v1/store/flathub": "flathub",
                     "/v1/store/steam": "steam", "/v1/store/all": "all", "/v1/store/all?pc=1": "all pc",
                     "/v1/store/icon/game.ximg": "icon game.ximg", "/v1/store/icon/kodi.ximg": "icon kodi.ximg"}
    got = {route: sig_get(route) for route in signed_routes}
    check("store signing: every list, the bundle and thumbnails signed, bodies as before",
          all(code == 200 and len(values) == 1 and store_sig_good(values, signed_routes[route], body, keys)
              for route, (code, body, values) in got.items())
          and got["/v1/store"][1] == unsigned[1] and got["/v1/store/icon/game.ximg"][1] == b"XIMGthumb")
    check("store signing: the header is 1, the key id and 128 hex digits",
          re.fullmatch(r"1 " + pub.hex()[:16] + r" [0-9a-f]{128}", got["/v1/store"][2][0]) is not None)
    check("store signing: the files themselves aren't signed (their SHA-256 is)",
          sig_get("/v1/store/file/game")[2] == [] and sig_get("/v1/store/file/game")[0] == 200)
    store_code, store_body, store_sig = got["/v1/store"]
    check("store signing: a changed list fails", not store_sig_good(store_sig, "store", store_body.replace(b"Game X", b"Game Y"), keys)
          and not store_sig_good(store_sig, "store", store_body + b"evil|app|Evil|1|9|" + b"0" * 64 + b"|e|x\n", keys))
    check("store signing: one reply can't stand in for another",
          not store_sig_good(store_sig, "needs", store_body, keys)
          and not store_sig_good(got["/v1/store/all"][2], "all pc", got["/v1/store/all"][1], keys)
          and not store_sig_good(got["/v1/store/icon/game.ximg"][2], "icon kodi.ximg", b"XIMGthumb", keys))
    check("store signing: another key's console refuses it, and a missing header is refused",
          not store_sig_good(store_sig, "store", store_body, [ed25519_public(bytes(32)).hex()])
          and not store_sig_good([], "store", store_body, keys)
          and not store_sig_good(["1 " + pub.hex()[:16] + " " + "0" * 128], "store", store_body, keys))
    check("store signing: two keys at once, each console finds its own",
          store_sig_good(["1 0123456789abcdef " + "ab" * 64] + store_sig, "store", store_body,
                         [ed25519_public(bytes(32)).hex(), pub.hex()]))
    check("store signing: the same list, the same signature (made once)",
          sig_get("/v1/store")[2] == store_sig and kvparse(req("GET", "/v1/status")[1]).get("store_key") == pub.hex()[:16])
    import contextlib
    with contextlib.redirect_stdout(io.StringIO()) as said:
        addr = f"127.0.0.1:{httpd.server_address[1]}"
        checked = store_key_command(["check", addr, pub.hex()]) == 0 and store_key_command(["check", addr, "ab" * 32]) == 1
        as_url = store_key_command(["check", f"http://{addr}/", pub.hex()]) == 0
    check("store-key check: passes a signed server, fails with another key, takes a URL too",
          checked and as_url and said.getvalue().count("PASS /v1/store") == 14 and "FAIL /v1/store:" in said.getvalue())

    # The key file: made private, never overwritten, kept out of what the server serves
    key_dir = os.path.join(tmp, "keys")
    os.makedirs(key_dir)
    key_file = os.path.join(key_dir, "store.key")
    with contextlib.redirect_stdout(io.StringIO()) as said:
        made = store_key_command(["new", key_file]) == 0
        public_line = said.getvalue()
    with contextlib.redirect_stderr(io.StringIO()):
        again = store_key_command(["new", key_file])
    file_pub = ed25519_public(read_store_key(key_file)).hex()
    check("store-key new: a key only its owner can read, its public half printed, never overwritten",
          made and again == 1 and os.stat(key_file).st_mode & 0o777 == 0o600 and f'"{file_pub}",' in public_line)
    with contextlib.redirect_stderr(io.StringIO()) as said:
        loaded = load_store_key(key_file, [store_dir, web_dir, dl_dir, os.path.join(tmp, "state")])
        shutil.copy(key_file, os.path.join(store_dir, "store.key"))
        in_store = load_store_key(os.path.join(store_dir, "store.key"), [store_dir, web_dir])
        shutil.copy(key_file, os.path.join(tmp, "state", "k"))
        in_state = load_store_key(os.path.join(tmp, "state", "k"), [store_dir, os.path.join(tmp, "state")])
        with open(os.path.join(key_dir, "bad.key"), "w") as f:
            f.write("# a comment\nnot a key\n")
        bad_key = load_store_key(os.path.join(key_dir, "bad.key"), [store_dir])
        missing = load_store_key(os.path.join(key_dir, "nope.key"), [store_dir])
    os.remove(os.path.join(store_dir, "store.key"))
    check("XENITH_STORE_KEY: loads from its own folder, refused in the store or state folder, or broken",
          loaded is not None and loaded.public.hex() == file_pub and in_store is None and in_state is None
          and bad_key is None and missing is None and said.getvalue().count("unsigned") == 4)
    header_file = os.path.join(key_dir, "store_key.h")
    with open(header_file, "w") as f:
        f.write(f'/* "{"0" * 64}" in a comment doesn\'t count */\nstatic const char *const store_keys[] = {{\n'
                f'    "{file_pub}",\n    // "{"1" * 64}",\n    NULL\n}};\n')
    check("store_key.h: the keys a console build trusts", store_keys_in(header_file) == [file_pub])
    store.key = None
    code, body = req("GET", "/v1/lan")
    check("lan: this server's id and its home address", code == 200 and kvparse(body) == {"id": "5e4e1d", "lan": "192.168.1.20:8795"})
    check("lan address must be private and numeric",
          lan_addr_ok("192.168.50.145:8795") and lan_addr_ok("10.0.0.2:80") and not lan_addr_ok("37.1.2.3:8795")
          and not lan_addr_ok("keystone:8795") and not lan_addr_ok("192.168.1.2") and not lan_addr_ok("192.168.1.2:0"))
    httpd.RequestHandlerClass.https = "cloud.example.org"         # XENITH_PUBLIC_HTTPS
    code, body = req("GET", "/v1/lan")
    check("lan: and the HTTPS address, when there is one", code == 200 and kvparse(body) ==
          {"id": "5e4e1d", "lan": "192.168.1.20:8795", "https": "cloud.example.org"})
    check("https address: a name, or name:port",
          https_addr_ok("cloud.example.org") and https_addr_ok("localhost:8443") and not https_addr_ok("https://x.org")
          and not https_addr_ok("x.org/path") and not https_addr_ok("-x.org") and not https_addr_ok("x.org:0")
          and not https_addr_ok("x.org:99999") and not https_addr_ok(""))
    check("rate limits count an IPv6 /64 as one address",
          rate_key("2001:db8:1:2::5") == rate_key("2001:db8:1:2:ffff::9") != rate_key("2001:db8:1:3::5")
          and rate_key("203.0.113.7") == "203.0.113.7" and rate_key("::ffff:203.0.113.7") == "::ffff:203.0.113.7")

    # Accounts
    code, body = req("POST", "/v1/accounts/create", "name=Owen&pin=1234&avatar=1202001000")
    acc = kvparse(body)
    token = acc.get("token", "")
    check("create account", code == 200 and acc.get("name") == "Owen" and len(token) == 48)
    check("token stored only as a hash", token not in json.dumps(state.data))
    check("pin stored only as a hash", "1234" not in json.dumps(state.data["accounts"]))
    check("duplicate name refused (any case)", req("POST", "/v1/accounts/create", "name=OWEN&pin=1234&avatar=1202001000")[0] == 409)
    for bad in ("name=ab&pin=1234&avatar=1202001000", "name=1abc&pin=1234&avatar=1202001000",
                "name=Big%20%20Owen&pin=1234&avatar=1202001000", "name=Admin&pin=1234&avatar=1202001000",
                "name=Owen2&pin=12&avatar=1202001000", "name=Owen2&pin=abcd&avatar=1202001000",
                "name=Owen2&pin=1234&avatar=1zz2001000", "name=Owen%3Cb%3E&pin=1234&avatar=1202001000"):
        check(f"create refused: {bad[:28]}", req("POST", "/v1/accounts/create", bad)[0] == 400)
    code, body = req("POST", "/v1/accounts/login", "name=owen&pin=1234")
    check("login (any case)", code == 200 and kvparse(body).get("name") == "Owen")
    check("wrong pin refused", req("POST", "/v1/accounts/login", "name=Owen&pin=9999")[0] == 401)
    check("unknown name refused", req("POST", "/v1/accounts/login", "name=Nobody&pin=1234")[0] == 401)
    code, body = req("POST", "/v1/accounts/me", f"token={token}")
    me = kvparse(body)
    check("me", code == 200 and me.get("name") == "Owen" and me.get("avatar") == "1202001000")
    check("me with bad token", req("POST", "/v1/accounts/me", "token=nope")[0] == 401)
    check("avatar update", req("POST", "/v1/accounts/avatar", f"token={token}&avatar=1333333333")[0] == 200
          and state.account("owen")["avatar"] == "1333333333")
    check("bad avatar refused", req("POST", "/v1/accounts/avatar", f"token={token}&avatar=1999999999")[0] == 400)

    # Scores
    check("score with token", req("POST", "/v1/scores/volt", f"token={token}&score=120")[1] == b"1|Owen|120\n")
    check("score cooldown", req("POST", "/v1/scores/volt", f"token={token}&score=130")[0] == 429)
    state.submit("volt", "ZED", 300)
    state.submit("volt", "Owen", 50)
    code, body = req("GET", "/v1/scores/volt")
    check("scores ordered with avatars", body == b"1|ZED|300|\n2|Owen|120|1333333333\n")
    code, body = req("GET", "/v1/players")
    check("players list", body.decode().startswith("Owen|1333333333|"))
    code, body = req("GET", "/v1/players/owen")
    pl = kvparse(body)
    check("player profile", pl.get("volt_best") == "120" and pl.get("volt_rank") == "2")
    check("unknown player 404", req("GET", "/v1/players/nobody")[0] == 404)
    check("legacy name of a profile refused", req("POST", "/v1/scores/volt", "name=OWEN&score=5")[0] == 409)
    check("legacy bad name refused", req("POST", "/v1/scores/volt", "name=a%20b&score=5")[0] == 400)
    check("out of range score refused", req("POST", "/v1/scores/volt", f"token={token}&score=99999999")[0] == 400)

    # Lockout after too many wrong passcodes
    req("POST", "/v1/accounts/create", "name=Lockme&pin=4321&avatar=1202001000")
    for _ in range(LOCK_AFTER):
        req("POST", "/v1/accounts/login", "name=Lockme&pin=0000")
    check("locked after wrong passcodes", req("POST", "/v1/accounts/login", "name=Lockme&pin=4321")[0] == 429)

    # Logout
    check("logout", req("POST", "/v1/accounts/logout", f"token={token}")[0] == 200)
    check("token dead after logout", req("POST", "/v1/accounts/me", f"token={token}")[0] == 401)

    # Inbox and admin
    check("inbox needs admin key", req("POST", "/v1/inbox", "hi")[0] == 401)
    check("inbox add", req("POST", "/v1/inbox", "Hello|Xbox\nfrom phone", key="k3y")[0] == 200)
    check("inbox read without console key refused", req("GET", "/v1/inbox")[0] == 401)
    code, body = req("GET", "/v1/inbox?key=c0nsole")
    check("inbox read cleaned", code == 200 and body.decode().split("|", 2)[2] == "Hello Xbox from phone\n")
    check("motd set", req("POST", "/v1/motd", "Game night 9PM", key="k3y")[0] == 200)
    check("inbox clear", req("POST", "/v1/inbox/clear", "", key="k3y")[0] == 200
          and req("GET", "/v1/inbox?key=c0nsole")[1] == b"")
    check("body too large", req("POST", "/v1/motd", "x" * (MAX_BODY + 1), key="k3y")[0] == 413)

    # Public service: news, hello numbers, v2 avatars, downloads
    token = kvparse(req("POST", "/v1/accounts/create", "name=Tuber&pin=5678&avatar=22020010000000")[1]).get("token", "")
    check("v2 avatar accepted at sign-up", token != "" and state.account("tuber")["avatar"] == "22020010000000")
    check("news post needs admin", req("POST", "/v1/news", "Patch notes")[0] == 401)
    check("news posted", req("POST", "/v1/news", "Patch notes are up", key="k3y")[0] == 200)
    code, body = req("GET", "/v1/news")
    check("news is public", code == 200 and body.decode().split("|", 2)[2] == "Patch notes are up\n")

    # Feedback: kept, read back by the admin, and its webhooks file read the forgiving way
    check("feedback needs a kind", req("POST", "/v1/feedback", "kind=other&text=Hello there")[0] == 400)
    check("feedback needs words", req("POST", "/v1/feedback", "kind=bug&text=x")[0] == 400)
    check("a bug report is taken", req("POST", "/v1/feedback",
                                       f"kind=bug&text=The+Store+froze%7Conce&version=0.6.7&platform=PC&token={token}")[0] == 200)
    check("an idea from a guest is taken", req("POST", "/v1/feedback", "kind=idea&text=More+games+please")[0] == 200)
    check("feedback list needs admin", req("GET", "/v1/feedback")[0] == 401)
    code, body = req("GET", "/v1/feedback", key="k3y")
    lines = body.decode().splitlines()
    check("feedback kept, newest first, from who", code == 200 and len(lines) == 2 and lines[0].split("|")[1:3] == ["idea", "Guest"]
          and lines[1].split("|", 5)[1:] == ["bug", "Tuber", "0.6.7", "PC", "The Store froze once"])
    hooks_dir = os.path.join(tmp, "hooks")
    os.makedirs(hooks_dir)
    check("no webhooks file, no webhooks", feedback_hooks(hooks_dir) == {})
    with open(os.path.join(hooks_dir, "feedback-webhooks.txt"), "w") as f:
        f.write("# feedback\nbug=https://discord.com/api/webhooks/1/abc_D-e\nhttps://discord.com/api/webhooks/2/xyz\n"
                "idea=https://example.com/not-discord\n")
    check("webhooks: one for bugs, one line for the rest", feedback_hooks(hooks_dir) ==
          {"bug": "https://discord.com/api/webhooks/1/abc_D-e", "idea": "https://discord.com/api/webhooks/2/xyz"})
    hello = kvparse(req("GET", "/v1/hello?name=Owen&ver=0.5")[1])
    check("hello has players and online", int(hello.get("players", "0")) >= 1 and int(hello.get("online", "0")) >= 1
          and int(hello.get("now", "0")) > 0)
    check("v2 avatar valid", valid_avatar("22020010000000") and valid_avatar("29dc797bab5442") and
          valid_avatar("2" + "0" * 13) and not valid_avatar("2" + "z" * 13) and not valid_avatar("2" + "0" * 12)
          and not valid_avatar("2adc797bab5442"))
    check("v1 avatar still valid", valid_avatar("1202001000") and not valid_avatar("1999999999"))
    check("v2 avatar saved", req("POST", "/v1/accounts/avatar", f"token={token}&avatar=29d97979ab4421")[0] == 200
          and state.account("tuber")["avatar"] == "29d97979ab4421")
    code, body = req("GET", "/download/config.txt")
    check("config download", code == 200 and body.decode().startswith(f"server=127.0.0.1:{httpd.server_address[1]}\n"))
    code, body = req("GET", "/download/xenithos-xbox360.zip")
    with zipfile.ZipFile(io.BytesIO(body)) as z:
        names = set(z.namelist())
        cfg_text = z.read("xenith/config.txt").decode()
        data_info = z.getinfo("data/gamedata.wad") if "data/gamedata.wad" in names else None
    check("xbox zip", code == 200 and {"apps/game.elf32", "apps/game.txt", "apps/game.ximg", "xenith/config.txt",
                                       "README.txt"} <= names and cfg_text.startswith("server=127.0.0.1:"))
    check("xbox zip has the game data its apps need, stored as is", data_info is not None
          and data_info.compress_type == zipfile.ZIP_STORED and "apps/gamedata.elf32" not in names)
    check("xbox zip leaves the PC update out", not any("xenithos-pc" in n for n in names))
    check("xbox zip comes from the cache", req("GET", "/download/xenithos-xbox360.zip") == (200, body))
    store_list[0]["version"] = "1.1"
    with open(os.path.join(store_dir, "store.json"), "w") as f:
        json.dump(store_list, f)
    later = time.time() + 5
    os.utime(os.path.join(store_dir, "store.json"), (later, later))
    code, body = req("GET", "/download/xenithos-xbox360.zip")
    with zipfile.ZipFile(io.BytesIO(body)) as z:
        app_txt = z.read("apps/game.txt").decode()
    check("xbox zip is rebuilt when the Store changes", code == 200 and "version=1.1" in app_txt)
    check("install.sh missing is 404", req("GET", "/install.sh")[0] == 404)
    with open(os.path.join(dl_dir, "install.sh"), "w") as f:
        f.write('SERVER="@XENITHOS_SERVER@"\nSOURCE_URL="@XENITHOS_SOURCE@"\n')
    code, body = req("GET", "/install.sh")
    check("install.sh filled in", code == 200 and f'SERVER="127.0.0.1:{httpd.server_address[1]}"'.encode() in body
          and b"/download/xenithos-source.tar.gz" in body)

    # Tube, with stand-in yt-dlp and ffmpeg
    calls_file = os.path.join(tmp, "calls.txt")

    def yt_calls(vid=None):
        """The stand-in yt-dlp's calls (argument lists); asks for one video when vid is given."""
        if not os.path.exists(calls_file):
            return []
        with open(calls_file) as f:
            rows = [line.split() for line in f]
        return [r for r in rows if vid is None or ("-j" in r and r[-1].endswith(vid))]

    def kind(row):
        return "full" if "--js-runtimes" in row and "--plugin-dirs" in row else \
               "quick" if "--no-js-runtimes" in row and "--plugin-dirs" not in row else "?"

    def play(vid, at=0):
        """Start a video and ask for its first chunk until it's more than "not ready yet"."""
        code, body = req("POST", "/v1/tube/start", tform + f"&id={vid}&t={at}")
        sid = kvparse(body).get("session", "") if code == 200 else ""
        for _ in range(12):
            got = req("GET", f"/v1/tube/chunk?s={sid}&off=0")
            if got[0] != 204:
                return code, sid, got
        return code, sid, (204, b"")

    tform = f"token={token}"
    check("tube needs a profile", req("POST", "/v1/tube/search", "q=doom")[0] == 401)
    code, body = req("POST", "/v1/tube/search", tform + "&q=doom")
    check("tube search", code == 200 and body == b"abcdefghijk|Test video One|Chan|61\n")
    check("tube search leaves Deno out", any("--flat-playlist" in r and "--no-js-runtimes" in r and
                                             "--plugin-dirs" not in r for r in yt_calls()))
    check("tube bad id", req("POST", "/v1/tube/start", tform + "&id=../x")[0] == 400)
    t0 = time.monotonic()
    code, body = req("POST", "/v1/tube/start", tform + "&id=abcdefghijk&t=5")
    sess = kvparse(body)
    check("tube start answers at once, with what the search said", code == 200 and time.monotonic() - t0 < 1
          and TUBE_SID_RE.match(sess.get("session", "")) and sess.get("fps") == "20"
          and sess.get("title") == "Test video One" and sess.get("duration") == "61")
    sid = sess.get("session", "")
    code, first = req("GET", f"/v1/tube/chunk?s={sid}&off=0")
    check("tube chunk", code == 200 and first.startswith(b"RIFF") and b"movi00dc" in first)
    check("tube asks the quick way first", [kind(r) for r in yt_calls("abcdefghijk")] == ["quick"])
    check("tube chunk again", req("GET", f"/v1/tube/chunk?s={sid}&off=0") == (200, first))
    check("tube chunk out of order", req("GET", f"/v1/tube/chunk?s={sid}&off=3")[0] == 400)
    code, _ = req("GET", f"/v1/tube/chunk?s={sid}&off={len(first)}")
    check("tube stream ends", code == 410 and req("GET", f"/v1/tube/chunk?s={sid}&off={len(first)}")[0] == 410)
    check("tube stop", req("GET", f"/v1/tube/stop?s={sid}")[0] == 200)
    code, sid, got = play("abcdefghijk", 30)
    check("tube skipping ahead reuses the links", got[0] == 200 and len(yt_calls("abcdefghijk")) == 1)

    code, sid, got = play("quickfails1")
    check("tube refused the quick way: the full way gets it", got[0] == 200
          and [kind(r) for r in yt_calls("quickfails1")] == ["quick", "full"] and tube.full_until > time.time())
    code, sid, got = play("privatevid1")
    check("tube goes straight to the full way for a while", [kind(r) for r in yt_calls("privatevid1")] == ["full"])
    check("tube says when a video can't be played", got[0] == 503 and b"can't be played" in got[1])
    check("tube says it again to a console that retries",
          req("GET", f"/v1/tube/chunk?s={sid}&off=0") == got)
    tube.full_until = 0
    code, sid, got = play("blockedall1")
    check("tube says when YouTube refuses both ways", got[0] == 503 and b"turning XenithOS Cloud away" in got[1]
          and [kind(r) for r in yt_calls("blockedall1")] == ["quick", "full"])
    code, sid, got = play("emptystream")
    code, sid, again = play("emptystream")
    check("tube links that stop working are asked for again", got[0] == 503 and b"stopped sending" in got[1]
          and again[0] == 503 and len(yt_calls("emptystream")) == 2)
    code, body = req("POST", "/v1/tube/start", tform + "&id=slowvideo11")
    sid = kvparse(body).get("session", "")
    slow = tube.get(sid)
    for _ in range(40):                            # wait for the stand-in yt-dlp to be running
        if slow and slow.helper:
            break
        time.sleep(0.05)
    t0 = time.monotonic()
    req("GET", f"/v1/tube/stop?s={sid}")
    for _ in range(60):
        if not slow or not slow.helper:
            break
        time.sleep(0.05)
    check("tube stop ends a slow ask", slow and not slow.helper and time.monotonic() - t0 < 3
          and req("GET", f"/v1/tube/chunk?s={sid}&off=0")[0] == 410)
    check("tube thumb bad id", req("GET", "/v1/tube/thumb/..x.jpg")[0] == 404)

    # Cloudflare's tunnel (XENITH_EDGE_PORT): CF-Connecting-IP names the console
    import socket
    edged = ThreadingHTTPServer(("127.0.0.1", 0), edge(httpd.RequestHandlerClass))
    edged.daemon_threads = True
    threading.Thread(target=edged.serve_forever, daemon=True).start()

    def send_raw(port, data):
        with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
            sock.sendall(data)
            out = b""
            while True:
                part = sock.recv(65536)
                if not part:
                    return out
                out += part

    def via_tunnel(method, path, ip, body=""):
        head = f"{method} {path} HTTP/1.1\r\nHost: cloud.example.org\r\nConnection: keep-alive\r\n"
        head += f"CF-Connecting-IP: {ip}\r\n" if ip else ""
        head += f"Content-Length: {len(body)}\r\n" if body else ""
        return send_raw(edged.server_address[1], (head + "\r\n" + body).encode())

    def code_of(out):
        return (out.split(b" ", 2) + [b"", b""])[1]

    out = via_tunnel("GET", "/v1/hello?name=TUNNELED&ver=0.6", "198.51.100.4")
    check("edge: CF-Connecting-IP accepted", out.startswith(b"HTTP/1.0 200") and b"motd=" in out)
    check("edge: the console's own address is recorded",
          state.data["consoles"].get("TUNNELED", {}).get("ip") == "198.51.100.4")
    check("edge: no CF-Connecting-IP, no answer", via_tunnel("GET", "/v1/ping", "") == b"")
    check("edge: a bad address, no answer", via_tunnel("GET", "/v1/ping", "999.1.1.1") == b"")
    send_raw(httpd.server_address[1], b"GET /v1/hello?name=DIRECTCF&ver=0.6 HTTP/1.0\r\n"
                                      b"CF-Connecting-IP: 198.51.100.9\r\n\r\n")
    check("edge: the direct port ignores CF-Connecting-IP",
          state.data["consoles"].get("DIRECTCF", {}).get("ip") == "127.0.0.1")

    def score(ip, name):
        return code_of(via_tunnel("POST", "/v1/scores/volt", ip, f"name={name}&score=5"))

    check("edge: rate limits go by each console's address",
          score("198.51.100.4", "EDGEA") == b"200" and score("198.51.100.4", "EDGEA") == b"429"
          and score("198.51.100.5", "EDGEB") == b"200")
    check("edge: an IPv6 /64 counts as one address",
          score("2001:db8:1:2::5", "EDGEC") == b"200" and score("2001:db8:1:2::6", "EDGEC") == b"429"
          and score("2001:db8:1:3::5", "EDGED") == b"200")
    check("edge: install.sh gets the source over HTTPS too",
          b'SOURCE_URL="https://cloud.example.org/download/xenithos-source.tar.gz"' in
          via_tunnel("GET", "/install.sh", "198.51.100.4"))
    check("edge: install.sh from the direct port stays plain",
          b'SOURCE_URL="http://127.0.0.1:' in req("GET", "/install.sh")[1])
    edged.shutdown()

    # A relay's connections (XENITH_PROXY_PORT): the PROXY line names the console
    import socket
    relayed = ThreadingHTTPServer(("127.0.0.1", 0), proxied(httpd.RequestHandlerClass))
    relayed.daemon_threads = True
    threading.Thread(target=relayed.serve_forever, daemon=True).start()

    def raw(data):
        with socket.create_connection(("127.0.0.1", relayed.server_address[1]), timeout=5) as sock:
            sock.sendall(data)
            out = b""
            while True:
                part = sock.recv(65536)
                if not part:
                    return out
                out += part

    out = raw(b"PROXY TCP4 203.0.113.7 10.0.0.2 51000 8795\r\nGET /v1/hello?name=RELAYED&ver=0.5 HTTP/1.0\r\n\r\n")
    check("relay: PROXY line accepted", out.startswith(b"HTTP/1.0 200") and b"motd=" in out)
    check("relay: the console's own address is recorded",
          state.data["consoles"].get("RELAYED", {}).get("ip") == "203.0.113.7")
    check("relay: IPv6 accepted",
          raw(b"PROXY TCP6 2001:db8::7 2001:db8::1 51000 8795\r\nGET /v1/ping HTTP/1.0\r\n\r\n").endswith(b"pong\n"))
    check("relay: no PROXY line, no answer", raw(b"GET /v1/ping HTTP/1.0\r\n\r\n") == b"")
    check("relay: bad address, no answer",
          raw(b"PROXY TCP4 999.1.1.1 10.0.0.2 1 2\r\nGET /v1/ping HTTP/1.0\r\n\r\n") == b"")
    check("relay: family must match the address",
          raw(b"PROXY TCP6 203.0.113.7 ::1 1 2\r\nGET /v1/ping HTTP/1.0\r\n\r\n") == b"")
    check("relay: the direct port ignores PROXY lines", req("GET", "/v1/ping") == (200, b"pong\n"))
    relayed.shutdown()

    reloaded = State(os.path.join(tmp, "state"))
    check("state persists", reloaded.data["motd"] == "Game night 9PM"
          and reloaded.account("tuber")["avatar"] == "29d97979ab4421")

    httpd.shutdown()
    shutil.rmtree(tmp)
    for name, ok in checks:
        print(("PASS " if ok else "FAIL ") + name)
    failed = sum(not ok for _, ok in checks)
    print(f"{len(checks) - failed}/{len(checks)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["store-key"]:
        sys.exit(store_key_command(args[1:]))
    if args == ["--selftest"]:
        sys.exit(selftest())
    if len(args) == 2 and args[0] in ("send", "motd"):
        # Talk to the running server so its in-memory state stays the source of truth.
        import urllib.error
        import urllib.request
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
