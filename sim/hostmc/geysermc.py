#!/usr/bin/env python3
"""A stand-in for GeyserMC's download service (download.geysermc.org/v2), for
sim/hostmc/test-bedrock.sh, so HostMC's Bedrock players can be tried with nothing downloaded:

    python3 sim/hostmc/geysermc.py PORT FOLDER

HostMC is pointed at it with HOSTMC_GEYSER_API=http://127.0.0.1:PORT/v2. It answers the two
requests HostMC makes, the way GeyserMC's service does:

  GET /v2/projects/<geyser|floodgate>/versions/latest/builds/latest
      the newest build: version, build and downloads.spigot (name, sha256)
  GET /v2/projects/<project>/versions/<version>/builds/<build>/downloads/spigot
      that build's jar (a few bytes standing in for it)

FOLDER/builds.json, read again each time, says which builds are newest, like
{"geyser": ["2.12.1", 1303], "floodgate": ["2.2.5", 141]} (those without it); with "bad": true the
jars it sends don't match their checksums, and with "bad": "floodgate" (or "geyser") only that one's
doesn't. Every request goes in FOLDER/requests.log."""
import hashlib
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, FOLDER = int(sys.argv[1]), sys.argv[2]
NAMES = {"geyser": "Geyser-Spigot.jar", "floodgate": "floodgate-spigot.jar"}


def builds():
    try:
        with open(os.path.join(FOLDER, "builds.json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {"geyser": ["2.12.1", 1303], "floodgate": ["2.2.5", 141]}


def jar(project, version, build):
    return f"stand-in {NAMES[project]} {version} build {build}, for HostMC's tests\n".encode()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send(self, code, body, ctype):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        with open(os.path.join(FOLDER, "requests.log"), "a", encoding="utf-8") as f:
            f.write(self.path + "\n")
        parts = self.path.split("?")[0].strip("/").split("/")
        b = builds()
        # v2 projects <p> versions <v> builds <b> [downloads spigot]
        if len(parts) >= 7 and parts[:2] == ["v2", "projects"] and parts[2] in NAMES and parts[3] == "versions" \
                and parts[5] == "builds":
            project, (version, build) = parts[2], b[parts[2]]
            if parts[4:] == ["latest", "builds", "latest"]:
                body = {"project_id": project, "project_name": project.capitalize(), "version": version, "build": build,
                        "time": "2026-11-15T20:18:35.212Z", "channel": "default", "promoted": False, "changes": [],
                        "downloads": {"spigot": {"name": NAMES[project],
                                                 "sha256": hashlib.sha256(jar(project, version, build)).hexdigest()},
                                      "velocity": {"name": "other.jar", "sha256": "0" * 64}}}
                return self.send(200, json.dumps(body).encode(), "application/json")
            if parts[4:] == [version, "builds", str(build), "downloads", "spigot"]:
                data = jar(project, version, build)
                if b.get("bad") is True or b.get("bad") == project:
                    data = data.replace(b"stand-in", b"tampered")
                return self.send(200, data, "application/java-archive")
        self.send(404, b'{"error":"no such thing"}', "application/json")


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
