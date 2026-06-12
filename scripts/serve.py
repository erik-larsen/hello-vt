#!/usr/bin/env python3
"""
Development web server for the hello-vt Emscripten build.

Serves the sample/ directory (so both the built app under bin/web/ and the
uv-test-8kx8k/ tile store are reachable from the same origin) and adds the
COOP/COEP headers required for SharedArrayBuffer, which Emscripten pthreads
depend on. A plain `python3 -m http.server` will NOT work: the app would fail
to start its loader/decompressor threads.

Usage:
    python3 scripts/serve.py [port]      (default port 8000)
Then open:
    http://localhost:8000/bin/web/hello-vt.html
"""

import os
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


class COIRequestHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        # Required for SharedArrayBuffer (Emscripten pthreads)
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # Convenience for development: always refetch
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    web_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sample")
    os.chdir(web_root)

    server = ThreadingHTTPServer(("0.0.0.0", port), COIRequestHandler)
    print(f"Serving {os.getcwd()} at http://localhost:{port}")
    print(f"Open http://localhost:{port}/bin/web/hello-vt.html")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
