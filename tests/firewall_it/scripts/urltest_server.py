#!/usr/bin/env python3
import argparse
import http.server
import socketserver
import time


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/fast":
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"fast\n")
            return

        if self.path == "/slow":
            time.sleep(0.25)
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"slow\n")
            return

        if self.path == "/fail":
            self.send_response(500)
            self.end_headers()
            self.wfile.write(b"fail\n")
            return

        self.send_response(404)
        self.end_headers()

    def log_message(self, fmt, *args):
        return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=18080)
    args = parser.parse_args()

    with socketserver.TCPServer((args.host, args.port), Handler) as httpd:
        httpd.serve_forever()


if __name__ == "__main__":
    main()
