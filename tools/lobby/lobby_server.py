#!/usr/bin/env python3
"""The public games list on this PC, for tests (hosttest LOBBY_URL=http://127.0.0.1:8787).
Answers as src/worker.js does (the Cloudflare one): see there."""
import sys, time, threading, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FORGET = 90.0
games = {}   # address -> dict(name, players, v, seen, since)
lock = threading.Lock()

class H(BaseHTTPRequestHandler):
    def reply(self, body, status=200):
        b = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def forget(self):
        now = time.time()
        for a in [a for a, g in games.items() if now - g["seen"] > FORGET]:
            del games[a]

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        if u.path != "/list":
            return self.reply("Castle Crashers Vita public games\n" if u.path == "/" else "not here\n", 200 if u.path == "/" else 404)
        v = urllib.parse.parse_qs(u.query).get("v", [""])[0]
        with lock:
            self.forget()
            rows = sorted(((a, g) for a, g in games.items() if g["v"] == v and g["players"] < 4), key=lambda x: -x[1]["since"])[:24]
        self.reply("".join(f"{a}\t{g['players']}\t{g['name']}\n" for a, g in rows))

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        n = int(self.headers.get("Content-Length") or 0)
        form = urllib.parse.parse_qs(self.rfile.read(n).decode(errors="replace"))
        addr = self.client_address[0]
        with lock:
            self.forget()
            if u.path == "/leave":
                games.pop(addr, None)
                return self.reply("ok\n")
            if u.path != "/host":
                return self.reply("not here\n", 404)
            name = "".join(c for c in form.get("name", [""])[0] if ord(c) >= 32 and ord(c) != 127).strip()[:24] or "PS Vita"
            try: players = max(1, min(4, int(form.get("players", ["1"])[0])))
            except ValueError: players = 1
            old = games.get(addr)
            games[addr] = dict(name=name, players=players, v=form.get("v", [""])[0][:8], seen=time.time(), since=old["since"] if old else time.time())
        self.reply("ok\n")

    def log_message(self, fmt, *a):
        sys.stderr.write("lobby: " + fmt % a + "\n")

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8787
    ThreadingHTTPServer(("0.0.0.0", port), H).serve_forever()
