# Public games list

The server behind Multiplayer > Public Games (`src/lobby.c` is the Vita's side). A host that
picks Host a Public Game says "a game here" every 30 s; the server lists it at the address it
sees the request come from (so nobody can list anyone else) and forgets it 90 s after the last
word. Joining is then straight Vita to Vita, as with a game code.

- `src/worker.js` + `wrangler.toml`: the real one, a free Cloudflare Worker (one Durable Object
  keeps the games in memory).
- `lobby_server.py`: the same answers on a PC, for tests (`hosttest` with
  `LOBBY_URL=http://127.0.0.1:8787`, `NET_PUBLIC=1` joins the first listed game).

## Put it online (once)

Needs a free Cloudflare account and Node.js.

    cd tools/lobby
    npx wrangler login      # opens the browser: Allow
    npx wrangler deploy     # prints https://cc-vita-lobby.<you>.workers.dev

Then put that address in `CC_LOBBY_URL` (`src/cc_host.h`) and build, or (no rebuild) write it
as the first line of `ux0:data/CastleCrashers/lobby.txt` on the Vita.

Check it: `curl https://cc-vita-lobby.<you>.workers.dev/list?v=3` (empty when nobody hosts).
