#!/usr/bin/env python3
"""A stand-in for the one thing XenithOS Cloud asks OCAccounts (sim/hostmc/test-relay.sh):

    python3 sim/hostmc/ocaccounts.py MEMBERS_FILE PORT KEY

GET /api/partner/xenithos/supporter?profile=<gamertag>, with the header X-Partner-Key: KEY,
answers what MEMBERS_FILE says about that profile, read again each time so a test can change
who's a supporter while it runs. The file is JSON: {"<profile, lower case>": {"linked": true,
"patreon": true, "supporter": true, "tier": "Supporter"}}. A profile it doesn't name gets 404
and {"linked": false}, as one no OCAccount has linked would."""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

MEMBERS, PORT, KEY = sys.argv[1], int(sys.argv[2]), sys.argv[3]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        url = urlparse(self.path)
        who = parse_qs(url.query).get("profile", [""])[0].lower()
        try:
            with open(MEMBERS, encoding="utf-8") as f:
                members = json.load(f)
        except (OSError, ValueError):
            members = {}
        if url.path != "/api/partner/xenithos/supporter" or self.headers.get("X-Partner-Key") != KEY:
            code, out = 401, {"error": "not allowed"}
        elif who not in members:
            code, out = 404, {"linked": False}
        else:
            code, out = 200, members[who]
        body = json.dumps(out).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
