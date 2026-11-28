"""A local stand-in for the update Worker (tools/release/worker), for testing
publish.py and the launcher without Cloudflare. Same routes and tokens; the
buckets are folders.

  python -I tools/release/local_server.py --root E:\\swm_build\\fake_r2 --port 8787 \\
      --publish-token test-publish --portable-token test-portable

Then publish.py --url http://127.0.0.1:8787, and a launcher.cfg url of
http://127.0.0.1:8787/public (or /private).
"""
import argparse
import hmac
import http.server
import json
import os
import re
import socketserver
import urllib.parse

KEY = re.compile(r"^(channels/[a-z0-9_-]{1,32}/[A-Za-z0-9._-]{1,64}|blobs/[0-9a-f]{2}/[0-9a-f]{64})$")


def make_handler(root, publish_token, portable_token):
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def _route(self):
            u = urllib.parse.urlparse(self.path)
            parts = [p for p in u.path.split("/") if p]
            area = parts[0] if parts else ""
            return area, "/".join(parts[1:]), urllib.parse.parse_qs(u.query)

        def _token(self):
            h = self.headers.get("Authorization", "")
            return h[7:] if h.startswith("Bearer ") else ""

        def _send(self, status, body=b"", ctype="text/plain"):
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)

        def _authorized(self, area):
            t = self._token()
            if hmac.compare_digest(t, publish_token):
                return True
            return area == "public" or (portable_token and hmac.compare_digest(t, portable_token))

        def do_GET(self):
            area, key, query = self._route()
            if area == "guard" and key == "":
                # The usage guard's verdict: always open, nothing used (no budgets locally).
                if not hmac.compare_digest(self._token(), publish_token):
                    return self._send(401, b"unauthorized\n")
                verdict = {"reads_open": True, "writes_open": True, "reason": "",
                           "usage": {"class_a": 0, "class_b": 0, "storage_bytes": 0},
                           "budgets": {"class_a": 800000, "class_b": 8000000, "storage_bytes": 8e9}}
                return self._send(200, json.dumps(verdict).encode(), "application/json")
            if area not in ("public", "private"):
                return self._send(404, b"not found\n")
            if key == "list":
                if not hmac.compare_digest(self._token(), publish_token):
                    return self._send(401, b"unauthorized\n")
                keys = []
                blobs = os.path.join(root, area, "blobs")
                for d, _, fs in os.walk(blobs):
                    keys += ["blobs/%s/%s" % (os.path.basename(d), f) for f in fs]
                return self._send(200, json.dumps({"keys": keys, "cursor": None}).encode(), "application/json")
            if not KEY.match(key):
                return self._send(404, b"not found\n")
            if not self._authorized(area):
                return self._send(401, b"unauthorized\n")
            path = os.path.join(root, area, *key.split("/"))
            if not os.path.isfile(path):
                return self._send(404, b"not found\n")
            with open(path, "rb") as f:
                self._send(200, f.read(), "application/octet-stream")

        do_HEAD = do_GET

        def do_PUT(self):
            area, key, _ = self._route()
            if area not in ("public", "private") or not KEY.match(key):
                return self._send(404, b"not found\n")
            if not hmac.compare_digest(self._token(), publish_token):
                return self._send(401, b"unauthorized\n")
            n = int(self.headers.get("Content-Length", "0"))
            data = self.rfile.read(n)
            path = os.path.join(root, area, *key.split("/"))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path + ".tmp", "wb") as f:
                f.write(data)
            os.replace(path + ".tmp", path)
            self._send(201, b"stored\n")

        def log_message(self, fmt, *args):
            pass

    return Handler


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", required=True)
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--publish-token", required=True)
    ap.add_argument("--portable-token", default="")
    args = ap.parse_args()
    os.makedirs(args.root, exist_ok=True)
    Server(("127.0.0.1", args.port), make_handler(args.root, args.publish_token, args.portable_token)).serve_forever()


if __name__ == "__main__":
    main()
