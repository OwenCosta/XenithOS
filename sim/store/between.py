#!/usr/bin/env python3
"""Someone between a console and XenithOS Cloud, for sim/store/test-store.sh. It passes each
request on to the server and the answer back, changing things on the way when told to:

  between.py <port to listen on> <server host:port> [--swap ID] [--strip] [--no-bundle]
             [--slow ID] [--rate KB/s]

  --swap ID     what an attacker would do: ID's file is swapped for one of the same size, and so
                is its SHA-256 in the Store's lists (the bundle too), the signature left as it was
  --strip       takes the X-Xenith-Sig header off every answer
  --no-bundle   answers /v1/store/all with 404, so the console asks for each list on its own
  --slow ID     sends ID's file slowly (--rate, default 160 KB/s), so it can be stopped midway

Each request goes to stderr, with what was done to it. Standard library only; it serves until
it's killed.
"""
import argparse
import hashlib
import re
import socket
import socketserver
import sys
import time


def read_all(sock):
    out = b""
    while True:
        part = sock.recv(65536)
        if not part:
            return out
        out += part


class Between(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def handler_for(opts):
    host, port = opts.server.rsplit(":", 1)
    swapped = {}                             # ID -> (the SHA-256 it had, the swapped file)

    def swap(body):
        """ID's row (8 fields in /v1/store and the bundle's store section): same size, new file."""
        m = re.search(rb"(?m)^" + re.escape(opts.swap.encode()) + rb"\|[^|\n]*\|[^|\n]*\|[^|\n]*\|(\d+)\|([0-9a-f]{64})\|",
                      body)
        if not m:
            return body, False
        size, old = int(m.group(1)), m.group(2)
        if opts.swap not in swapped:
            evil = (b"Not what XenithOS Cloud sent. " * (size // 30 + 1))[:size]
            swapped[opts.swap] = (old, evil)
        return body.replace(old, hashlib.sha256(swapped[opts.swap][1]).hexdigest().encode()), True

    class Handler(socketserver.BaseRequestHandler):
        def handle(self):
            req = b""
            while b"\r\n\r\n" not in req:
                part = self.request.recv(65536)
                if not part or len(req) > 1 << 20:
                    return
                req += part
            head, _, body = req.partition(b"\r\n\r\n")
            m = re.search(rb"(?im)^content-length:\s*(\d+)", head)
            while m and len(body) < int(m.group(1)):
                part = self.request.recv(65536)
                if not part:
                    return
                body += part
            path = head.split(b" ", 2)[1].decode("ascii", "replace")
            did = []
            if opts.no_bundle and path.startswith("/v1/store/all"):
                self.request.sendall(b"HTTP/1.0 404 Not Found\r\nContent-Length: 17\r\nConnection: close\r\n\r\n"
                                     b"error=Not found.\n")
                print(f"between: {path} -> 404 (no bundle)", file=sys.stderr, flush=True)
                return
            with socket.create_connection((host, int(port)), timeout=30) as up:
                up.sendall(head + b"\r\n\r\n" + body)
                answer = read_all(up)
            top, _, data = answer.partition(b"\r\n\r\n")
            lines = top.split(b"\r\n")
            if opts.swap and path.startswith("/v1/store") and "/file/" not in path:
                data, hit = swap(data)
                if hit:
                    did.append(f"swapped {opts.swap}'s SHA-256")
            if opts.swap and path == f"/v1/store/file/{opts.swap}" and opts.swap in swapped:
                data = swapped[opts.swap][1]
                did.append("swapped the file")
            if opts.strip and any(line.lower().startswith(b"x-xenith-sig:") for line in lines):
                lines = [line for line in lines if not line.lower().startswith(b"x-xenith-sig:")]
                did.append("signature taken off")
            lines = [b"Content-Length: %d" % len(data) if line.lower().startswith(b"content-length:") else line
                     for line in lines]
            out = b"\r\n".join(lines) + b"\r\n\r\n"
            slow = opts.slow and path == f"/v1/store/file/{opts.slow}"
            print(f"between: {path} -> {lines[0].decode('ascii', 'replace')}"
                  f"{' (' + ', '.join(did) + ')' if did else ''}{' (slowly)' if slow else ''}", file=sys.stderr, flush=True)
            try:
                if not slow:
                    self.request.sendall(out + data)
                    return
                self.request.sendall(out)
                step = 8192
                for at in range(0, len(data), step):
                    self.request.sendall(data[at:at + step])
                    time.sleep(step / (opts.rate * 1024))
            except OSError:
                print(f"between: {path}: the console hung up", file=sys.stderr, flush=True)

    return Handler


def main():
    ap = argparse.ArgumentParser(description="Someone between a console and XenithOS Cloud (sim/store).")
    ap.add_argument("port", type=int)
    ap.add_argument("server")
    ap.add_argument("--swap", default="")
    ap.add_argument("--strip", action="store_true")
    ap.add_argument("--no-bundle", action="store_true")
    ap.add_argument("--slow", default="")
    ap.add_argument("--rate", type=float, default=160)
    opts = ap.parse_args()
    with Between(("127.0.0.1", opts.port), handler_for(opts)) as srv:
        print(f"between: on 127.0.0.1:{opts.port}, passing on to {opts.server}", file=sys.stderr, flush=True)
        srv.serve_forever()


if __name__ == "__main__":
    main()
