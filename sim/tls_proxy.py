#!/usr/bin/env python3
"""A stand-in for Cloudflare's edge and tunnel, for sim/test-https.sh: HTTPS on a port of
127.0.0.1 with the certificate it's given, and each request passed on to XenithOS Cloud's edge
port (XENITH_EDGE_PORT) with the CF-Connecting-IP header Cloudflare adds. That header names
every console the address given here, so the server's log shows which requests came this way.

  tls_proxy.py <port> <certificate.pem> <key.pem> <edge port> <address to name>

Only 127.0.0.1 is listened on: a name like localhost that has ::1 first then makes the client
try its next address, as a PC on a network without working IPv6 would.
"""
import socket
import ssl
import sys
import threading


def handle(conn, edge_port, client_ip):
    data = b""
    while b"\r\n\r\n" not in data:
        part = conn.recv(65536)
        if not part or len(data) > 65536:
            return
        data += part
    head, _, body = data.partition(b"\r\n\r\n")
    lines = head.split(b"\r\n")
    length = 0
    for line in lines[1:]:
        name, _, value = line.partition(b":")
        if name.strip().lower() == b"content-length":
            length = int(value.strip())
    while len(body) < length:
        part = conn.recv(65536)
        if not part:
            return
        body += part
    # Cloudflare replaces whatever a client sent in these.
    kept = [line for line in lines[1:]
            if line.partition(b":")[0].strip().lower() not in (b"cf-connecting-ip", b"x-forwarded-proto")]
    out = b"\r\n".join([lines[0]] + kept + [b"CF-Connecting-IP: " + client_ip, b"X-Forwarded-Proto: https"])
    with socket.create_connection(("127.0.0.1", edge_port), timeout=30) as up:
        up.sendall(out + b"\r\n\r\n" + body)
        while True:
            part = up.recv(65536)
            if not part:
                break
            conn.sendall(part)
    try:
        conn.unwrap()                     # TLS's goodbye, which Cloudflare sends too
    except (OSError, ValueError):
        pass


def serve_one(raw, ctx, edge_port, client_ip):
    raw.settimeout(30)
    try:
        conn = ctx.wrap_socket(raw, server_side=True)
    except OSError as e:                  # a client that turned the certificate down, say
        print(f"handshake failed: {e}", flush=True)
        raw.close()
        return
    with conn:
        try:
            handle(conn, edge_port, client_ip)
        except OSError as e:
            print(f"connection failed: {e}", flush=True)


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    port, cert, key, edge_port, client_ip = sys.argv[1:]
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    srv = socket.create_server(("127.0.0.1", int(port)))
    print(f"https on 127.0.0.1:{port}, passed on to :{edge_port} as {client_ip}", flush=True)
    while True:
        raw, _ = srv.accept()
        threading.Thread(target=serve_one, args=(raw, ctx, int(edge_port), client_ip.encode()),
                         daemon=True).start()


if __name__ == "__main__":
    main()
