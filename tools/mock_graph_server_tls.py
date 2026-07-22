#!/usr/bin/env python3
"""tools/mock_graph_server_tls.py — TLS version of mock_graph_server.py.

Same Graph-API-shaped JSON, served over HTTPS with a self-signed cert
(vm/tls_mock/{cert,key}.pem, CN/SAN = 10.0.2.2) so the NT4 client can exercise
the full stack — pal sockets + mbedTLS handshake/record layer + core/http +
core/json + core/model — against a real (if self-signed) TLS 1.2 server.

    python3 tools/mock_graph_server_tls.py [port]   # default 8443
"""
import http.server
import json
import ssl
import sys
import os

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
        sys.stderr.write("mock-tls: " + (fmt % args) + "\n")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8443
    here = os.path.dirname(os.path.abspath(__file__))
    certdir = os.path.join(here, "..", "vm", "tls_mock")

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile=os.path.join(certdir, "cert.pem"),
                        keyfile=os.path.join(certdir, "key.pem"))
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2  # force 1.2 to match our client

    srv = http.server.HTTPServer(("0.0.0.0", port), Handler)
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    print(f"mock TLS graph server on :{port} (guest reaches it via 10.0.2.2:{port})")
    srv.serve_forever()
