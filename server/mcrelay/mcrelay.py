#!/usr/bin/env python3
"""mcrelay: HostMC's supporter relay, on a machine of its own.

Friends type <name>.<domain> into Minecraft: Java Edition, and a wildcard DNS record brings
them here. The game names the address it was given in its first packet (the handshake), so
one port serves every name: the router reads it and passes the connection to that
supporter's HostMC, down the SSH tunnel HostMC keeps open to this machine.

Three parts, each run as its own service (setup.sh sets them up):

  mcrelay.py router   players, on MCRELAY_LISTEN. Names come from the routes file the sync
                      writes. Players' addresses are never logged or kept.
  mcrelay.py sync     every 30 seconds asks XenithOS Cloud (/v1/relay/sync) for the names,
                      their ports on this machine's loopback and the keys supporters' HostMC log
                      in with, both ways signed with the secret the two share. It writes the
                      routes and the tunnel account's authorized_keys, and tells XenithOS Cloud
                      which ports have a tunnel on them (busy_ports). It runs as the tunnel
                      account, which owns those keys and tunnels. When XenithOS Cloud doesn't
                      answer, the names from before stay.
  sshd                tunnels only, with the settings `mcrelay.py sshd-config` writes: its own
                      port, one account, keys only, remote forwards only (ssh -R), each key to
                      its own port on the loopback (permitlisten), no shell or anything else.

Standard library only.
  MCRELAY_LISTEN       default 0.0.0.0:25565
  MCRELAY_DIR          default /var/lib/mcrelay       routes, authorized_keys
  MCRELAY_CLOUD        XenithOS Cloud's address, e.g. http://cloud.example.com:8795
  MCRELAY_SECRET_FILE  default /etc/mcrelay/secret    the secret shared with XenithOS Cloud
  MCRELAY_HOSTKEY      default /etc/mcrelay/ssh_host_ed25519_key.pub   told to XenithOS Cloud,
                       which gives it to HostMC to check this machine by
  MCRELAY_EVERY        default 30: seconds between syncs
  MCRELAY_MAX          default 2000 connections at once; MCRELAY_PER_IP default 12 at once from
                       one address, MCRELAY_RATE default 40 new ones from one address in 10 s;
                       MCRELAY_PER_NAME default 200 at once to one server
  mcrelay.py sshd-config <port> <user> <authorized_keys> <host key> <pid file>
  mcrelay.py --selftest
"""
import asyncio
import base64
import hashlib
import hmac
import json
import os
import re
import socket
import sys
import time
import urllib.error
import urllib.request
from urllib.parse import urlencode

NAME_RE = re.compile(r"^[a-z][a-z0-9-]{1,18}[a-z0-9]$")
DOMAIN_RE = re.compile(r"^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z]{2,63}$")
KEY_HEAD = b"\0\0\0\x0bssh-ed25519\0\0\0\x20"
CHUNK = 65536
HANDSHAKE_TIMEOUT = 5          # seconds for a game to say which server it wants
MAX_HANDSHAKE = 1100           # bytes: a handshake is a few hundred at most
SYNC_EVERY = 30
LOOPBACK = {"0100007F", "00000000000000000000000001000000"}   # 127.0.0.1 and ::1 in /proc/net/tcp*


def log(message):
    print(f"mcrelay: {message}", flush=True)


def address(text, default_port):
    host, _, port = text.rpartition(":")
    return host.strip("[]") or "0.0.0.0", int(port or default_port)


def plain_ip(ip):
    """An IPv4 address that arrived as IPv6 ("::ffff:1.2.3.4") is just IPv4."""
    return ip[7:] if ip.startswith("::ffff:") and "." in ip else ip


def ssh_key(text):
    """An ssh-ed25519 public key, rebuilt from its bytes, or "" (the same check XenithOS Cloud
    makes, made again here since these lines go into authorized_keys)."""
    parts = (text or "").split()
    if len(parts) < 2 or parts[0] != "ssh-ed25519" or len(parts[1]) != 68:
        return ""
    try:
        blob = base64.b64decode(parts[1], validate=True)
    except ValueError:
        return ""
    if len(blob) != 51 or not blob.startswith(KEY_HEAD):
        return ""
    return "ssh-ed25519 " + base64.b64encode(blob).decode()


