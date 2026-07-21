#!/usr/bin/env python3
import argparse
import http.server
import json
import time


class Handler(http.server.BaseHTTPRequestHandler):
    identity = "unknown"
    delay_ms = 0

    def do_GET(self):
        time.sleep(self.delay_ms / 1000)
        payload = json.dumps({
            "outbound": self.identity,
            "local": self.connection.getsockname()[0],
            "peer": self.client_address[0],
            "path": self.path,
        }).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_):
        pass


parser = argparse.ArgumentParser()
parser.add_argument("--identity", required=True)
parser.add_argument("--delay-ms", type=int, default=0)
args = parser.parse_args()
Handler.identity = args.identity
Handler.delay_ms = args.delay_ms
http.server.ThreadingHTTPServer(("0.0.0.0", 18080), Handler).serve_forever()
