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
                                      that instead, once it answers with the same id.
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

HostMC's supporter relay (off unless XENITH_RELAY_DOMAIN is set; see Relay)
  POST /v1/relay/status               token  -> relay (on/off), step (ok, link, patreon, tier,
                                      error), message, suggest, and with a name: name, state (on,
                                      lapsed, off), until, address (with :port when the relay's
                                      players_port isn't 25565), players_port, port, ssh_host,
                                      ssh_port, ssh_user, hostkey; held (names kept for them)
  POST /v1/relay/claim                token, name, key (HostMC's ssh-ed25519 public key) -> as status
  POST /v1/relay/release              token  -> as status
  POST /v1/relay/sync                 from the relay machine: t, hostkey, busy (ports with a
                                      tunnel on them), signed (X-Relay-Sign)
                                      -> t, domain, on|name|port|key and held|name rows, signed

Admin (header X-Xenith-Key: <XENITH_ADMIN_KEY>)
  POST /v1/news (or /v1/inbox)        body is the news post
  POST /v1/inbox/clear
  POST /v1/motd                       body is the new message of the day
  GET  /v1/feedback                   time|kind|from|version|platform|text, newest first
  GET  /v1/relay/list                 name|profile|state|port|until|step|checked|tunnel
  POST /v1/relay/off, /v1/relay/on    body is a name: turns it off on the relay, or back on
  POST /v1/relay/check                body is a name: asks OCAccounts about its supporter now
                                      -> step, tier, state

Command line
  xenith_cloud.py send "text"         message the console inbox (through the running server)
  xenith_cloud.py motd "text"         set the message of the day
  xenith_cloud.py relay list | off <name> | on <name> | check <name>
  xenith_cloud.py --selftest          built-in tests (no network, temp dir)

Environment
  XENITH_PORT        default 8795
  XENITH_PROXY_PORT  a second port for a relay (server/xenith-relay.py) that starts every
                     connection with a PROXY protocol v1 line naming the console's own address,
                     so XenithOS Cloud can run behind a home connection while consoles keep a
                     public address. Off when unset; only the relay should reach this port.
  XENITH_STATE_DIR   default ./state      (state.json lives here)
  XENITH_STORE_DIR   default ./store      (store.json + the files it lists, flathub.json, steam.json,
                                          and <id>.ximg / <id>.png thumbnails)
  XENITH_WEB_DIR     default ./web        (index.html and static/)
  XENITH_ADMIN_KEY   admin key; admin routes are off when unset
  XENITH_CONSOLE_KEY when set, the old /v1/inbox route needs ?key=<this>
  XENITH_PUBLIC_ADDR the "ip:port" consoles should use (goes into downloads); default: the
                     address each download was requested from
  XENITH_LAN_ADDR    the private "a.b.c.d:port" consoles on this server's own network can reach
                     it at directly, skipping the relay (/v1/lan). Off when unset.
  XENITH_DOWNLOAD_DIR default ./downloads   (install.sh, xenithos-source.tar.gz)
  XENITH_YTDLP, XENITH_FFMPEG  paths for Tube (default: found on PATH); Tube is off without them
  XENITH_TUBE_STREAMS default 3: videos playing at once, across everyone
  XENITH_DENO        Deno, so yt-dlp can solve YouTube's JavaScript challenges when the quick
                     way is refused (setup-tube.sh installs it)
  XENITH_YTDLP_PLUGINS  a yt-dlp plugin folder: the PO token provider, whose token server is
                     xenith-pot.service
  HostMC's supporter relay (server/mcrelay; its setup prints the first three):
  XENITH_RELAY_DOMAIN  names are <name>.<this>, e.g. mc.owencosta.diy; the relay is off when unset
  XENITH_RELAY_SSH     host:port of the relay's SSH for tunnels, e.g. tunnel.mc.owencosta.diy:2222
  XENITH_RELAY_SECRET  shared with the relay, which signs its syncs with it
  XENITH_RELAY_USER    default hostmc: the relay's account for tunnels
  XENITH_RELAY_PLAYERS_PORT  default 25565: the relay's port for players (another one is added to
                     the address friends type, <name>.<domain>:<port>)
  XENITH_RELAY_PORTS   default 30000-39999: supporters' ports on the relay's loopback
  XENITH_OCACCOUNT_URL, XENITH_OCACCOUNT_KEY  where OCAccounts answers who's a supporter
                     (/api/partner/xenithos/supporter), and the key it wants for that
  XENITH_RELAY_PATREON  the Patreon page HostMC shows a QR code of
  XENITH_RELAY_ACCOUNT_URL  default https://owencosta.diy/account
  relay-blocked-words.txt in the state folder: words no name may contain, one a line
"""
import base64
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
from urllib.parse import parse_qs, unquote, urlencode, urlparse

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


def lan_addr_ok(addr):
    """XENITH_LAN_ADDR: a private IPv4 address and port (the Xbox has no DNS)."""
    m = LAN_ADDR_RE.match(addr or "")
    if not m or not 0 < int(m.group(2)) < 65536:
        return False
    try:
        return ipaddress.ip_address(m.group(1)).is_private
    except ValueError:
        return False


# ---------------------------------------------------------------------- supporter relay

# HostMC's supporter relay. Someone who supports Owen on Patreon (OCAccounts knows, once they've
# linked Patreon and their XenithOS profile there) gets one name, <name>.<XENITH_RELAY_DOMAIN>,
# for their Minecraft server. The relay machine (server/mcrelay) asks /v1/relay/sync for the
# names, each supporter's port on its loopback and the key their HostMC logs in with; HostMC keeps
# an SSH tunnel to it that can open that one port, and the relay passes players to it by the name
# their game asks for.

RELAY_NAME_RE = re.compile(r"^[a-z][a-z0-9-]{1,18}[a-z0-9]$")
RELAY_DOMAIN_RE = re.compile(r"^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z]{2,63}$")
RELAY_HOST_RE = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,252})$")
RELAY_RESERVED = {
    "www", "mail", "smtp", "imap", "pop", "ftp", "ns", "ns1", "ns2", "dns", "mx", "api", "admin",
    "administrator", "root", "tunnel", "relay", "ssh", "status", "help", "support", "abuse",
    "postmaster", "hostmaster", "webmaster", "noreply", "security", "localhost", "test", "staging",
    "dev", "owen", "owencosta", "ocaccount", "ocaccounts", "account", "accounts", "login", "auth",
    "oauth", "patreon", "xenith", "xenithos", "hostmc", "cloud", "mc", "minecraft", "play", "server",
    "official", "staff", "mod", "moderator", "mojang", "microsoft", "xbox",
}
RELAY_HOLD_LAPSED = 30 * 86400     # a membership that ends: the name waits this long for them
RELAY_HOLD_LEFT = 7 * 86400        # a name changed or given up: theirs to take back for a week
RELAY_CHECK_FRESH = 5 * 60         # OCAccounts' answers are reused for this long
RELAY_RECHECK = 6 * 3600           # names in use are checked again this often
RELAY_RETRY = 10 * 60              # and this long after OCAccounts didn't answer
RELAY_SYNC_SKEW = 120              # a signed sync request must be this fresh (seconds)
RELAY_CHANGES_PER_DAY = 3
RELAY_ADJECTIVES = ("brave", "calm", "clever", "cosy", "swift", "happy", "lucky", "mighty", "quiet",
                    "sunny", "bold", "bright", "misty", "frosty", "golden", "jolly", "nimble", "rapid",
                    "silver", "wild", "cosmic", "gentle", "lunar", "mellow", "noble", "polar", "rusty",
                    "sleepy", "snowy", "stormy", "witty", "zesty", "amber", "breezy", "cheery", "dusky")
RELAY_ANIMALS = ("otter", "fox", "badger", "falcon", "panda", "koala", "lynx", "owl", "wolf", "heron",
                 "bison", "gecko", "lemur", "moose", "raven", "tiger", "whale", "yak", "zebra", "beaver",
                 "crane", "ferret", "hare", "ibis", "jaguar", "kiwi", "llama", "marten", "newt", "orca",
                 "puffin", "quokka", "robin", "seal", "toucan", "walrus", "axolotl", "panther")
RELAY_KEY_HEAD = b"\0\0\0\x0bssh-ed25519\0\0\0\x20"
LEET = str.maketrans("013457", "oieast")


def relay_key(text):
    """The public key line a HostMC sends, rebuilt from its bytes so nothing else can reach the
    relay's list of keys: "ssh-ed25519 <base64>", or "" when it isn't one."""
    parts = (text or "").split()
    if len(parts) < 2 or parts[0] != "ssh-ed25519" or len(parts[1]) != 68:
        return ""
    try:
        blob = base64.b64decode(parts[1], validate=True)
    except ValueError:
        return ""
    if len(blob) != 51 or not blob.startswith(RELAY_KEY_HEAD):
        return ""
    return "ssh-ed25519 " + base64.b64encode(blob).decode()


def relay_blocked(state_dir):
    """Words no name may contain: relay-blocked-words.txt in the state folder, one a line (Owen's
    to fill in; it's read each time, so changes count at once)."""
    try:
        with open(os.path.join(state_dir, "relay-blocked-words.txt"), encoding="utf-8") as f:
            words = [line.strip().lower() for line in f]
    except OSError:
        return []
    return [w.replace("-", "") for w in words if w and not w.startswith("#")]


def relay_name_problem(name, blocked=()):
    """What's wrong with a name someone asked for, or None. Names are DNS labels: 3 to 20 small
    letters, numbers and single hyphens, starting with a letter."""
    if not 3 <= len(name) <= 20:
        return "Names are 3 to 20 letters, numbers and hyphens."
    if not RELAY_NAME_RE.match(name) or "--" in name:
        return "Use letters, numbers and single hyphens, starting with a letter."
    if name in RELAY_RESERVED:
        return "That name is reserved. Try another one."
    squashed = name.replace("-", "")
    if any(w and (w in squashed or w in squashed.translate(LEET)) for w in blocked):
        return "That name isn't allowed. Try another one."
    return None


def relay_suggest(taken):
    """A free name like brave-otter-42, for HostMC to offer."""
    for _ in range(40):
        name = f"{secrets.choice(RELAY_ADJECTIVES)}-{secrets.choice(RELAY_ANIMALS)}-{secrets.randbelow(90) + 10}"
        if name not in taken and name not in RELAY_RESERVED:
            return name
    return ""


def relay_sign(secret, kind, body):
    """The signature on a sync request (kind "sync") or on its answer ("list"): HMAC-SHA256 of
    the body, with the secret XenithOS Cloud and the relay share. The secret never crosses the
    wire, and an answer can't be passed off as a request or the other way round."""
    if isinstance(body, str):
        body = body.encode()
    return hmac.new(secret.encode(), kind.encode() + b"\n" + body, hashlib.sha256).hexdigest()


def ocaccount_supporter(url, key, gamertag, timeout=10):
    """Asks OCAccounts whether a XenithOS profile belongs to a supporter:
    GET <url>/api/partner/xenithos/supporter?profile=<gamertag> with X-Partner-Key, answering
    {"linked": bool, "patreon": bool, "supporter": bool, "tier": "..."}. Returns (step, tier),
    step being "ok", "link" (no OCAccount has this profile linked), "patreon" (no Patreon linked
    to it), "tier" (not on the supporter tier or above) or "error" (no answer to go by)."""
    if not url:
        return "error", ""
    req = urllib.request.Request(url.rstrip("/") + "/api/partner/xenithos/supporter?" + urlencode({"profile": gamertag}),
                                 headers={"X-Partner-Key": key, "Accept": "application/json",
                                          "User-Agent": "XenithOS-Cloud/" + VERSION})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.loads(resp.read(8192).decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        return ("link", "") if e.code == 404 else ("error", "")
    except (OSError, ValueError):
        return "error", ""
    if not isinstance(data, dict):
        return "error", ""
    if data.get("linked") is not True:
        return "link", ""
    if data.get("patreon") is not True:
        return "patreon", ""
    return ("ok" if data.get("supporter") is True else "tier"), clean(data.get("tier") or "", 40)


class Relay:
    """The supporter relay's names, kept in state.json under "relay".

    names: name -> {owner, state, until, since}. A name's state is "on" (the relay passes players
    to it), "lapsed" (its supporter's membership ended: kept for them until "until", and on again
    if they come back), "left" (changed or given up: theirs alone to take back until "until") or
    "off" (turned off with `xenith relay off`). Someone has one name that isn't "left" at most.
    people: profile (lower case) -> {port, key, checked, step, tier}: their port on the relay's
    loopback, kept while they have any name, and the key their HostMC logs in with."""

    def __init__(self, state, domain="", ssh="", secret="", user="hostmc", ports="30000-39999",
                 ocaccount_url="", ocaccount_key="", patreon_url="", account_url="", players_port=25565):
        self.state, self.lock = state, state.lock
        self.domain = domain.strip().lower().strip(".")
        self.players_port = int_or(players_port, 0)           # the relay's port for players
        self.at = "" if self.players_port == 25565 else f":{self.players_port}"   # what friends add to the name
        host, sep, port = ssh.strip().rpartition(":")
        self.ssh_host, self.ssh_port = (host, int_or(port, 0)) if sep else (ssh.strip(), 22)
        self.secret, self.user = secret, user or "hostmc"
        lo, _, hi = ports.partition("-")
        self.port_lo = int_or(lo, 0)
        self.port_hi = int_or(hi, self.port_lo) if hi else self.port_lo
        self.ocaccount_url, self.ocaccount_key = ocaccount_url, ocaccount_key
        self.patreon_url = patreon_url
        self.account_url = account_url or "https://owencosta.diy/account"
        self.checks = {}                           # profile -> (good until, step, tier)
        self.tried = {}                            # profile -> when OCAccounts last didn't answer
        self.changes = RateLimit(RELAY_CHANGES_PER_DAY, 86400)
        self.asks = RateLimit(60, 3600)
        self.last_sync = 0
        self.busy = set()                          # ports with a tunnel on them, at the last sync
        with self.lock:
            r = state.data.setdefault("relay", {})
            r.setdefault("names", {})
            r.setdefault("people", {})

    def problems(self):
        """What's wrong with the settings (XENITH_RELAY_*), for the log; the relay stays off."""
        out = []
        if not RELAY_DOMAIN_RE.match(self.domain):
            out.append(f"XENITH_RELAY_DOMAIN must be a domain such as mc.example.com, not {self.domain!r}")
        if not RELAY_HOST_RE.match(self.ssh_host) or not 0 < self.ssh_port < 65536:
            out.append("XENITH_RELAY_SSH must be the relay's SSH address for tunnels, host:port")
        if len(self.secret) < 32:
            out.append("XENITH_RELAY_SECRET must be the relay's secret (its setup prints one)")
        if not 1024 < self.port_lo <= self.port_hi < 65536:
            out.append("XENITH_RELAY_PORTS must be a range such as 30000-39999")
        if not 0 < self.players_port < 65536:
            out.append("XENITH_RELAY_PLAYERS_PORT must be the relay's port for players, like 25565")
        return out

    @property
    def data(self):
        return self.state.data["relay"]

    def ready(self):
        """Open to supporters: set up here, and the relay has said hello (with its host key)."""
        return bool(self.domain and self.secret and self.data.get("hostkey"))

    def _current(self, who):
        for name, n in self.data["names"].items():
            if n["owner"] == who and n["state"] != "left":
                return name, n
        return None, None

    def message(self, step, state=None, until=0):
        site = self.account_url.split("://", 1)[-1].rstrip("/")
        if state == "off":
            return "This name has been turned off on the relay."
        if state == "lapsed":
            t = time.gmtime(until)
            return (f"Your Patreon membership ended, so the relay stopped. Your name is kept for you "
                    f"until {t.tm_mday} {time.strftime('%B', t)} {t.tm_year}.")
        return {"ok": "", "link": f"Link this XenithOS profile to your OCAccount at {site} first.",
                "patreon": f"Link Patreon to your OCAccount at {site}.",
                "tier": "Your Patreon membership isn't on the supporter tier.",
                "error": "Your membership couldn't be checked just now. Try again in a few minutes."}[step]

    # -------------------------------------------------------------- who's a supporter
    def supporter(self, who, gamertag, now=None, cached=True):
        """(step, tier) for a profile (see ocaccount_supporter), from an answer kept a few
        minutes or a new one. A definite answer also turns their name on or lapses it."""
        now = time.time() if now is None else now
        kept = self.checks.get(who)
        if cached and kept and now < kept[0]:
            return kept[1], kept[2]
        step, tier = ocaccount_supporter(self.ocaccount_url, self.ocaccount_key, gamertag)
        self.checks[who] = (now + (30 if step == "error" else RELAY_CHECK_FRESH), step, tier)
        if step == "error":
            self.tried[who] = now
        else:
            self.settle(who, step, tier, now)
        return step, tier

    def settle(self, who, step, tier, now):
        with self.lock:
            p = self.data["people"].get(who)
            if not p:
                return
            p["checked"], p["step"], p["tier"] = int(now), step, tier
            _, n = self._current(who)
            if n and step == "ok" and n["state"] == "lapsed":
                n["state"], n["until"] = "on", 0
            elif n and step != "ok" and n["state"] == "on":
                n["state"], n["until"] = "lapsed", int(now + RELAY_HOLD_LAPSED)
            self.state.save()

    # -------------------------------------------------------------- names
    def claim(self, who, name, key, now=None):
        """Gives a supporter a name, or takes their key again for the one they have. The
        supporter check is the caller's. Returns (http code, message)."""
        now = time.time() if now is None else now
        problem = relay_name_problem(name, relay_blocked(os.path.dirname(self.state.path)))
        if problem:
            return 400, problem
        key = relay_key(key)
        if not key:
            return 400, "HostMC's key isn't one the relay takes."
        with self.lock:
            names, people = self.data["names"], self.data["people"]
            cur, cn = self._current(who)
            if cn and cn["state"] == "off":
                return 403, self.message("ok", "off")
            n = names.get(name)
            if n and n["owner"] != who:
                return 409, "That name is taken. Try another one."
            if any(q.get("key") == key for w, q in people.items() if w != who):
                return 409, "HostMC's key is in use by someone else. Make HostMC a new one."
            if cur != name and cur is not None and not self.changes.allow(who):
                return 429, "That's a few new names today. Try again tomorrow."
            p = people.get(who)
            if not p:
                used = {q["port"] for q in people.values()} | self.busy
                port = next((x for x in range(self.port_lo, self.port_hi + 1) if x not in used), 0)
                if not port:
                    return 503, "The relay is full right now."
                p = people[who] = {"port": port, "checked": int(now), "step": "ok", "tier": ""}
            p["key"] = key
            if cur != name:
                if cn:                             # the old name waits a week for them
                    cn["state"], cn["until"] = "left", int(now + RELAY_HOLD_LEFT)
                names[name] = {"owner": who, "state": "on", "until": 0, "since": int(now)}
            elif cn["state"] == "lapsed":
                cn["state"], cn["until"] = "on", 0
            self.state.save()
            return 200, ""

    def release(self, who, now=None):
        now = time.time() if now is None else now
        with self.lock:
            _, cn = self._current(who)
            if not cn:
                return 404, "You don't have a name on the relay."
            if cn["state"] == "off":
                return 403, self.message("ok", "off")
            cn["state"], cn["until"] = "left", int(now + RELAY_HOLD_LEFT)
            self.state.save()
            return 200, ""

    def recheck(self, name):
        """`xenith relay check <name>`: asks OCAccounts about its supporter now, which turns the
        name on or lapses it. None for a name nobody has."""
        with self.lock:
            n = self.data["names"].get(name)
            acc = self.state.account(n["owner"]) if n else None
        if not acc:
            return None
        step, tier = self.supporter(acc["name"].lower(), acc["name"], cached=False)
        with self.lock:
            return {"step": step, "tier": tier, "state": self.data["names"].get(name, {}).get("state", "")}

    def turn(self, name, on):
        """`xenith relay off|on <name>`. Returns False for a name nobody has."""
        with self.lock:
            n = self.data["names"].get(name)
            if not n or n["state"] == "left":
                return False
            if not on:
                n["state"], n["until"] = "off", 0
            elif n["state"] == "off":
                n["state"] = "on"                  # the next check lapses it if it should
                p = self.data["people"].get(n["owner"])
                if p:
                    p["checked"] = 0
            self.state.save()
            return True

    def status(self, acc, now=None):
        """What HostMC is told: key=value fields for kv()."""
        now = time.time() if now is None else now
        who = acc["name"].lower()
        step, tier = self.supporter(who, acc["name"], now)
        with self.lock:
            name, n = self._current(who)
            p = self.data["people"].get(who) or {}
            out = {"relay": "on", "domain": self.domain, "step": step, "tier": tier,
                   "patreon": self.patreon_url, "account": self.account_url}
            if n:
                out.update({"name": name, "state": n["state"], "until": n.get("until", 0),
                            "address": f"{name}.{self.domain}{self.at}", "players_port": self.players_port,
                            "port": p.get("port", 0),
                            "ssh_host": self.ssh_host, "ssh_port": self.ssh_port, "ssh_user": self.user,
                            "hostkey": self.data.get("hostkey", "")})
            held = sorted(x for x, m in self.data["names"].items() if m["owner"] == who and m["state"] == "left")
            if held:
                out["held"] = ",".join(held)
            out["suggest"] = relay_suggest(self.data["names"])
        out["message"] = self.message(step, n["state"] if n else None, n.get("until", 0) if n else 0)
        return out

    def rows(self):
        """`xenith relay list`: name|profile|state|port|until|step|checked|tunnel (up when the
        relay saw one on that port at its last sync)."""
        with self.lock:
            out = []
            for name, n in sorted(self.data["names"].items()):
                p = self.data["people"].get(n["owner"], {})
                port = p.get("port", 0)
                out.append(f"{name}|{n['owner']}|{n['state']}|{port}|{n.get('until', 0)}|{p.get('step', '')}|"
                           f"{p.get('checked', 0)}|{'up' if port in self.busy and n['state'] != 'left' else ''}\n")
            return "".join(out)

    # -------------------------------------------------------------- the relay machine
    def signed(self, body, signature):
        return bool(self.secret) and hmac.compare_digest(relay_sign(self.secret, "sync", body).encode(),
                                                         (signature or "").strip().encode())

    def synced(self, hostkey, busy="", now=None):
        """A signed sync came in: the relay's SSH host key, which HostMC pins, and the ports with
        a tunnel on them. A port with one left on from a name that's no longer on isn't given to
        anyone else while it lasts."""
        self.last_sync = time.time() if now is None else now
        self.busy = {p for p in (int_or(x, 0) for x in busy.split(",")[:5000]) if self.port_lo <= p <= self.port_hi}
        hostkey = relay_key(hostkey)
        with self.lock:
            if hostkey and hostkey != self.data.get("hostkey"):
                self.data["hostkey"] = hostkey
                self.state.save()

    def listing(self, t):
        """The answer to a sync: t (the request's, so an old answer can't be passed off as
        new), the domain, "on|name|port|key" for names in use and "held|name" for the rest."""
        with self.lock:
            people = self.data["people"]
            lines = [f"t={t}\n", f"domain={self.domain}\n"]
            for name, n in sorted(self.data["names"].items()):
                p = people.get(n["owner"])
                if n["state"] == "on" and p and p.get("key"):
                    lines.append(f"on|{name}|{p['port']}|{p['key']}\n")
                else:
                    lines.append(f"held|{name}\n")
            return "".join(lines)

    # -------------------------------------------------------------- now and then
    def tick(self, now=None):
        """Every minute or so: frees names whose time is up, and asks OCAccounts again about
        names in use (a few at a time)."""
        now = time.time() if now is None else now
        with self.lock:
            names = self.data["names"]
            gone = [x for x, n in names.items() if n["state"] in ("lapsed", "left") and n["until"] <= now]
            for x in gone:
                del names[x]
            owners = {n["owner"] for n in names.values()}
            for who in [w for w in self.data["people"] if w not in owners]:
                del self.data["people"][who]
            if gone:
                self.state.save()
            due = sorted((p.get("checked", 0), who) for who, p in self.data["people"].items()
                         if now - p.get("checked", 0) >= RELAY_RECHECK and now - self.tried.get(who, 0) >= RELAY_RETRY
                         and self._current(who)[1] and self._current(who)[1]["state"] in ("on", "lapsed"))
        for _, who in due[:20]:
            acc = self.state.account(who)
            if acc:
                self.supporter(who, acc["name"], now, cached=False)


# ---------------------------------------------------------------------- http


def kv(d):
    return "".join(f"{k}={clean(v)}\n" for k, v in d.items())


def make_handler(state, store, admin_key, web_dir="web", console_key="", tube=None, public="",
                 downloads="downloads", lan="", server_id="", relay=None):
    last_score = {}
    last_score_lock = threading.Lock()
    creates = RateLimit(CREATE_PER_HOUR, 3600)
    logins = RateLimit(LOGIN_PER_10MIN, 600)
    feedback_by_ip = RateLimit(30, 3600)          # consoles reached through the relay share an address
    feedback_by_name = RateLimit(6, 3600)

    class Handler(BaseHTTPRequestHandler):
        server_version = "XenithOS-Cloud/" + VERSION
        protocol_version = "HTTP/1.0"

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
                return self.reply(200, kv({"id": server_id, **({"lan": lan} if lan else {})}), cache="no-store")

            if parts == ["v1", "status"]:
                lines = {"motd": state.data["motd"], "server": "XENITHOS CLOUD " + VERSION,
                         "consoles": str(len(state.data["consoles"])),
                         "players": str(len(state.data["accounts"])),
                         "apps": str(len(store.items()))}
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

            if parts == ["v1", "relay", "list"]:
                if not self.is_admin():
                    return self.error(401, "Admin key required.")
                if not relay:
                    return self.error(404, "The supporter relay isn't set up here.")
                head = f"# relay {'on' if relay.ready() else 'off'}, last sync {int(relay.last_sync)}\n"
                return self.reply(200, head + relay.rows(), cache="no-store")

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
                source = f"http://{host}/download/xenithos-source.tar.gz" if PUBLIC_ADDR_RE.match(host) or re.match(r"^[A-Za-z0-9.-]{1,64}$", host) else ""
                script = script.replace("@XENITHOS_SERVER@", addr).replace("@XENITHOS_SOURCE@", source)
                return self.reply(200, script.encode(), "text/plain; charset=utf-8", "no-store")

            if parts == ["v1", "inbox"]:
                if console_key and not hmac.compare_digest(
                        q.get("key", [""])[0].encode(), console_key.encode()):
                    return self.error(401, "This console isn't allowed to read the inbox.")
                return self.reply(200, "".join(f"{m['id']}|{m['t']}|{m['text']}\n"
                                               for m in state.data["inbox"]))

            if parts == ["v1", "store"]:
                return self.reply(200, store_rows(store.items()))

            if parts == ["v1", "store", "all"]:
                return self.reply(200, store_bundle(store, q.get("pc", [""])[0] == "1"),
                                  "application/octet-stream", "no-store")

            if parts == ["v1", "store", "flathub"]:
                # Their own routes: consoles that don't know these kinds never see them.
                return self.reply(200, flathub_rows(store.flathub()))

            if parts == ["v1", "store", "steam"]:
                return self.reply(200, steam_rows(store.steam()))

            if parts == ["v1", "store", "needs"]:
                # Its own route: a 9th field on /v1/store would end up in older consoles' descriptions.
                return self.reply(200, needs_rows(store.needs()))

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
                return self.send_file(path, "image/png" if ext == "png" else "application/octet-stream")

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

            if parts == ["v1", "accounts", "create"]:
                name, pin, avatar = form.get("name", "").strip(), form.get("pin", ""), form.get("avatar", "")
                problem = check_gamertag(name)
                if problem:
                    return self.error(400, problem)
                if not PIN_RE.match(pin):
                    return self.error(400, "Passcodes are 4 to 8 digits.")
                if not valid_avatar(avatar):
                    return self.error(400, "That avatar code isn't valid.")
                if not creates.allow(ip):
                    return self.error(429, "Too many new profiles from this network. Try again later.")
                result, err = state.create(name, pin, avatar)
                if err:
                    return self.error(409, err)
                return self.session_reply(*result)

            if parts == ["v1", "accounts", "login"]:
                if not logins.allow(ip):
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
                if not feedback_by_ip.allow(ip) or (acc and not feedback_by_name.allow(acc["name"])):
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

            if parts == ["v1", "relay", "sync"]:
                if not relay or not relay.domain or not relay.secret:
                    return self.error(404, "Not found.")
                if not relay.signed(body, self.headers.get("X-Relay-Sign", "")):
                    return self.error(401, "That sync isn't signed with this server's relay secret.")
                t = int_or(form.get("t"), 0)
                if abs(time.time() - t) > RELAY_SYNC_SKEW:
                    return self.error(401, "The relay's clock is out. Set it right (timedatectl).")
                relay.synced(form.get("hostkey", ""), form.get("busy", ""))
                text = relay.listing(t)
                return self.reply(200, text, cache="no-store",
                                  headers={"X-Relay-Sign": relay_sign(relay.secret, "list", text)})

            if parts[:2] == ["v1", "relay"] and len(parts) == 3 and parts[2] in ("status", "claim", "release"):
                if not relay or not relay.ready():
                    return self.reply(200, kv({"relay": "off", "message": "The supporter relay isn't open yet."}),
                                      cache="no-store")
                acc = state.by_token(form.get("token", ""))
                if not acc:
                    return self.error(401, "Your sign-in expired. Sign in again.")
                who = acc["name"].lower()
                if not relay.asks.allow(who):
                    return self.error(429, "That's a lot of asking. Wait a few minutes.")
                code, msg = 200, ""
                if parts[2] == "claim":
                    step, _ = relay.supporter(who, acc["name"])
                    if step != "ok":
                        return self.error(503 if step == "error" else 403, relay.message(step))
                    code, msg = relay.claim(who, form.get("name", "").strip().lower(), form.get("key", ""))
                elif parts[2] == "release":
                    code, msg = relay.release(who)
                if code != 200:
                    return self.error(code, msg)
                return self.reply(200, kv(relay.status(acc)), cache="no-store")

            if parts in (["v1", "relay", "off"], ["v1", "relay", "on"], ["v1", "relay", "check"]):
                if not self.is_admin():
                    return self.error(401, "Admin key required.")
                if not relay:
                    return self.error(404, "The supporter relay isn't set up here.")
                if parts[2] == "check":
                    found = relay.recheck(body.strip().lower())
                    if found is None:
                        return self.error(404, "No name like that.")
                    return self.reply(200, kv(found))
                if not relay.turn(body.strip().lower(), parts[2] == "on"):
                    return self.error(404, "No name like that is in use.")
                return self.reply(200, "ok\n")

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
                    if now - last_score.get(ip, -1e9) < SCORE_COOLDOWN_S:
                        return self.error(429, "Slow down a little.")
                    last_score[ip] = now
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


def serve():
    port = int(os.environ.get("XENITH_PORT", "8795"))
    proxy_port = int(os.environ.get("XENITH_PROXY_PORT", "0") or 0)
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
    with state.lock:                          # tells this server apart from another at that address
        if not state.data.get("server_id"):
            state.data["server_id"] = secrets.token_hex(8)
            state.save()
        server_id = state.data["server_id"]
    relay = None
    if os.environ.get("XENITH_RELAY_DOMAIN"):
        relay = Relay(state, os.environ.get("XENITH_RELAY_DOMAIN", ""), os.environ.get("XENITH_RELAY_SSH", ""),
                      os.environ.get("XENITH_RELAY_SECRET", ""), os.environ.get("XENITH_RELAY_USER", "hostmc"),
                      os.environ.get("XENITH_RELAY_PORTS", "30000-39999"), os.environ.get("XENITH_OCACCOUNT_URL", ""),
                      os.environ.get("XENITH_OCACCOUNT_KEY", ""), os.environ.get("XENITH_RELAY_PATREON", ""),
                      os.environ.get("XENITH_RELAY_ACCOUNT_URL", ""), os.environ.get("XENITH_RELAY_PLAYERS_PORT", "25565"))
        for problem in relay.problems():
            log(problem)
        if relay.problems():
            relay = None
        elif not relay.ocaccount_url:
            log("XENITH_OCACCOUNT_URL isn't set: nobody can be checked as a supporter, so no names are given out")

    def reaper():
        while True:
            time.sleep(30)
            tube.reap()
            if relay:
                try:
                    relay.tick()
                except Exception as e:            # keep the reaper going whatever happens
                    log(f"relay: {e!r}")
    threading.Thread(target=reaper, daemon=True).start()

    handler = make_handler(state, store, admin_key, web_dir, console_key, tube, public, downloads, lan, server_id,
                           relay)
    httpd = ThreadingHTTPServer(("0.0.0.0", port), handler)
    httpd.daemon_threads = True
    if proxy_port:
        relayed = ThreadingHTTPServer(("0.0.0.0", proxy_port), proxied(handler))
        relayed.daemon_threads = True
        threading.Thread(target=relayed.serve_forever, daemon=True).start()
    tube_mode = ("on, Deno and PO tokens when needed" if tube.deno and tube.plugins else
                 "on, Deno when needed" if tube.deno else "on") if tube.available() else "off"
    print(f"XenithOS Cloud {VERSION} on :{port}{f' (relay on :{proxy_port})' if proxy_port else ''} "
          f"(admin routes {'on' if admin_key else 'off'}, Tube {tube_mode}"
          f"{', consoles on its network use ' + lan if lan else ''}"
          f"{', supporter relay for *.' + relay.domain if relay else ''})", flush=True)
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
    code, body = req("GET", "/v1/lan")
    check("lan: this server's id and its home address", code == 200 and kvparse(body) == {"id": "5e4e1d", "lan": "192.168.1.20:8795"})
    check("lan address must be private and numeric",
          lan_addr_ok("192.168.50.145:8795") and lan_addr_ok("10.0.0.2:80") and not lan_addr_ok("37.1.2.3:8795")
          and not lan_addr_ok("keystone:8795") and not lan_addr_ok("192.168.1.2") and not lan_addr_ok("192.168.1.2:0"))

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

    # HostMC's supporter relay, with a stand-in OCAccounts that knows a few profiles
    members = {"sup1": {"linked": True, "patreon": True, "supporter": True, "tier": "Supporter"},
               "sup2": {"linked": True, "patreon": True, "supporter": True, "tier": "Big supporter"},
               "sup3": {"linked": True, "patreon": True, "supporter": True, "tier": "Supporter"},
               "sup4": {"linked": True, "patreon": True, "supporter": True, "tier": "Supporter"},
               "nopatreon": {"linked": True, "patreon": False, "supporter": False},
               "lowtier": {"linked": True, "patreon": True, "supporter": False, "tier": "Fan"}}
    oc = {"down": False, "asked": 0}

    class OCAccounts(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def do_GET(self):
            oc["asked"] += 1
            u = urlparse(self.path)
            who = parse_qs(u.query).get("profile", [""])[0].lower()
            if oc["down"]:
                code, out = 500, b"{}"
            elif u.path != "/api/partner/xenithos/supporter" or self.headers.get("X-Partner-Key") != "partn3r":
                code, out = 401, b"{}"
            elif who not in members:
                code, out = 404, b'{"linked": false}'
            else:
                code, out = 200, json.dumps(members[who]).encode()
            self.send_response(code)
            self.send_header("Content-Length", str(len(out)))
            self.end_headers()
            self.wfile.write(out)

    ocs = ThreadingHTTPServer(("127.0.0.1", 0), OCAccounts)
    threading.Thread(target=ocs.serve_forever, daemon=True).start()
    secret = secrets.token_hex(32)
    hostkey = "ssh-ed25519 " + base64.b64encode(RELAY_KEY_HEAD + os.urandom(32)).decode()

    def new_key():
        return "ssh-ed25519 " + base64.b64encode(RELAY_KEY_HEAD + os.urandom(32)).decode()

    check("supporter relay: off where it isn't set up", kvparse(req("POST", "/v1/relay/status", "token=x")[1]).get("relay") == "off"
          and req("POST", "/v1/relay/sync", "t=1")[0] == 404)
    check("supporter relay: settings checked", len(Relay(state, "not a domain", "", "short", ports="80-90").problems()) == 4)
    relay = Relay(state, "mc.example.com", "tunnel.mc.example.com:2222", secret, "hostmc", "30000-30002",
                  f"http://127.0.0.1:{ocs.server_address[1]}", "partn3r", "https://www.patreon.com/example")
    check("supporter relay: settings ok", relay.problems() == [] and relay.ssh_port == 2222)
    rs = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(state, store, "k3y", web_dir, relay=relay))
    threading.Thread(target=rs.serve_forever, daemon=True).start()

    def rreq(method, path, body=None, headers=None):
        r = urllib.request.Request(f"http://127.0.0.1:{rs.server_address[1]}{path}", method=method,
                                   data=body.encode() if body is not None else None, headers=headers or {})
        try:
            with urllib.request.urlopen(r) as resp:
                return resp.status, resp.read(), resp.headers
        except urllib.error.HTTPError as e:
            return e.code, e.read(), e.headers

    def sync(t=None, key=secret, busy=""):
        t = int(time.time()) if t is None else t
        b = urlencode({"t": t, "hostkey": hostkey + " root@relay", "busy": busy})
        return rreq("POST", "/v1/relay/sync", b, {"X-Relay-Sign": relay_sign(key, "sync", b)})

    def listing():
        code, b, h = sync()
        ok = code == 200 and hmac.compare_digest(relay_sign(secret, "list", b), h.get("X-Relay-Sign", ""))
        return b.decode().splitlines()[2:] if ok else None

    tokens = {}
    for who in ("Sup1", "Sup2", "Sup3", "Sup4", "Nolink", "Nopatreon", "Lowtier"):
        tokens[who.lower()] = state.create(who, "1234", "1202001000")[0][1]

    def ask(who, route="status", **form):
        code, b, _ = rreq("POST", f"/v1/relay/{route}", urlencode({"token": tokens[who], **form}))
        return code, kvparse(b)

    check("supporter relay: closed until the relay has said hello", ask("sup1")[1].get("relay") == "off")
    check("supporter relay: unsigned sync refused", rreq("POST", "/v1/relay/sync", "t=1")[0] == 401)
    check("supporter relay: sync with another secret refused", sync(key="x" * 64)[0] == 401)
    check("supporter relay: stale sync refused", sync(t=int(time.time()) - 600)[0] == 401)
    t0 = int(time.time())
    code, b, h = sync(t0)
    check("supporter relay: sync answered, signed, with the request's time",
          code == 200 and b.decode().startswith(f"t={t0}\ndomain=mc.example.com\n")
          and hmac.compare_digest(relay_sign(secret, "list", b), h.get("X-Relay-Sign", "")))
    check("supporter relay: the relay's host key kept, as a key alone", relay.data.get("hostkey") == hostkey and relay.ready())
    check("supporter relay: needs a profile", rreq("POST", "/v1/relay/status", "token=nope")[0] == 401)
    code, st = ask("nolink")
    check("supporter relay: profile not linked to an OCAccount", code == 200 and st.get("step") == "link"
          and "owencosta.diy/account" in st.get("message", "") and "name" not in st)
    check("supporter relay: no name without being a supporter", ask("nolink", "claim", name="sneaky", key=new_key())[0] == 403)
    check("supporter relay: Patreon not linked", ask("nopatreon")[1].get("step") == "patreon")
    check("supporter relay: not on the tier", ask("lowtier")[1].get("step") == "tier"
          and ask("lowtier", "claim", name="sneaky", key=new_key())[0] == 403)
    code, st = ask("sup1")
    check("supporter relay: a supporter, offered a free name", code == 200 and st.get("step") == "ok" and st.get("tier") == "Supporter"
          and relay_name_problem(st.get("suggest", "")) is None and "name" not in st
          and st.get("patreon") == "https://www.patreon.com/example")
    with open(os.path.join(tmp, "state", "relay-blocked-words.txt"), "w") as f:
        f.write("# words\nbadword\n")
    key1 = new_key()
    for bad in ("ab", "1abc", "a--b", "-abc", "abc-", "www", "a_b", "x" * 21, "my-badword", "my-b4dw0rd", "owen"):
        check(f"supporter relay: name refused: {bad}", ask("sup1", "claim", name=bad, key=key1)[0] == 400)
    for bad in ("", "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQ", "ssh-ed25519 AAAA",
                "ssh-ed25519 " + base64.b64encode(os.urandom(51)).decode()):
        check(f"supporter relay: key refused: {bad[:20]}", ask("sup1", "claim", name="brave-otter", key=bad)[0] == 400)
    code, st = ask("sup1", "claim", name="Brave-Otter", key=key1 + " me@pc\nrestrict,permitlisten=\"*\" " + new_key())
    check("supporter relay: a name given, with what HostMC needs", code == 200 and st.get("name") == "brave-otter"
          and st.get("state") == "on" and st.get("address") == "brave-otter.mc.example.com" and st.get("port") == "30000"
          and st.get("ssh_host") == "tunnel.mc.example.com" and st.get("ssh_port") == "2222"
          and st.get("ssh_user") == "hostmc" and st.get("hostkey") == hostkey and st.get("players_port") == "25565")
    relay.players_port, relay.at = 25566, ":25566"
    st = ask("sup1")[1]
    check("supporter relay: a players' port other than 25565 goes in the address",
          st.get("address") == "brave-otter.mc.example.com:25566" and st.get("players_port") == "25566"
          and Relay(state, "mc.example.com", "t.example.com:22", "s" * 40, players_port="0").problems()
          == ["XENITH_RELAY_PLAYERS_PORT must be the relay's port for players, like 25565"])
    relay.players_port, relay.at = 25565, ""
    check("supporter relay: the key kept as a key alone", relay.data["people"]["sup1"]["key"] == key1)
    check("supporter relay: the relay told", listing() == [f"on|brave-otter|30000|{key1}"])
    key2 = new_key()
    check("supporter relay: a name someone has is taken", ask("sup2", "claim", name="brave-otter", key=key2)[0] == 409)
    check("supporter relay: someone else's key refused", ask("sup2", "claim", name="other-name", key=key1)[0] == 409)
    code, st = ask("sup2", "claim", name="calm-heron", key=key2)
    check("supporter relay: each supporter their own port", code == 200 and st.get("port") == "30001")
    key1 = new_key()
    check("supporter relay: same name again takes a new key", ask("sup1", "claim", name="brave-otter", key=key1)[0] == 200
          and relay.data["people"]["sup1"]["key"] == key1)
    code, st = ask("sup1", "claim", name="swift-fox", key=key1)
    check("supporter relay: a new name, the old one held for them", code == 200 and st.get("name") == "swift-fox"
          and st.get("port") == "30000" and st.get("held") == "brave-otter"
          and listing() == ["held|brave-otter", f"on|calm-heron|30001|{key2}", f"on|swift-fox|30000|{key1}"])
    check("supporter relay: a held name is no one else's", ask("sup3", "claim", name="brave-otter", key=new_key())[0] == 409)
    check("supporter relay: theirs to take back", ask("sup1", "claim", name="brave-otter", key=key1)[1].get("name") == "brave-otter")
    check("supporter relay: a third new name today", ask("sup1", "claim", name="third-name", key=key1)[0] == 200)
    check("supporter relay: not a fourth", ask("sup1", "claim", name="fourth-name", key=key1)[0] == 429)
    check("supporter relay: one name each", sum(n["owner"] == "sup1" and n["state"] != "left"
                                      for n in relay.data["names"].values()) == 1)

    later = time.time() + RELAY_RECHECK + 1
    members["sup2"]["supporter"] = False
    relay.checks.clear()
    relay.tick(later)
    code, st = ask("sup2")
    check("supporter relay: membership ended: the relay stops, the name is kept", relay.data["names"]["calm-heron"]["state"] == "lapsed"
          and "held|calm-heron" in listing() and st.get("state") == "lapsed" and "kept for you until" in st.get("message", ""))
    members["sup2"]["supporter"] = True
    relay.checks.clear()
    check("supporter relay: back on when they come back", ask("sup2")[1].get("state") == "on" and f"on|calm-heron|30001|{key2}" in listing())
    oc["down"] = True
    relay.checks.clear()
    asked = oc["asked"]
    relay.tick(later + RELAY_RECHECK)
    relay.tick(later + RELAY_RECHECK + 60)
    check("supporter relay: OCAccounts down: nothing changes, and it isn't asked every minute",
          relay.data["names"]["third-name"]["state"] == "on" and oc["asked"] - asked == 2)
    check("supporter relay: OCAccounts down: said plainly", ask("sup1")[1].get("step") == "error"
          and ask("sup1", "claim", name="third-name", key=key1)[0] == 503)
    oc["down"] = False
    members["sup2"]["supporter"] = False
    relay.checks.clear()
    relay.tick(later + 3 * RELAY_RECHECK)
    relay.tick(later + 3 * RELAY_RECHECK + RELAY_HOLD_LAPSED)
    check("supporter relay: names freed when their time is up", sorted(relay.data["names"]) == ["third-name"]
          and sorted(relay.data["people"]) == ["sup1"])
    sync(busy="30000,30001,99")                    # sup2's old tunnel is still up on 30001
    check("supporter relay: a port with a tunnel left on it goes to no one else",
          ask("sup3", "claim", name="brave-otter", key=new_key())[1].get("port") == "30002" and relay.busy == {30000, 30001})
    sync(busy="30000")
    members["sup2"]["supporter"] = True
    relay.checks.clear()
    check("supporter relay: a freed name and port can be had again",
          ask("sup2", "claim", name="calm-heron", key=key2)[1].get("port") == "30001")
    check("supporter relay: full", ask("sup4", "claim", name="last-one", key=new_key())[0] == 503)
    code, st = ask("sup3", "release")
    check("supporter relay: a name given up", code == 200 and "name" not in st and st.get("held") == "brave-otter"
          and "held|brave-otter" in listing())
    check("supporter relay: list needs the admin key", rreq("GET", "/v1/relay/list")[0] == 401)
    sync(busy="30000")
    code, b, _ = rreq("GET", "/v1/relay/list", headers={"X-Xenith-Key": "k3y"})
    check("supporter relay: list, with whose tunnel is up", code == 200 and "\nthird-name|sup1|on|30000|0|ok|" in b.decode()
          and b.decode().splitlines()[-1].endswith("|up") and "\ncalm-heron|sup2|on|30001|0|ok|" in b.decode())
    check("supporter relay: turned off", rreq("POST", "/v1/relay/off", "third-name", {"X-Xenith-Key": "k3y"})[0] == 200
          and "held|third-name" in listing() and ask("sup1")[1].get("state") == "off")
    check("supporter relay: turned off: no way round it", ask("sup1", "claim", name="other-name", key=key1)[0] == 403
          and ask("sup1", "release")[0] == 403)
    check("supporter relay: on again", rreq("POST", "/v1/relay/on", "third-name", {"X-Xenith-Key": "k3y"})[0] == 200
          and f"on|third-name|30000|{key1}" in listing())
    check("supporter relay: off needs a name in use", rreq("POST", "/v1/relay/off", "nobody", {"X-Xenith-Key": "k3y"})[0] == 404)
    members["sup1"]["supporter"] = False
    code, b, _ = rreq("POST", "/v1/relay/check", "third-name", {"X-Xenith-Key": "k3y"})
    check("supporter relay: checked now, on the admin's word", code == 200 and kvparse(b) == {"step": "tier", "tier": "Supporter",
                                                                                            "state": "lapsed"}
          and rreq("POST", "/v1/relay/check", "third-name")[0] == 401)
    members["sup1"]["supporter"] = True
    check("supporter relay: and back", kvparse(rreq("POST", "/v1/relay/check", "third-name", {"X-Xenith-Key": "k3y"})[1])
          .get("state") == "on")
    rs.shutdown()
    ocs.shutdown()

    reloaded = State(os.path.join(tmp, "state"))
    check("state persists", reloaded.data["motd"] == "Game night 9PM"
          and reloaded.account("tuber")["avatar"] == "29d97979ab4421"
          and reloaded.data["relay"]["names"]["third-name"]["owner"] == "sup1")

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
    if (len(args) == 2 and args[0] in ("send", "motd")) or args == ["relay", "list"] or \
            (len(args) == 3 and args[:2] in (["relay", "off"], ["relay", "on"], ["relay", "check"])):
        # Talk to the running server so its in-memory state stays the source of truth.
        key = os.environ.get("XENITH_ADMIN_KEY", "")
        if not key:
            sys.exit("XENITH_ADMIN_KEY is not set (on the VPS use the `xenith` wrapper)")
        port = os.environ.get("XENITH_PORT", "8795")
        if args[0] == "relay":
            path, data = f"/v1/relay/{args[1]}", (args[2].encode() if len(args) == 3 else None)
        else:
            path, data = ("/v1/inbox" if args[0] == "send" else "/v1/motd"), args[1].encode()
        r = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=data,
                                   method="POST" if data is not None else "GET", headers={"X-Xenith-Key": key})
        try:
            with urllib.request.urlopen(r, timeout=5) as resp:
                text = resp.read().decode().strip()
        except urllib.error.HTTPError as e:
            sys.exit(e.read().decode().strip().replace("error=", "", 1))
        except (urllib.error.URLError, OSError) as e:
            sys.exit(f"server not reachable: {e}")
        if args[:2] == ["relay", "list"]:
            print(text or "no names yet")
        elif args[:2] == ["relay", "check"]:
            print(text)
        else:
            print({"send": "sent", "motd": "motd set", "relay": f"{args[2]} turned {args[1]}"}[args[0]],
                  text if args[0] != "relay" else "")
        sys.exit(0)
    if args:
        print(__doc__)
        sys.exit(2)
    serve()