def sign(secret, kind, body):
    """HMAC-SHA256 of a sync request (kind "sync") or of its answer ("list"), as XenithOS Cloud
    makes it (relay_sign in xenith_cloud.py)."""
    if isinstance(body, str):
        body = body.encode()
    return hmac.new(secret.encode(), kind.encode() + b"\n" + body, hashlib.sha256).hexdigest()


# ---------------------------------------------------------------------- the game's first packets

class More(ValueError):
    """The bytes so far stop in the middle of something."""


def read_varint(buf, pos):
    value = 0
    for i in range(5):
        if pos >= len(buf):
            raise More
        b = buf[pos]
        pos += 1
        value |= (b & 0x7F) << (7 * i)
        if not b & 0x80:
            return (value - (1 << 32) if value >= 1 << 31 else value), pos
    raise ValueError("a number that never ends")


def varint(n):
    n &= 0xFFFFFFFF
    out = bytearray()
    while True:
        b, n = n & 0x7F, n >> 7
        out.append(b | 0x80 if n else b)
        if not n:
            return bytes(out)


def packet(pid, payload=b""):
    body = varint(pid) + payload
    return varint(len(body)) + body


def mc_string(text):
    data = text.encode("utf-8")
    return varint(len(data)) + data


def handshake(buf):
    """(address, next state, bytes it took) from the start of a connection, or None while more
    bytes are needed. ValueError for anything that isn't a Minecraft: Java Edition handshake."""
    if buf[:1] == b"\xfe":
        raise ValueError("a server list ping from Minecraft 1.6 or older")
    try:                                                 # each part checked as soon as it's in
        length, pos = read_varint(buf, 0)
        if not 6 <= length <= MAX_HANDSHAKE:
            raise ValueError("not a handshake")
        end = pos + length
        pid, p = read_varint(buf, pos)
        if pid != 0:
            raise ValueError("not a handshake")
        _, p = read_varint(buf, p)                       # the game's protocol version
        size, p = read_varint(buf, p)
        if not 0 <= size <= 1020 or p + size + 3 > end:
            raise ValueError("not a handshake")
        if len(buf) < end:
            return None
        host = buf[p:p + size].decode("utf-8", "replace")
        state, p = read_varint(buf, p + size + 2)        # after the port the game was told
    except More:
        return None
    if p > end or state not in (1, 2, 3):
        raise ValueError("not a handshake")
    return host, state, end


def name_in(host, domain):
    """The supporter's name from the address a player typed, <name>.<domain> in any case, with a
    dot at the end or what Forge adds after a zero byte; "" for anything else."""
    host = host.split("\0", 1)[0].strip().rstrip(".").lower()
    if not domain or not host.endswith("." + domain):
        return ""
    name = host[:-len(domain) - 1]
    return name if NAME_RE.match(name) else ""


def status_packet(text, version):
    """A server list answer: the text where the server's message goes, and the version shown in
    red in place of the players (protocol -1 matches no game)."""
    return packet(0, mc_string(json.dumps({"version": {"name": version, "protocol": -1},
                                           "players": {"max": 0, "online": 0},
                                           "description": {"text": text}})))


def disconnect_packet(text):
    return packet(0, mc_string(json.dumps({"text": text})))


# ---------------------------------------------------------------------- router

