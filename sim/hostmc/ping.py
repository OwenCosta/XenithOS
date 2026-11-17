"""A Minecraft server list ping, as a player's game sends it:

    python3 sim/hostmc/ping.py HOST PORT [NAME [FROM]]

prints the server's answer (JSON) and exits 0, or exits 1 if no Minecraft server answered.
NAME is the address the player typed, sent in the handshake, when it isn't HOST (a name on the
supporter relay, connecting to the relay's own address); FROM is the address the player connects
from (another loopback address, like 127.0.0.7, stands for a player elsewhere). sim/hostmc/test-vps.sh
and test-relay.sh use it as a player outside, reaching the server through the VPS or the relay."""
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
        s = socket.create_connection((host, port), timeout=5,
                                     source_address=(sys.argv[4], 0) if len(sys.argv) > 4 else None)
        name = (sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else host).encode()
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
