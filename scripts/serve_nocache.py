"""Static dev server that disables ALL browser caching.

Python's stock `http.server` sends no Cache-Control, so browsers apply
heuristic caching and may serve a stale MiniEngine.wasm/.data from disk
cache even after a rebuild + hard refresh (the Emscripten loader fetches
those via fetch(), which a page hard-reload does not always re-fetch).
This handler forces every response to be re-fetched, so a rebuild is
always reflected in the browser.

Usage: run from the directory to serve, e.g.
    python scripts/serve_nocache.py 8000
"""
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


class NoCacheHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()

    def send_header(self, key, value):
        # Drop the default Last-Modified so the browser cannot do a
        # conditional (304) request and reuse stale bytes.
        if key.lower() == "last-modified":
            return
        super().send_header(key, value)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    httpd = ThreadingHTTPServer(("", port), NoCacheHandler)
    print(f"Serving (no-cache) on http://localhost:{port}  — Ctrl+C to stop")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
