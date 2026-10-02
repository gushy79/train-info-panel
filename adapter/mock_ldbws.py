"""Local stand-in for the RDM LDBWS endpoint, serving fixtures from testdata/ldbws.

Binds to loopback only. Pick a scenario with ?scenario=normal|delayed|cancelled|empty|unavailable
(default: normal). Requires a non-empty x-apikey header, like the real service; any value works.
The route is the one confirmed against the live RDM service. filterCrs=ZZZ returns an empty board:
the real service does that for a station the trains never call at.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

FIXTURES = Path(__file__).resolve().parent.parent / "testdata" / "ldbws"
SCENARIOS = {"normal", "delayed", "cancelled", "empty", "unavailable"}
ROUTE = "/1010-live-departure-board-dep1_2/LDBWS/api/20220120/GetDepartureBoard/"


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        url = urlparse(self.path)
        if not url.path.startswith(ROUTE):
            return self._send(404, {"error": "not found"})
        if not self.headers.get("x-apikey"):
            return self._send(401, {"error": "missing x-apikey"})
        query = parse_qs(url.query)
        scenario = query.get("scenario", ["normal"])[0]
        if query.get("filterCrs", [""])[0] == "ZZZ":
            scenario = "empty"
        if scenario not in SCENARIOS:
            return self._send(400, {"error": "unknown scenario"})
        self._send(200, json.loads((FIXTURES / f"southbound_{scenario}.json").read_text()))

    def _send(self, code, body):
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
    HTTPServer(("127.0.0.1", port), Handler).serve_forever()
