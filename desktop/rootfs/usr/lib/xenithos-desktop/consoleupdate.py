"""XenithOS Console's updates from the desktop, done the way XenithOS's own Store does them on a
PC: the "xenithos-pc" item in XenithOS Cloud's Store is the new source, its SHA-256 is checked,
it's unpacked in ~/.cache/xenithos-update/src, and the linux/update.sh that comes with it builds
the programs and puts them in /usr/lib/xenithos (which is the XenithOS account's, so no sudo).
Settings > Updates uses this so one button updates everything.

The Store's list has to come signed by XenithOS Cloud, as XenithOS Console checks it since 0.7.0
(source/store_sig.c): the SHA-256 that checks the download is in that list, so a list that isn't
signed with XenithOS Cloud's key is never used. Only a newer version is ever installed, so an old
signed list played back can't take XenithOS Console back to an older version either.

XenithOS Cloud is reached the way XenithOS Console reaches it, from what it wrote in route.txt at
its last start: its address on this network first, then its HTTPS address, then the address in
config.txt. The signature, not the way in, is what makes a list trustworthy, so any of them will do.
"""
import hashlib
import http.client
import os
import re
import shutil
import subprocess
import tarfile
import time
import urllib.request

DATA = os.environ.get("XENITHOS_DATA") or os.path.join(os.path.expanduser("~"), ".local", "share", "xenithos")
CONFIG = os.path.join(DATA, "uda:", "xenith", "config.txt")
ROUTE = os.path.join(DATA, "uda:", "xenith", "route.txt")
WORK = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache"), "xenithos-update")
CHECK_LOG = os.path.join(WORK, "last-check.txt")
VERSION_TOOL = "/usr/lib/xenithos-desktop/console-version"
# As XenithOS Console introduces itself: Cloudflare, in front of the HTTPS address, turns
# Python's own User-Agent away.
USER_AGENT = "XenithOS/0.4 (XenithOS Desktop; console updates)"
# XenithOS Cloud's Store key, the public half: the same as in XenithOS Console's source/store_key.h.
STORE_KEYS = ("6ac2e8d5bd25ccc7478843b2270d572ab7490ed9c9ab2bbdecda2e50a171026b",)
LAN_RE = re.compile(r"^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:\d{1,5}$")


def read_kv(path):
    found = {}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                key, sep, value = line.strip().partition("=")
                if sep and not key.startswith("#"):
                    found[key] = value.strip()
    except OSError:
        pass
    return found


def addresses():
    """Where to ask XenithOS Cloud, best first, as base addresses ("http://...", "https://...")."""
    main = read_kv(CONFIG).get("server", "")
    if not main:
        return []
    route = read_kv(ROUTE)
    found = []
    if route.get("for") == main:                    # written for this XenithOS Cloud
        if route.get("id") and LAN_RE.match(route.get("lan", "")):
            found.append("http://" + route["lan"])
        if route.get("https", "").startswith("https://"):
            found.append(route["https"].rstrip("/"))
    found.append((main if main.startswith(("http://", "https://")) else "http://" + main).rstrip("/"))
    return list(dict.fromkeys(found))


def fetch(url, timeout):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": USER_AGENT}), timeout=timeout)


def installed():
    try:
        out = subprocess.run([VERSION_TOOL], capture_output=True, text=True, timeout=10).stdout.strip()
        return out if out and out[0].isdigit() else ""
    except (OSError, subprocess.TimeoutExpired):
        return ""


def number(version):
    parts = []
    for p in version.split("."):
        parts.append(int(p) if p.isdigit() else 0)
    return tuple(parts)


# ---------------------------------------------------------------------- the Store's signature
# Copied as they are from server/xenith_cloud.py, which signs the Store's replies with them:
#     X-Xenith-Sig: 1 <key id> <signature>
# the key id the first 16 hex digits of the public key, the signature 128 hex digits over
#     XenithOS Store 1\n<name>\n<SHA-256 of the body, in hex>\n
# where the name says which reply it is ("store" for /v1/store). ed25519 after RFC 8032, since
# Python's standard library has none; it only checks here, which uses nothing secret.

