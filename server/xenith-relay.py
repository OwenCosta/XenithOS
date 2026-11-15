#!/usr/bin/env python3
"""xenith-relay: XenithOS Cloud's public console address, for a XenithOS Cloud that runs
somewhere else, such as a machine at home with no public address of its own.

Consoles (the Xbox has no DNS or HTTPS) keep connecting to this machine. The machine that
really runs XenithOS Cloud keeps an SSH tunnel open to here (ssh -R 127.0.0.1:18795:...), and
this passes every console connection down it. Each one starts with a PROXY protocol v1 line,
so XenithOS Cloud (its XENITH_PROXY_PORT) sees the console's own address. When the tunnel is
down, consoles get a plain "error=" answer instead of a hang.

Standard library only.
  XENITH_RELAY_LISTEN  default 0.0.0.0:8795      where consoles connect
  XENITH_RELAY_TO      default 127.0.0.1:18795   the tunnel's end on this machine
  XENITH_RELAY_MAX     default 400               connections at once
  xenith-relay.py --selftest
"""
import asyncio
import os
import socket
import sys

DOWN = (b"HTTP/1.0 503 Service Unavailable\r\nContent-Type: text/plain; charset=ascii\r\n"
        b"Connection: close\r\n\r\nerror=XenithOS Cloud isn't answering right now. Try again in a minute.\n")
BUSY = (b"HTTP/1.0 503 Service Unavailable\r\nContent-Type: text/plain; charset=ascii\r\n"
        b"Connection: close\r\n\r\nerror=XenithOS Cloud is busy. Try again in a minute.\n")
CHUNK = 65536


def address(text, default_port):
    host, _, port = text.rpartition(":")
    return host or "0.0.0.0", int(port or default_port)


def plain_ip(ip):
    """An IPv4 address that arrived as IPv6 ("::ffff:1.2.3.4") is just IPv4."""
    return ip[7:] if ip.startswith("::ffff:") and "." in ip else ip


async def pipe(reader, writer):
    """Copies until the sender is done, then says so (a half-close), so a request that has been
    sent in full still gets its whole answer back."""
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


class Relay:
    def __init__(self, to, limit):
        self.to = to
        self.limit = limit
        self.active = 0

    async def answer(self, writer, text):
        try:
            writer.write(text)
            await writer.drain()
        except (ConnectionError, OSError):
            pass
        writer.close()

    async def handle(self, client_r, client_w):
        if self.active >= self.limit:
            return await self.answer(client_w, BUSY)
        self.active += 1
        try:
            peer = client_w.get_extra_info("peername") or ("0.0.0.0", 0)
            here = client_w.get_extra_info("sockname") or ("0.0.0.0", 0)
            src, dst = plain_ip(peer[0]), plain_ip(here[0])
            try:
                server_r, server_w = await asyncio.wait_for(asyncio.open_connection(*self.to), 10)
            except (OSError, asyncio.TimeoutError):
                return await self.answer(client_w, DOWN)
            family = "TCP6" if ":" in src else "TCP4"
            if (":" in dst) != (family == "TCP6"):
                dst = "::" if family == "TCP6" else "0.0.0.0"
            server_w.write(f"PROXY {family} {src} {dst} {peer[1]} {here[1]}\r\n".encode("ascii"))
            try:
                await asyncio.gather(pipe(client_r, server_w), pipe(server_r, client_w))
            finally:
                for w in (server_w, client_w):
                    w.close()
        finally:
            self.active -= 1


async def run(listen, to, limit, ready=None):
    relay = Relay(to, limit)
    server = await asyncio.start_server(relay.handle, listen[0], listen[1], reuse_address=True)
    if ready is not None:
        ready.append(server.sockets[0].getsockname()[1])
    else:
        print(f"xenith-relay: consoles on {listen[0]}:{listen[1]}, passed to {to[0]}:{to[1]}", flush=True)
    async with server:
        await server.serve_forever()


# ---------------------------------------------------------------------- selftest

def selftest():
    import threading
    import time

    checks = []

    def check(name, ok):
        checks.append((name, bool(ok)))

    # A stand-in XenithOS Cloud: answers with the PROXY line it was given and the request.
    backend = socket.socket()
    backend.bind(("127.0.0.1", 0))
    backend.listen(8)

    def serve_backend():
        while True:
            try:
                conn, _ = backend.accept()
            except OSError:
                return
            with conn:
                got = b""
                while b"\r\n\r\n" not in got:
                    part = conn.recv(4096)
                    if not part:
                        break
                    got += part
                body = b"saw:" + got + b"big:" + b"x" * 300000
                conn.sendall(b"HTTP/1.0 200 OK\r\nContent-Length: %d\r\n\r\n" % len(body) + body)

    threading.Thread(target=serve_backend, daemon=True).start()
    closed = socket.socket()
    closed.bind(("127.0.0.1", 0))
    dead_port = closed.getsockname()[1]
    closed.close()                                         # nothing listens here: the tunnel is down

    ports = []
    for to in (("127.0.0.1", backend.getsockname()[1]), ("127.0.0.1", dead_port)):
        threading.Thread(target=lambda t=to: asyncio.run(run(("127.0.0.1", 0), t, 400, ports)), daemon=True).start()
    for _ in range(100):
        if len(ports) == 2:
            break
        time.sleep(0.02)

    def ask(port, data, half_close):
        with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
            s.sendall(data)
            if half_close:
                s.shutdown(socket.SHUT_WR)              # what an HTTP/1.0 client may do after its request
            out = b""
            while True:
                part = s.recv(65536)
                if not part:
                    return out
                out += part

    out = ask(ports[0], b"GET /v1/ping HTTP/1.0\r\n\r\n", False)
    check("passes the request on, PROXY line first",
          b"saw:PROXY TCP4 127.0.0.1 127.0.0.1 " in out and b"\r\nGET /v1/ping HTTP/1.0" in out)
    check("the whole answer comes back", out.endswith(b"x" * 1000) and len(out) > 300000)
    out = ask(ports[0], b"GET /v1/store HTTP/1.0\r\n\r\n", True)
    check("a client that half-closes still gets its answer", b"GET /v1/store" in out and len(out) > 300000)
    out = ask(ports[1], b"GET /v1/ping HTTP/1.0\r\n\r\n", False)
    check("tunnel down: an error, not a hang", out.startswith(b"HTTP/1.0 503") and b"error=" in out)
    check("IPv4 seen through IPv6 stays IPv4", plain_ip("::ffff:203.0.113.9") == "203.0.113.9"
          and plain_ip("2001:db8::1") == "2001:db8::1")
    check("addresses parse", address("0.0.0.0:8795", 1) == ("0.0.0.0", 8795) and address(":9", 1) == ("0.0.0.0", 9))
    backend.close()
    for name, ok in checks:
        print(("PASS " if ok else "FAIL ") + name)
    failed = sum(not ok for _, ok in checks)
    print(f"{len(checks) - failed}/{len(checks)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    try:
        asyncio.run(run(address(os.environ.get("XENITH_RELAY_LISTEN", "0.0.0.0:8795"), 8795),
                        address(os.environ.get("XENITH_RELAY_TO", "127.0.0.1:18795"), 18795),
                        int(os.environ.get("XENITH_RELAY_MAX", "400"))))
    except KeyboardInterrupt:
        pass
