"""XenithOS Console's updates from the desktop, done the way XenithOS's own Store does them on a
PC: the "xenithos-pc" item in XenithOS Cloud's Store is the new source, its SHA-256 is checked,
it's unpacked in ~/.cache/xenithos-update/src, and the linux/update.sh that comes with it builds
the programs and puts them in /usr/lib/xenithos (which is the XenithOS account's, so no sudo).
Settings > Updates uses this so one button updates everything.
"""
import hashlib
import os
import shutil
import subprocess
import tarfile
import urllib.request

DATA = os.environ.get("XENITHOS_DATA") or os.path.join(os.path.expanduser("~"), ".local", "share", "xenithos")
CONFIG = os.path.join(DATA, "uda:", "xenith", "config.txt")
WORK = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache"), "xenithos-update")
VERSION_TOOL = "/usr/lib/xenithos-desktop/console-version"


def server():
    try:
        for line in open(CONFIG):
            if line.startswith("server="):
                return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""


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


def check():
    """What the Store has: {"installed", "available", "newer", "sha256", "server"}, or None when
    XenithOS Cloud can't be reached (or isn't set up)."""
    addr = server()
    have = installed()
    if not addr or not have:
        return None
    try:
        with urllib.request.urlopen(f"http://{addr}/v1/store", timeout=8) as r:
            rows = r.read().decode("utf-8", "replace").splitlines()
    except (OSError, ValueError):
        return None
    for row in rows:
        f = row.split("|")
        if len(f) >= 7 and f[0] == "xenithos-pc" and f[1] == "pc":
            return {"installed": have, "available": f[3], "newer": number(f[3]) > number(have),
                    "sha256": f[5].lower(), "server": addr}
    return None


def update(info):
    """Downloads, checks, builds and installs. Returns "" when it worked, or what went wrong."""
    try:
        os.makedirs(WORK, exist_ok=True)
        tarball = os.path.join(WORK, "xenithos-pc.tar.gz")
        digest = hashlib.sha256()
        with urllib.request.urlopen(f"http://{info['server']}/v1/store/file/xenithos-pc", timeout=30) as r, \
                open(tarball, "wb") as out:
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
    except (OSError, tarfile.TarError, subprocess.TimeoutExpired, KeyError) as e:
        return f"XenithOS Console couldn't be updated: {e}"
