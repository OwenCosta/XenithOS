#!/usr/bin/env python3
"""XenithOS Cloud calls for the simulator's tests of friends, cloud saves and achievements.

    cloud.py wait    HOST:PORT                     until it answers /v1/ping (10 s at most)
    cloud.py profile HOST:PORT NAME PIN OUT        makes the profile (or signs in) and writes OUT as
                                                   XenithOS's profile.txt; prints the token
    cloud.py post    HOST:PORT PATH [key=value...] prints the reply; fails unless it's 200
    cloud.py get     HOST:PORT PATH                the same with GET
    cloud.py put     HOST:PORT TOKEN APP NAME FILE [base=N] [force=1]
                                                   uploads FILE as a cloud save (/v1/saves/put)
"""
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


def call(base, path, data=None):
    req = urllib.request.Request(base + path, data=data, method="POST" if data is not None else "GET")
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()
    except OSError as e:
        return 0, str(e).encode()


def main(argv):
    cmd, base = argv[1], "http://" + argv[2]
    if cmd == "wait":
        for _ in range(100):
            if call(base, "/v1/ping")[0] == 200:
                return 0
            time.sleep(0.1)
        return 1
    if cmd == "profile":
        name, pin, out = argv[3], argv[4], argv[5]
        form = urllib.parse.urlencode({"name": name, "pin": pin, "avatar": "22020010000000"}).encode()
        code, body = call(base, "/v1/accounts/create", form)
        if code != 200:
            code, body = call(base, "/v1/accounts/login", urllib.parse.urlencode({"name": name, "pin": pin}).encode())
        if code != 200:
            print(body.decode(errors="replace"), file=sys.stderr)
            return 1
        kv = dict(line.split("=", 1) for line in body.decode().splitlines() if "=" in line)
        with open(out, "w") as f:
            f.write(f"name={kv['name']}\ntoken={kv['token']}\navatar={kv['avatar']}\njoined={kv['joined']}\n")
        print(kv["token"])
        return 0
    if cmd in ("post", "get"):
        form = dict(a.split("=", 1) for a in argv[4:])
        code, body = call(base, argv[3], urllib.parse.urlencode(form).encode() if cmd == "post" else None)
        sys.stdout.write(body.decode("latin-1"))
        return 0 if code == 200 else 1
    if cmd == "put":
        token, app, name, path = argv[3:7]
        extra = dict(a.split("=", 1) for a in argv[7:])
        with open(path, "rb") as f:
            data = f.read()
        head = urllib.parse.urlencode({"token": token, "app": app, "name": name, "from": "Test", **extra})
        code, body = call(base, "/v1/saves/put", head.encode() + b"\n" + data)
        sys.stdout.write(body.decode("latin-1"))
        return 0 if code == 200 else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