ED_P = 2 ** 255 - 19
ED_L = 2 ** 252 + 27742317777372353535851937790883648493
ED_D = -121665 * pow(121666, ED_P - 2, ED_P) % ED_P
ED_I = pow(2, (ED_P - 1) // 4, ED_P)                   # a square root of -1
ED_ZERO = (0, 1, 1, 0)
STORE_SIG_HEADER = "X-Xenith-Sig"


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


def store_sig_text(name, body):
    """What a signature covers: the reply's name and its body's SHA-256."""
    return f"XenithOS Store 1\n{name}\n{hashlib.sha256(body).hexdigest()}\n".encode()


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


# ---------------------------------------------------------------------- checking and updating

def remember(lines):
    """What the last check found at each address, for whoever wonders why no update showed up."""
    try:
        os.makedirs(WORK, exist_ok=True)
        with open(CHECK_LOG, "w", encoding="utf-8") as f:
            f.write(time.strftime("%Y-%m-%d %H:%M:%S\n") + "".join(line + "\n" for line in lines))
    except OSError:
        pass


def check():
    """What the Store has: {"installed", "available", "newer", "sha256", "base"}, or None when
    XenithOS Cloud can't be reached (or isn't set up), or its list didn't come signed by it."""
    places = addresses()
    have = installed()
    if not places or not have:
        return None
    said = []
    for base in places:
        try:
            with fetch(base + "/v1/store", 8) as r:
                body, values = r.read(), r.headers.get_all(STORE_SIG_HEADER) or []
        except (OSError, ValueError, http.client.HTTPException) as e:
            said.append(f"{base}: {e}")
            continue
        if not store_sig_good(values, "store", body, STORE_KEYS):
            said.append(f"{base}: the Store's list didn't come signed by XenithOS Cloud, so it wasn't used")
            continue
        remember(said + [f"{base}: the Store's list, signed by XenithOS Cloud"])
        for row in body.decode("utf-8", "replace").splitlines():
            f = row.split("|")
            if len(f) >= 7 and f[0] == "xenithos-pc" and f[1] == "pc":
                return {"installed": have, "available": f[3], "newer": number(f[3]) > number(have),
                        "sha256": f[5].lower(), "base": base}
        return None
    remember(said)
    return None


def update(info):
    """Downloads, checks, builds and installs. Returns "" when it worked, or what went wrong."""
    try:
        if not number(info["available"]) > number(installed() or info["installed"]):
            return "XenithOS Console is up to date already."
        os.makedirs(WORK, exist_ok=True)
        tarball = os.path.join(WORK, "xenithos-pc.tar.gz")
        digest = hashlib.sha256()
        with fetch(f"{info['base']}/v1/store/file/xenithos-pc", 30) as r, open(tarball, "wb") as out:
            while True:
                block = r.read(65536)
                if not block:
                    break
                digest.update(block)
                out.write(block)
        if digest.hexdigest() != info["sha256"]:
            return "XenithOS Console's download came out wrong. Try again."
        src = os.path.join(WORK, "src")
        shutil.rmtree(src, ignore_errors=True)
        os.makedirs(src)
        with tarfile.open(tarball) as tar:
            for member in tar.getmembers():
                # like tar --strip-components=1, and nothing that would land outside src
                _, _, rest = member.name.partition("/")
                if not rest or rest.startswith("/") or ".." in rest.split("/"):
                    continue
                if not (member.isfile() or member.isdir()):
                    continue
                member.name = rest
                tar.extract(member, src)
        script = os.path.join(src, "linux", "update.sh")
        if not os.path.isfile(script):
            return "XenithOS Console's download has no update in it."
        log = os.path.join(DATA, "uda:", "xenith", "update.log")
        os.makedirs(os.path.dirname(log), exist_ok=True)
        with open(log, "w") as out:
            code = subprocess.run(["bash", script, DATA], stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                                  timeout=1800).returncode
        if code == 3:
            return "XenithOS Console's folder isn't this account's, so it couldn't be updated."
        if code != 0:
            return f"XenithOS Console didn't build. What it said is in {log}. The old version is still there."
        # The update put every app back on the home screen: off again with the ones this
        # person hasn't installed (Settings > Console apps).
        try:
            subprocess.run([os.path.join(os.path.dirname(os.path.abspath(__file__)), "console-apps"), "sync"],
                           stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
        except (OSError, subprocess.TimeoutExpired):
            pass                                    # the session does it again before XenithOS Console starts
        return ""
    except (OSError, ValueError, http.client.HTTPException, tarfile.TarError, subprocess.TimeoutExpired,
            KeyError) as e:
        return f"XenithOS Console couldn't be updated: {e}"
