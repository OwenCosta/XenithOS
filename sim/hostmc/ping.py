"""A Minecraft server list ping, as a player's game sends it:

    python3 sim/hostmc/ping.py HOST PORT

prints the server's answer (JSON) and exits 0, or exits 1 if no Minecraft server answered.
sim/hostmc/test-vps.sh uses it as a player outside, reaching the server through the VPS."""
import socket
import struct
import sys


def varint(n):
    n &= 0xFFFFFFFF
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | 0x80 if n else b)
        if not n:
            return bytes(out)


def read_varint(s):
    n = shift = 0
    while True:
        b = s.recv(1)
        if not b:
            raise EOFError("closed")
        n |= (b[0] & 0x7F) << shift
        shift += 7
        if not b[0] & 0x80:
            return n


def main():
    host, port = sys.argv[1], int(sys.argv[2])
    try:
        s = socket.create_connection((host, port), timeout=5)
        name = host.encode()
        body = varint(0) + varint(-1) + varint(len(name)) + name + struct.pack(">H", port) + varint(1)
        s.sendall(varint(len(body)) + body + bytes([1, 0]))
        length = read_varint(s)
        data = b""
        while len(data) < length:
            chunk = s.recv(length - len(data))
            if not chunk:
                raise EOFError("closed")
            data += chunk
        s.close()
    except (OSError, EOFError) as e:
        print("no answer: %s" % e, file=sys.stderr)
        return 1
    i = 1                                    # past the packet id
    n = shift = 0
    while True:                              # the JSON's length
        b = data[i]
        i += 1
        n |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            break
    print(data[i:i + n].decode("utf-8", "replace"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
