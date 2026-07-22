#!/usr/bin/env python3
"""tools/mock_graph_server.py — tiny Graph-API-shaped HTTP mock for milestone 4.

Serves a canned /me/media-style JSON response over plain HTTP so the NT4
client can exercise pal sockets + core/http + core/json + core/model end to
end before TLS/mbedTLS exists. Bind 0.0.0.0 so the QEMU guest can reach it via
the SLIRP gateway address (10.0.2.2) without any port forwarding.

    python3 tools/mock_graph_server.py [port]   # default 8080
"""
import http.server
import json
import sys

BODY = json.dumps({
    "data": [
        {
            "id": "1001", "username": "jun.photo",
            "caption": "golden hour never sets old", "media_type": "IMAGE",
            "media_url": "https://example.com/1.jpg",
            "permalink": "https://instagram.com/p/aaa",
            "timestamp": "2024-06-01T10:00:00+0000", "like_count": 1204,
        },
        {
            "id": "1002", "username": "skywatch", "media_type": "IMAGE",
            "media_url": "https://example.com/2.jpg", "like_count": 892,
        },
    ],
    "paging": {"cursors": {"after": "MOCKCURSOR"}},
}).encode()


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(BODY)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(BODY)

    def log_message(self, fmt, *args):
        sys.stderr.write("mock: " + (fmt % args) + "\n")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    srv = http.server.HTTPServer(("0.0.0.0", port), Handler)
    print(f"mock graph server on :{port} (guest reaches it via 10.0.2.2:{port})")
    srv.serve_forever()