def read_routes(path):
    """(domain, {name: port}, {held names}) from the file the sync writes."""
    domain, on, held = "", {}, set()
    with open(path, encoding="ascii", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("domain="):
                domain = line[7:] if DOMAIN_RE.match(line[7:]) else ""
                continue
            f = line.split("|")
            if len(f) == 3 and f[0] == "on" and NAME_RE.match(f[1]) and f[2].isdigit() and 1024 < int(f[2]) < 65536:
                on[f[1]] = int(f[2])
            elif len(f) == 2 and f[0] == "held" and NAME_RE.match(f[1]):
                held.add(f[1])
    return domain, on, held


async def pipe(reader, writer):
    """Copies until the sender is done, then says so (a half-close)."""
    try:
        while True:
            data = await reader.read(CHUNK)
            if not data:
                break
            writer.write(data)
            await writer.drain()
        if writer.can_write_eof():
            writer.write_eof()
    except (ConnectionError, OSError):
        pass


class Router:
    def __init__(self, routes, limit=2000, per_ip=12, rate=40, per_name=200):
        self.path, self.port = routes, 25565
        self.domain, self.on, self.held, self.seen = "", {}, set(), None
        self.limit, self.per_ip, self.rate, self.per_name = limit, per_ip, rate, per_name
        self.active, self.by_ip, self.by_name, self.recent = 0, {}, {}, {}

    def reload(self):
        try:
            st = os.stat(self.path)
            if (st.st_mtime_ns, st.st_size) == self.seen:
                return
            self.domain, self.on, self.held = read_routes(self.path)
            self.seen = (st.st_mtime_ns, st.st_size)
        except OSError:
            return
        log(f"{len(self.on)} names on, {len(self.held)} held, under {self.domain or '(no domain yet)'}")

    async def watch(self):
        while True:
            self.reload()
            await asyncio.sleep(2)

    def allow(self, ip):
        """Fewer than `rate` new connections from one address in the last 10 seconds."""
        now = time.monotonic()
        recent = [t for t in self.recent.get(ip, ()) if now - t < 10]
        ok = len(recent) < self.rate
        if ok:
            recent.append(now)
        self.recent[ip] = recent
        if len(self.recent) > 20000:
            self.recent = {k: v for k, v in self.recent.items() if v and now - v[-1] < 10}
        return ok

    async def handle(self, reader, writer):
        peer = writer.get_extra_info("peername") or ("?", 0)
        ip = plain_ip(str(peer[0]))
        if self.active >= self.limit or self.by_ip.get(ip, 0) >= self.per_ip or not self.allow(ip):
            writer.close()
            return
        self.active += 1
        self.by_ip[ip] = self.by_ip.get(ip, 0) + 1
        try:
            await self.serve(reader, writer)
        except (ConnectionError, OSError, asyncio.TimeoutError, ValueError):
            pass
        finally:
            self.active -= 1
            self.by_ip[ip] -= 1
            if not self.by_ip[ip]:
                del self.by_ip[ip]
            try:
                writer.close()
            except (ConnectionError, OSError):
                pass

    async def serve(self, reader, writer):
        loop = asyncio.get_running_loop()
        deadline, buf = loop.time() + HANDSHAKE_TIMEOUT, b""
        while True:
            got = handshake(buf)
            if got:
                break
            left = deadline - loop.time()
            if left <= 0:
                return
            data = await asyncio.wait_for(reader.read(2048), left)
            if not data:
                return
            buf += data
        host, state, used = got
        name = name_in(host, self.domain)
        port = self.on.get(name)
        if not port:
            if name in self.held:
                return await self.refuse(reader, writer, state, buf[used:], f"{name} isn't available right now.",
                                         "Unavailable")
            if name:
                return await self.refuse(reader, writer, state, buf[used:], f"There's no server called {name} here.",
                                         "No server")
            at = "" if self.port == 25565 else f":{self.port}"
            return await self.refuse(reader, writer, state, buf[used:],
                                     f"Type the server's name to join, like name.{self.domain or 'example.com'}{at}",
                                     "No server")
        if self.by_name.get(name, 0) >= self.per_name:
            return await self.refuse(reader, writer, state, buf[used:], f"{name} is full right now. Try again soon.",
                                     "Busy")
        try:
            up_r, up_w = await asyncio.wait_for(asyncio.open_connection("127.0.0.1", port), 5)
        except (OSError, asyncio.TimeoutError):
            return await self.refuse(reader, writer, state, buf[used:], f"{name} is offline right now.", "Offline")
        self.by_name[name] = self.by_name.get(name, 0) + 1
        try:
            up_w.write(buf)                            # the handshake and anything after it, as sent
            await asyncio.gather(pipe(reader, up_w), pipe(up_r, writer))
        finally:
            up_w.close()
            self.by_name[name] -= 1
            if not self.by_name[name]:
                del self.by_name[name]

    async def refuse(self, reader, writer, state, rest, text, version):
        """Says why, the way the game shows it: in the server list, or as the reason a join failed."""
        loop = asyncio.get_running_loop()
        if state != 1:
            writer.write(disconnect_packet(text))
            await writer.drain()
            if writer.can_write_eof():
                writer.write_eof()                     # so the game reads it before the connection ends
            deadline = loop.time() + 3
            while loop.time() < deadline and await asyncio.wait_for(reader.read(4096), deadline - loop.time()):
                pass
            return
        deadline, buf = loop.time() + HANDSHAKE_TIMEOUT, rest
        for _ in range(4):                             # the status request, then the ping
            while True:
                try:
                    length, pos = read_varint(buf, 0)
                    if not 1 <= length <= 64:
                        return
                    if len(buf) < pos + length:
                        raise More
                    break
                except More:
                    if len(buf) > 70:
                        return
                    data = await asyncio.wait_for(reader.read(512), max(0.01, deadline - loop.time()))
                    if not data:
                        return
                    buf += data
            pid, p = read_varint(buf, pos)
            body, buf = buf[p:pos + length], buf[pos + length:]
            if pid == 0 and not body:
                writer.write(status_packet(text, version))
            elif pid == 1 and len(body) == 8:
                writer.write(packet(1, body))
                await writer.drain()
                return
            else:
                return
            await writer.drain()


async def run_router(listen, routes, limits, ready=None):
    router = Router(routes, *limits)
    router.reload()
    server = await asyncio.start_server(router.handle, listen[0], listen[1], reuse_address=True)
    router.port = server.sockets[0].getsockname()[1]
    asyncio.get_running_loop().create_task(router.watch())
    if ready is not None:
        ready.append(server.sockets[0].getsockname()[1])
    else:
        log(f"players on {listen[0]}:{listen[1]}")
    async with server:
        await server.serve_forever()


# ---------------------------------------------------------------------- sync

def fetch(cloud, secret, hostkey, busy=(), timeout=15):
    """The names from XenithOS Cloud: (domain, [(name, port, key)], [held names]). It's told this
    machine's SSH host key and the ports with a tunnel on them. OSError or ValueError when
    there's no answer to go by."""
    t = int(time.time())
    body = urlencode({"t": t, "hostkey": hostkey, "busy": ",".join(map(str, busy))})
    req = urllib.request.Request(cloud.rstrip("/") + "/v1/relay/sync", data=body.encode(), method="POST",
                                 headers={"X-Relay-Sign": sign(secret, "sync", body), "User-Agent": "mcrelay/1",
                                          "Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            text, signature = resp.read(8 << 20), resp.headers.get("X-Relay-Sign", "")
    except urllib.error.HTTPError as e:
        why = e.read(300).decode("ascii", "replace").strip().replace("error=", "", 1)
        raise OSError(f"XenithOS Cloud said {e.code}: {why}") from None
    if not hmac.compare_digest(sign(secret, "list", text).encode(), signature.strip().encode()):
        raise ValueError("the answer wasn't signed with this relay's secret")
    lines = text.decode("ascii", "replace").splitlines()
    if not lines or lines[0] != f"t={t}":
        raise ValueError("the answer was to another request")
    domain, on, held, keys, ports = "", [], [], set(), set()
    for line in lines[1:]:
        if line.startswith("domain="):
            domain = line[7:]
            continue
        f = line.split("|")
        if len(f) == 4 and f[0] == "on" and NAME_RE.match(f[1]) and f[2].isdigit() and 1024 < int(f[2]) < 65536:
            key = ssh_key(f[3])
            if key and key not in keys and int(f[2]) not in ports:
                keys.add(key)
                ports.add(int(f[2]))
                on.append((f[1], int(f[2]), key))
        elif len(f) == 2 and f[0] == "held" and NAME_RE.match(f[1]):
            held.append(f[1])
    if not DOMAIN_RE.match(domain):
        raise ValueError("the answer had no domain")
    return domain, on, held


def write_file(path, text, mode):
    """Writes text to path in one go (a new file, then renamed). False if it was that already."""
    try:
        with open(path, encoding="ascii") as f:
            if f.read() == text:
                return False
    except OSError:
        pass
    tmp = path + ".new"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, mode)
    with os.fdopen(fd, "w", encoding="ascii") as f:
        f.write(text)
    os.chmod(tmp, mode)
    os.replace(tmp, path)
    return True


def write_lists(folder, domain, on, held):
    """The router's routes, and the tunnel account's authorized_keys: each key may only open its
    own port, on the loopback, and do nothing else."""
    routes = f"domain={domain}\n" + "".join(f"on|{n}|{p}\n" for n, p, _ in on) + "".join(f"held|{n}\n" for n in held)
    keys = "# HostMC's tunnels, written by mcrelay sync from XenithOS Cloud's list. Changes here don't last.\n" + \
        "".join(f'restrict,port-forwarding,permitlisten="127.0.0.1:{p}" {k} {n}\n' for n, p, k in on)
    changed = write_file(os.path.join(folder, "authorized_keys"), keys, 0o600)
    return write_file(os.path.join(folder, "routes"), routes, 0o644) or changed


def busy_ports():
    """The ports this account (the tunnel account) listens on, on the loopback: tunnels that are
    up. One left up from a name that's no longer on is never sent players (the router knows only
    names that are on), and XenithOS Cloud gives its port to no one else while it lasts. Such a
    tunnel can't come back once it drops, since its key is gone."""
    me, out = str(os.getuid()), set()
    for path in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            with open(path) as f:
                rows = f.read().splitlines()[1:]
        except OSError:
            continue
        for row in rows:
            f = row.split()
            if len(f) > 9 and f[3] == "0A" and f[7] == me:      # listening, and this account's
                where, port = f[1].rsplit(":", 1)
                if where in LOOPBACK:
                    out.add(int(port, 16))
    return sorted(out)


def run_sync(cloud, secret, hostkey_path, folder, once=False, every=SYNC_EVERY):
    told = None
    while True:
        try:
            with open(hostkey_path, encoding="ascii") as f:
                hostkey = f.readline().strip()
        except OSError:
            hostkey = ""
        try:
            domain, on, held = fetch(cloud, secret, hostkey, busy_ports())
            if write_lists(folder, domain, on, held) or told != "ok":
                log(f"{len(on)} names on, {len(held)} held")
            told = "ok"
        except (OSError, ValueError) as e:
            if told != str(e):
                log(f"no list from XenithOS Cloud ({e}); the names from before stay")
            told = str(e)
        if once:
            return told == "ok"
        time.sleep(every)


# ---------------------------------------------------------------------- the tunnels' sshd

def sshd_config(port, user, keys, hostkey, pidfile, strict=True):
    return f"""# sshd for HostMC's tunnels to this relay, and nothing else (written by mcrelay.py sshd-config).
Port {port}
HostKey {hostkey}
PidFile {pidfile}
AllowUsers {user}
AuthorizedKeysFile {keys}
AuthenticationMethods publickey
PubkeyAuthentication yes
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin no
UsePAM no
StrictModes {'yes' if strict else 'no'}
# Remote forwards (ssh -R) only, each key to its own port (permitlisten in authorized_keys),
# always on this machine's loopback. No shell, no files, no way out to anywhere.
AllowTcpForwarding remote
PermitOpen none
GatewayPorts no
AllowAgentForwarding no
AllowStreamLocalForwarding no
X11Forwarding no
PermitTunnel no
PermitTTY no
PermitUserRC no
PermitUserEnvironment no
MaxSessions 0
ForceCommand /bin/false
# A tunnel that goes quiet is closed within a minute, so its port is free for its next try.
ClientAliveInterval 15
ClientAliveCountMax 3
LoginGraceTime 20
MaxAuthTries 3
MaxStartups 20:30:100
LogLevel INFO
"""


# ---------------------------------------------------------------------- selftest

def selftest():
    import tempfile
    import threading
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
    from urllib.parse import parse_qs

    checks = []

    def check(name, ok):
        checks.append((name, bool(ok)))

    def shake(host, state=2, proto=767, port=25565):
        return packet(0, varint(proto) + mc_string(host) + port.to_bytes(2, "big") + varint(state))

    # The game's packets
    check("numbers both ways", all(read_varint(varint(n), 0)[0] == n for n in (0, 1, 127, 128, 25565, 2 ** 31 - 1, -1)))
    hs = shake("Brave-Otter.MC.Example.com.")
    check("handshake read", handshake(hs + b"\x02\x00\x00") == ("Brave-Otter.MC.Example.com.", 2, len(hs)))
    check("handshake in pieces", handshake(hs[:3]) is None and handshake(hs[:-1]) is None)
    check("not a handshake", all(_raises(handshake, b) for b in (b"\xfe\x01", b"GET / HTTP/1.1\r\n\r\n",
                                                                  packet(5, b"xxxxxx"), b"\xff\xff\xff\xff\xff\xff")))
    check("names from what players type", name_in("Brave-Otter.MC.Example.com.", "mc.example.com") == "brave-otter"
          and name_in("brave-otter.mc.example.com\0FML3\0", "mc.example.com") == "brave-otter"
          and name_in("mc.example.com", "mc.example.com") == "" and name_in("a.b.mc.example.com", "mc.example.com") == ""
          and name_in("brave-otter.mc.example.org", "mc.example.com") == "" and name_in("1.2.3.4", "mc.example.com") == "")

    # The router, with the sync's routes file and stand-in servers
    tmp = tempfile.mkdtemp()
    servers = []

    def stand_in():
        """A server that notes what it's sent and answers "hello" after the first bytes."""
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        s.listen(8)
        got = []

        def serve():
            while True:
                try:
                    conn, _ = s.accept()
                except OSError:
                    return
                with conn:
                    data = conn.recv(65536)
                    got.append(data)
                    conn.sendall(b"hello from " + str(s.getsockname()[1]).encode())
        threading.Thread(target=serve, daemon=True).start()
        servers.append(s)
        return s.getsockname()[1], got

    alpha_port, alpha_got = stand_in()
    beta_port, beta_got = stand_in()
    gone = socket.socket()
    gone.bind(("127.0.0.1", 0))
    dead_port = gone.getsockname()[1]
    gone.close()
    routes = os.path.join(tmp, "routes")
    with open(routes, "w") as f:
        f.write(f"domain=mc.example.com\non|alpha|{alpha_port}\non|beta|{beta_port}\non|down|{dead_port}\n"
                f"on|bad.name|{alpha_port}\non|low|80\nheld|lapsed\n")
    ports = []
    threading.Thread(target=lambda: asyncio.run(run_router(("127.0.0.1", 0), routes, (2000, 3, 1000, 200), ports)),
                     daemon=True).start()
    for _ in range(100):
        if ports:
            break
        time.sleep(0.02)

    def play(data, wait=True):
        with socket.create_connection(("127.0.0.1", ports[0]), timeout=5) as s:
            s.sendall(data)
            out = b""
            try:
                while True:
                    part = s.recv(65536)
                    if not part:
                        return out
                    out += part
                    if not wait:
                        return out
            except (socket.timeout, ConnectionError):
                return out

    def text_of(reply):
        """The text in a status answer or a disconnect."""
        try:
            length, pos = read_varint(reply, 0)
            _, p = read_varint(reply, pos)
            size, p = read_varint(reply, p)
            j = json.loads(reply[p:p + size])
        except (More, ValueError):
            return ""
        return j.get("description", j).get("text", "")

    login = shake("ALPHA.mc.example.com") + packet(0, mc_string("Steve") + bytes(16))
    check("routes read, bad lines left out", read_routes(routes)[1] == {"alpha": alpha_port, "beta": beta_port,
                                                                       "down": dead_port})
    check("a player reaches the server their name is for", play(login) == b"hello from %d" % alpha_port
          and alpha_got and alpha_got[-1] == login)
    check("and another name another server", play(shake("beta.mc.example.com", 1) + b"\x01\x00")
          == b"hello from %d" % beta_port)
    ping = b"\x09\x01" + b"12345678"
    out = play(shake("nobody.mc.example.com", 1) + b"\x01\x00" + ping)
    check("no such name: said in the server list, ping answered", "no server called nobody" in text_of(out)
          and out.endswith(ping))
    check("no such name: said when joining", "no server called nobody" in text_of(play(shake("nobody.mc.example.com"))))
    check("a held name", "isn't available" in text_of(play(shake("lapsed.mc.example.com", 1) + b"\x01\x00" + ping)))
    check("a server that's off", "offline right now" in text_of(play(shake("down.mc.example.com"))))
    check("the bare domain or an address", "Type the server's name" in text_of(play(shake("1.2.3.4"))))
    check("anything else is closed", play(b"GET / HTTP/1.0\r\n\r\n") == b"")
    held = [socket.create_connection(("127.0.0.1", ports[0])) for _ in range(3)]
    time.sleep(0.2)
    check("connections from one address are capped", play(shake("alpha.mc.example.com")) == b"")
    for s in held:
        s.close()
    time.sleep(0.2)
    check("and free again after", play(shake("alpha.mc.example.com")) == b"hello from %d" % alpha_port)
    with open(routes, "w") as f:
        f.write(f"domain=mc.example.com\non|gamma|{beta_port}\n")
    time.sleep(2.5)
    check("routes picked up when they change", play(shake("gamma.mc.example.com")) == b"hello from %d" % beta_port
          and "no server called alpha" in text_of(play(shake("alpha.mc.example.com"))))

    # The sync, against a stand-in XenithOS Cloud
    secret, hostkey = "s" * 64, "ssh-ed25519 " + base64.b64encode(KEY_HEAD + os.urandom(32)).decode()
    k1, k2 = ("ssh-ed25519 " + base64.b64encode(KEY_HEAD + os.urandom(32)).decode() for _ in range(2))
    cloud = {"rows": "", "sign": True, "t": None, "heard": None}

    class Cloud(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def do_POST(self):
            body = self.rfile.read(int(self.headers.get("Content-Length") or 0))
            ok = hmac.compare_digest(sign(secret, "sync", body), self.headers.get("X-Relay-Sign", ""))
            form = {k: v[0] for k, v in parse_qs(body.decode()).items()}
            cloud["heard"] = form.get("hostkey") if ok else None
            text = f"t={cloud['t'] or form.get('t')}\ndomain=mc.example.com\n{cloud['rows']}".encode()
            self.send_response(200 if ok else 401)
            self.send_header("Content-Length", str(len(text)))
            self.send_header("X-Relay-Sign", sign(secret if cloud["sign"] else "x", "list", text))
            self.end_headers()
            self.wfile.write(text)

    cs = ThreadingHTTPServer(("127.0.0.1", 0), Cloud)
    threading.Thread(target=cs.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{cs.server_address[1]}"
    cloud["rows"] = (f"on|alpha|30000|{k1}\non|beta|30001|{k2} extra\nheld|gone\non|evil|30002|{k1}\n"
                     f"on|twin|30001|{k2}\non|bad|30003|ssh-ed25519 AAAA\non|x.y|30004|{k1}\n"
                     f"on|inject|30005|{k1}\nrestrict\n")
    got = fetch(url, secret, hostkey + " root@relay")
    check("sync: the list, with only good rows", got == ("mc.example.com", [("alpha", 30000, k1), ("beta", 30001, k2)], ["gone"])
          and cloud["heard"] == hostkey + " root@relay")
    check("sync: refused with another secret", _raises(fetch, url, "t" * 64, hostkey, exc=OSError))
    cloud["sign"] = False
    check("sync: an answer not signed is no answer", _raises(fetch, url, secret, hostkey))
    cloud["sign"], cloud["t"] = True, 12345
    check("sync: an old answer is no answer", _raises(fetch, url, secret, hostkey))
    cloud["t"] = None
    folder = os.path.join(tmp, "lists")
    os.makedirs(folder)
    check("sync: lists written", write_lists(folder, *got) and not write_lists(folder, *got))
    with open(os.path.join(folder, "authorized_keys")) as f:
        keys = f.read().splitlines()
    check("sync: each key may open only its own port", keys[1:] == [
        f'restrict,port-forwarding,permitlisten="127.0.0.1:30000" {k1} alpha',
        f'restrict,port-forwarding,permitlisten="127.0.0.1:30001" {k2} beta']
        and os.stat(os.path.join(folder, "authorized_keys")).st_mode & 0o777 == 0o600)
    check("sync: the router's routes", read_routes(os.path.join(folder, "routes")) ==
          ("mc.example.com", {"alpha": 30000, "beta": 30001}, {"gone"}))
    cs.shutdown()
    cs.server_close()
    check("sync: XenithOS Cloud away, the lists stay", not run_sync(url, secret, "/nonexistent", folder, once=True)
          and read_routes(os.path.join(folder, "routes"))[1] == {"alpha": 30000, "beta": 30001})
    here, everywhere = socket.socket(), socket.socket()
    here.bind(("127.0.0.1", 0))
    everywhere.bind(("0.0.0.0", 0))
    here.listen(1)
    everywhere.listen(1)
    busy = busy_ports()
    check("busy ports: this account's, on the loopback only", here.getsockname()[1] in busy
          and everywhere.getsockname()[1] not in busy)
    here.close()
    everywhere.close()
    check("busy ports: told to XenithOS Cloud, signed", _told_busy(secret, [30001, 30002]))
    check("sshd settings: forwards to the loopback only, nothing else",
          all(line in sshd_config(2222, "hostmc", "/k", "/h", "/p").splitlines()
              for line in ("AllowTcpForwarding remote", "PermitOpen none", "GatewayPorts no", "MaxSessions 0",
                           "AuthorizedKeysFile /k", "AllowUsers hostmc", "PasswordAuthentication no")))

    for s in servers:
        s.close()
    for name, ok in checks:
        print(("PASS " if ok else "FAIL ") + name)
    failed = sum(not ok for _, ok in checks)
    print(f"{len(checks) - failed}/{len(checks)} passed")
    return 1 if failed else 0


def _told_busy(secret, busy):
    """fetch() sends the busy ports, signed with the rest (a stand-in that only listens)."""
    import threading
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
    from urllib.parse import parse_qs
    heard = {}

    class Listen(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def do_POST(self):
            body = self.rfile.read(int(self.headers.get("Content-Length") or 0))
            if hmac.compare_digest(sign(secret, "sync", body), self.headers.get("X-Relay-Sign", "")):
                heard.update({k: v[0] for k, v in parse_qs(body.decode()).items()})
            self.send_response(500)
            self.end_headers()

    s = ThreadingHTTPServer(("127.0.0.1", 0), Listen)
    threading.Thread(target=s.serve_forever, daemon=True).start()
    _raises(fetch, f"http://127.0.0.1:{s.server_address[1]}", secret, "", busy, exc=OSError)
    s.shutdown()
    s.server_close()
    return heard.get("busy") == ",".join(map(str, busy))


def _raises(fn, *args, exc=ValueError):
    try:
        fn(*args)
    except exc:
        return True
    return False


def main(args):
    if args == ["--selftest"]:
        return selftest()
    if len(args) == 6 and args[0] == "sshd-config":
        sys.stdout.write(sshd_config(int(args[1]), args[2], args[3], args[4], args[5]))
        return 0
    if args == ["router"]:
        limits = tuple(int(os.environ.get(k, d)) for k, d in (("MCRELAY_MAX", "2000"), ("MCRELAY_PER_IP", "12"),
                                                               ("MCRELAY_RATE", "40"), ("MCRELAY_PER_NAME", "200")))
        try:
            asyncio.run(run_router(address(os.environ.get("MCRELAY_LISTEN", "0.0.0.0:25565"), 25565),
                                   os.path.join(os.environ.get("MCRELAY_DIR", "/var/lib/mcrelay"), "routes"), limits))
        except KeyboardInterrupt:
            pass
        return 0
    if args == ["sync"]:
        cloud = os.environ.get("MCRELAY_CLOUD", "")
        if not re.match(r"^https?://[A-Za-z0-9.:\[\]-]+(/[^\s]*)?$", cloud):
            sys.exit("mcrelay: MCRELAY_CLOUD must be XenithOS Cloud's address, like http://cloud.example.com:8795")
        try:
            with open(os.environ.get("MCRELAY_SECRET_FILE", "/etc/mcrelay/secret"), encoding="ascii") as f:
                secret = f.read().strip()
        except OSError as e:
            sys.exit(f"mcrelay: no secret ({e})")
        if len(secret) < 32:
            sys.exit("mcrelay: the secret is too short")
        try:
            run_sync(cloud, secret, os.environ.get("MCRELAY_HOSTKEY", "/etc/mcrelay/ssh_host_ed25519_key.pub"),
                     os.environ.get("MCRELAY_DIR", "/var/lib/mcrelay"),
                     every=max(1, int(os.environ.get("MCRELAY_EVERY", str(SYNC_EVERY)))))
        except KeyboardInterrupt:
            pass
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
