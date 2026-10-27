// The public games list for Castle Crashers Vita (see src/lobby.c for the Vita's side).
// A Cloudflare Worker with one Durable Object holding the games in memory: a host says
// "a game here" every 30 s, so a game nobody has heard from in 90 s is gone.
//
//   POST /host   v=3&name=Jah&players=1   the game at the sender's own address
//   POST /leave  v=3                      the sender's game is gone
//   GET  /list?v=3                        address <tab> players <tab> name, a line each
//
// Only the address Cloudflare sees the request from is listed, so nobody can list someone else.

const FORGET_MS = 90 * 1000;
const MAX_GAMES = 500;    // in all
const MAX_LISTED = 24;    // in one reply
const NAME_MAX = 24;

const text = (body, status = 200) =>
  new Response(body, { status, headers: { "content-type": "text/plain; charset=utf-8", "cache-control": "no-store" } });

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/" || url.pathname === "") return text("Castle Crashers Vita public games\n");
    if (!["/host", "/leave", "/list"].includes(url.pathname)) return text("not here\n", 404);
    const lobby = env.LOBBY.get(env.LOBBY.idFromName("lobby"));
    return lobby.fetch(request);
  },
};

export class Lobby {
  constructor(state) {
    this.state = state;
    this.games = new Map();   // address -> {name, players, v, seen}
  }

  forget(now) {
    for (const [a, g] of this.games) if (now - g.seen > FORGET_MS) this.games.delete(a);
  }

  async fetch(request) {
    const url = new URL(request.url);
    const now = Date.now();
    this.forget(now);
    if (url.pathname === "/list") {
      if (request.method !== "GET") return text("GET\n", 405);
      const v = url.searchParams.get("v") || "";
      const lines = [...this.games.entries()]
        .filter(([, g]) => g.v === v && g.players < 4)
        .sort((x, y) => y[1].since - x[1].since)
        .slice(0, MAX_LISTED)
        .map(([a, g]) => `${a}\t${g.players}\t${g.name}\n`);
      return text(lines.join(""));
    }
    if (request.method !== "POST") return text("POST\n", 405);
    const addr = request.headers.get("cf-connecting-ip") || "";
    if (!/^\d{1,3}(\.\d{1,3}){3}$/.test(addr)) return text("IPv4 only\n", 400);
    const form = new URLSearchParams(await request.text());
    const v = (form.get("v") || "").slice(0, 8);
    if (url.pathname === "/leave") {
      this.games.delete(addr);
      return text("ok\n");
    }
    const name = (form.get("name") || "").replace(/[\x00-\x1f\x7f]/g, "").trim().slice(0, NAME_MAX) || "PS Vita";
    const players = Math.max(1, Math.min(4, parseInt(form.get("players") || "1", 10) || 1));
    const old = this.games.get(addr);
    if (!old && this.games.size >= MAX_GAMES) return text("full\n", 503);
    this.games.set(addr, { name, players, v, seen: now, since: old ? old.since : now });
    return text("ok\n");
  }
}
