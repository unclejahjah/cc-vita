// Lockstep netplay (see netplay.h). The packets, all little-endian, start with "CN", the
// version and the type:
//   HELLO     client -> host   (until the host answers with LOBBY)
//   LOBBY     host -> client   port u8, profile chunks received u64
//   PROF      client -> host   chunk u8, chunks u8, length u16, bytes: its own save data
//   PEERPROF  host -> client   port u8, chunk u8, chunks u8, length u16, bytes: the others'
//   PEERACK   client -> host   u64 per port: the chunks of the others' save data it has
//   START     host -> client   seed u32, ports u8, delay u8
//   INPUT     client -> host   frames acked s32, hash tick s32, hash u32, first tick s32,
//                              count u8, count x buttons u32
//   FRAME     host -> client   inputs acked s32, desync tick s32, first tick s32, count u8,
//                              count x 4 x buttons u32
// Every packet repeats what the other side hasn't acknowledged yet, so a lost one only
// costs time.
#include "netplay.h"

#include <stdio.h>
#include <string.h>

#define NP_VERSION 1
enum { P_HELLO = 1, P_LOBBY, P_PROF, P_PEERPROF, P_PEERACK, P_START, P_INPUT, P_FRAME };

#define SEND_EVERY_MS 15     // inputs and frames
#define LOBBY_EVERY_MS 100   // lobby traffic
#define HELLO_EVERY_MS 250
#define PER_PACKET 30        // ticks of inputs or frames in one packet
#define CHUNKS_PER_SEND 8

typedef struct { uint8_t *p; int n, cap; } Wr;
typedef struct { const uint8_t *p; int n, at, bad; } Rd;

static void w8(Wr *w, uint32_t v) { if (w->n < w->cap) w->p[w->n++] = (uint8_t)v; }
static void w16(Wr *w, uint32_t v) { w8(w, v); w8(w, v >> 8); }
static void w32(Wr *w, uint32_t v) { w16(w, v); w16(w, v >> 16); }
static void w64(Wr *w, uint64_t v) { w32(w, (uint32_t)v); w32(w, (uint32_t)(v >> 32)); }
static void wbytes(Wr *w, const void *s, int n) {
    if (w->n + n <= w->cap) { memcpy(w->p + w->n, s, n); w->n += n; }
}
static uint32_t r8(Rd *r) { if (r->at >= r->n) { r->bad = 1; return 0; } return r->p[r->at++]; }
static uint32_t r16(Rd *r) { uint32_t a = r8(r); return a | r8(r) << 8; }
static uint32_t r32(Rd *r) { uint32_t a = r16(r); return a | r16(r) << 16; }
static uint64_t r64(Rd *r) { uint64_t a = r32(r); return a | (uint64_t)r32(r) << 32; }

static Wr packet(uint8_t *buf, int cap, int type) {
    Wr w = {buf, 0, cap};
    w8(&w, 'C'); w8(&w, 'N'); w8(&w, NP_VERSION); w8(&w, type);
    return w;
}

static uint64_t all_chunks(int total) { return total >= 64 ? ~0ull : (1ull << total) - 1; }
static int chunks_of(int len) { return len ? (len + NP_CHUNK - 1) / NP_CHUNK : 1; }   // empty data is one empty chunk

static void fail(Netplay *np, const char *msg) {
    if (np->state == NP_FAILED) return;
    np->state = NP_FAILED;
    snprintf(np->error, sizeof np->error, "%s", msg);
}

// the ring slot for a tick, emptied if it held an older one
static int slot(Netplay *np, int32_t tick) {
    int s = (int)((uint32_t)tick % NP_WINDOW);
    if (np->tick_of[s] != tick) {
        np->tick_of[s] = tick;
        np->have[s] = 0;
        memset(np->inputs[s], 0, sizeof np->inputs[s]);
    }
    return s;
}
static int known(const Netplay *np, int32_t tick, unsigned ports) {
    int s = (int)((uint32_t)tick % NP_WINDOW);
    return np->tick_of[s] == tick && (np->have[s] & ports) == ports;
}

static void reset(Netplay *np, NpTransport t, int is_host) {
    // keep the app's own save data across the reset
    uint8_t *own = np->profile[0];
    int own_len = np->profile_len[0];
    static uint8_t keep[NP_PROFILE_MAX];
    memcpy(keep, own, own_len);
    memset(np, 0, sizeof *np);
    memcpy(np->profile[0], keep, own_len);
    np->profile_len[0] = own_len;
    np->t = t;
    np->is_host = is_host;
    np->state = NP_LOBBY;
    np->local_port = is_host ? 0 : -1;
    np->desync_tick = np->hash_tick = -1;
    np->input_acked = np->frames_upto = np->local_upto = -1;
    for (int i = 0; i < NP_WINDOW; i++) np->tick_of[i] = -1;
    for (int i = 0; i < 16; i++) np->hash_ticks[i] = -1;
}

void np_host(Netplay *np, NpTransport t, int delay) {
    reset(np, t, 1);
    np->delay = delay < 1 ? 1 : delay > 10 ? 10 : delay;
    np->ports = 1;
    np->prof_total[0] = chunks_of(np->profile_len[0]);
    np->prof_have[0] = all_chunks(np->prof_total[0]);
}

void np_join(Netplay *np, NpTransport t) {
    int len = np->profile_len[0];
    reset(np, t, 0);
    // a client's own data travels to the host, then comes back for the port it gets
    np->prof_total[0] = chunks_of(len);
}

int np_clients(const Netplay *np) {
    int n = 0;
    for (int i = 1; i < NP_PORTS; i++) n += np->peer[i].used;
    return n;
}

int np_lobby_ready(const Netplay *np) {
    for (int i = 1; i < NP_PORTS; i++) {
        const NpPeer *c = &np->peer[i];
        if (c->used && (!c->prof_total || c->prof_have != all_chunks(c->prof_total))) return 0;
    }
    return 1;
}

// inputs before the first DELAY ticks: nobody pressed anything yet
static void prefill(Netplay *np) {
    for (int32_t t = 0; t < np->delay; t++) np->have[slot(np, t)] = np->ports;
    np->local_upto = np->delay - 1;
    if (!np->is_host) np->frames_upto = np->delay - 1;
}

void np_start(Netplay *np, uint32_t seed) {
    if (!np->is_host || np->state != NP_LOBBY) return;
    np->seed = seed;
    np->state = NP_RUNNING;
    for (int i = 1; i < NP_PORTS; i++) np->peer[i].frame_acked = np->peer[i].input_have = -1;
    prefill(np);
}

void np_local_input(Netplay *np, uint32_t buttons) {
    if (np->state != NP_RUNNING) return;
    int32_t t = np->next_tick + np->delay;
    if (t <= np->local_upto) return;   // already given for this tick
    int s = slot(np, t);
    np->inputs[s][np->local_port] = buttons;
    np->have[s] |= 1u << np->local_port;
    np->local_upto = t;
}

int np_ready(const Netplay *np) {
    if (np->state != NP_RUNNING) return 0;
    if (!np->is_host) return np->frames_upto >= np->next_tick;
    if (!known(np, np->next_tick, np->ports)) return 0;
    // don't run so far ahead that the ring forgets frames a client still needs
    for (int i = 1; i < NP_PORTS; i++)
        if (np->peer[i].used && np->next_tick + np->delay + 1 - np->peer[i].frame_acked >= NP_WINDOW) return 0;
    return 1;
}

void np_inputs(const Netplay *np, uint32_t out[NP_PORTS]) {
    int s = (int)((uint32_t)np->next_tick % NP_WINDOW);
    for (int p = 0; p < NP_PORTS; p++) out[p] = np->tick_of[s] == np->next_tick ? np->inputs[s][p] : 0;
}

void np_advance(Netplay *np) { if (np_ready(np)) np->next_tick++; }

void np_check(Netplay *np, int32_t tick, uint32_t hash) {
    if (np->is_host) {
        int i = (int)((uint32_t)(tick / 30) % 16);
        np->hash_ticks[i] = tick;
        np->hashes[i] = hash;
    } else {
        np->hash_tick = tick;
        np->hash = hash;
    }
}

// ---- host ---------------------------------------------------------------------------

static void host_compare(Netplay *np, int32_t tick, uint32_t hash) {
    if (tick < 0) return;
    int i = (int)((uint32_t)(tick / 30) % 16);
    if (np->hash_ticks[i] == tick && np->hashes[i] != hash && (np->desync_tick < 0 || tick < np->desync_tick))
        np->desync_tick = tick;
}

static void host_drop(Netplay *np, int id) {
    NpPeer *c = &np->peer[id];
    if (c->port > 0) {
        np->ports &= ~(1u << c->port);
        np->profile_len[c->port] = np->prof_total[c->port] = 0;
        np->prof_have[c->port] = 0;
    }
    memset(c, 0, sizeof *c);
    if (np->t.drop) np->t.drop(np->t.ctx, id);
}

static void host_packet(Netplay *np, int id, Rd *r, int type) {
    if (id < 1 || id >= NP_PORTS) return;
    NpPeer *c = &np->peer[id];
    if (type == P_HELLO) {
        if (c->used || np->state != NP_LOBBY) return;   // no joining a game that has started
        int port = 1;
        while (port < NP_PORTS && (np->ports & (1u << port))) port++;
        if (port >= NP_PORTS) return;
        memset(c, 0, sizeof *c);
        c->used = 1;
        c->port = port;
        c->heard_ms = np->now_ms;
        np->ports |= 1u << port;
        return;
    }
    if (!c->used) return;
    c->heard_ms = np->now_ms;
    int port = c->port;
    if (type == P_PROF) {
        int idx = r8(r), total = r8(r), len = r16(r);
        if (r->bad || total < 1 || total > 64 || idx >= total || len > NP_CHUNK || r->at + len > r->n) return;
        if (idx * NP_CHUNK + len > NP_PROFILE_MAX) return;
        memcpy(np->profile[port] + idx * NP_CHUNK, r->p + r->at, len);
        if (idx == total - 1) np->profile_len[port] = idx * NP_CHUNK + len;
        c->prof_total = np->prof_total[port] = total;
        c->prof_have |= 1ull << idx;
        np->prof_have[port] = c->prof_have;
    } else if (type == P_PEERACK) {
        for (int p = 0; p < NP_PORTS; p++) c->peer_have[p] = r64(r);
    } else if (type == P_INPUT) {
        int32_t acked = (int32_t)r32(r), htick = (int32_t)r32(r);
        uint32_t hash = r32(r);
        int32_t first = (int32_t)r32(r);
        int count = r8(r);
        if (r->bad || np->state != NP_RUNNING) return;
        c->started = 1;
        if (acked > c->frame_acked) c->frame_acked = acked;
        host_compare(np, htick, hash);
        for (int k = 0; k < count; k++) {
            uint32_t b = r32(r);
            int32_t t = first + k;
            if (r->bad) break;
            if (t > c->input_have + 1) break;   // a gap: wait for the resend
            if (t <= c->input_have) continue;
            if (t < np->next_tick) { c->input_have = t; continue; }   // a tick that already ran (the first DELAY)
            if (t >= np->next_tick + NP_WINDOW / 2) break;
            int s = slot(np, t);
            np->inputs[s][port] = b;
            np->have[s] |= 1u << port;
            c->input_have = t;
        }
    }
}

// the last tick whose frame is complete, counting on from 'from'
static int32_t complete_upto(const Netplay *np, int32_t from) {
    int32_t t = from;
    while (t + 1 <= np->local_upto && known(np, t + 1, np->ports)) t++;
    return t;
}

static void host_send(Netplay *np) {
    uint8_t buf[1400];
    int lobby_due = np->now_ms - np->lobby_ms >= LOBBY_EVERY_MS;
    if (lobby_due) np->lobby_ms = np->now_ms;
    int frames_due = np->now_ms - np->last_send_ms >= SEND_EVERY_MS;
    if (frames_due) np->last_send_ms = np->now_ms;
    int32_t done = complete_upto(np, np->next_tick - 1);
    for (int id = 1; id < NP_PORTS; id++) {
        NpPeer *c = &np->peer[id];
        if (!c->used) continue;
        if (np->state == NP_LOBBY) {
            if (!lobby_due) continue;
            Wr w = packet(buf, sizeof buf, P_LOBBY);
            w8(&w, c->port);
            w64(&w, c->prof_have);
            np->t.send(np->t.ctx, id, buf, w.n);
            continue;
        }
        if (!c->started) {
            if (!lobby_due) continue;
            // the others' save data, then START once it has all of it
            int sent = 0, missing = 0;
            for (int p = 0; p < NP_PORTS && sent < CHUNKS_PER_SEND; p++) {
                if (p == c->port || !(np->ports & (1u << p))) continue;
                for (int k = 0; k < np->prof_total[p] && sent < CHUNKS_PER_SEND; k++) {
                    if (c->peer_have[p] & (1ull << k)) continue;
                    missing = 1;
                    int len = np->profile_len[p] - k * NP_CHUNK;
                    if (len > NP_CHUNK) len = NP_CHUNK;
                    if (len < 0) len = 0;
                    Wr w = packet(buf, sizeof buf, P_PEERPROF);
                    w8(&w, p); w8(&w, k); w8(&w, np->prof_total[p]); w16(&w, len);
                    wbytes(&w, np->profile[p] + k * NP_CHUNK, len);
                    np->t.send(np->t.ctx, id, buf, w.n);
                    sent++;
                }
            }
            for (int p = 0; p < NP_PORTS && !missing; p++)
                if (p != c->port && (np->ports & (1u << p)) && c->peer_have[p] != all_chunks(np->prof_total[p]))
                    missing = 1;
            if (!missing) {
                Wr w = packet(buf, sizeof buf, P_START);
                w32(&w, np->seed); w8(&w, np->ports); w8(&w, np->delay);
                np->t.send(np->t.ctx, id, buf, w.n);
            }
            continue;
        }
        if (!frames_due) continue;
        Wr w = packet(buf, sizeof buf, P_FRAME);
        int32_t first = c->frame_acked + 1;
        int count = done - first + 1;
        if (count < 0) count = 0;
        if (count > PER_PACKET) count = PER_PACKET;
        w32(&w, (uint32_t)c->input_have);
        w32(&w, (uint32_t)np->desync_tick);
        w32(&w, (uint32_t)first);
        w8(&w, count);
        for (int k = 0; k < count; k++) {
            int s = (int)((uint32_t)(first + k) % NP_WINDOW);
            for (int p = 0; p < NP_PORTS; p++) w32(&w, np->inputs[s][p]);
        }
        np->t.send(np->t.ctx, id, buf, w.n);
    }
}

// ---- client -------------------------------------------------------------------------

static void client_packet(Netplay *np, Rd *r, int type) {
    np->last_heard_ms = np->now_ms;
    if (type == P_LOBBY) {
        int port = r8(r);
        uint64_t have = r64(r);
        if (r->bad || port < 1 || port >= NP_PORTS || np->state != NP_LOBBY) return;
        if (np->local_port < 0) np->local_port = port;
        np->prof_have[0] = have;   // what the host has of ours (sent from slot 0)
    } else if (type == P_PEERPROF) {
        int p = r8(r), idx = r8(r), total = r8(r), len = r16(r);
        if (r->bad || np->local_port < 0 || p >= NP_PORTS || p == np->local_port || np->state != NP_LOBBY) return;
        if (total < 1 || total > 64 || idx >= total || len > NP_CHUNK || r->at + len > r->n) return;
        // slot 0 holds our own data until START moves it to our port, so the host's (port 0)
        // is collected in our port's slot and the two swap then
        int q = p == 0 ? np->local_port : p;
        memcpy(np->profile[q] + idx * NP_CHUNK, r->p + r->at, len);
        if (idx == total - 1) np->profile_len[q] = idx * NP_CHUNK + len;
        np->prof_total[q] = total;
        np->prof_have[q] |= 1ull << idx;
    } else if (type == P_START) {
        uint32_t seed = r32(r);
        unsigned ports = r8(r);
        int delay = r8(r);
        if (r->bad || np->state != NP_LOBBY || np->local_port < 0) return;
        np->seed = seed;
        np->ports = ports;
        np->delay = delay;
        // ours goes to our port, the host's (collected in our port's slot) to port 0
        int q = np->local_port;
        static uint8_t tmp[NP_PROFILE_MAX];
        int len = np->profile_len[0];
        memcpy(tmp, np->profile[0], len);
        memcpy(np->profile[0], np->profile[q], np->profile_len[q]);
        np->profile_len[0] = np->profile_len[q];
        memcpy(np->profile[q], tmp, len);
        np->profile_len[q] = len;
        np->prof_total[0] = np->prof_total[q];
        np->prof_have[0] = np->prof_have[q];
        np->prof_total[q] = chunks_of(len);
        np->prof_have[q] = all_chunks(np->prof_total[q]);
        np->state = NP_RUNNING;
        prefill(np);
    } else if (type == P_FRAME) {
        int32_t acked = (int32_t)r32(r), desync = (int32_t)r32(r), first = (int32_t)r32(r);
        int count = r8(r);
        if (r->bad || np->state != NP_RUNNING) return;
        if (acked > np->input_acked) np->input_acked = acked;
        if (desync >= 0) np->desync_tick = desync;
        for (int k = 0; k < count; k++) {
            uint32_t b[NP_PORTS];
            for (int p = 0; p < NP_PORTS; p++) b[p] = r32(r);
            int32_t t = first + k;
            if (r->bad) break;
            if (t != np->frames_upto + 1) continue;
            int s = slot(np, t);
            memcpy(np->inputs[s], b, sizeof b);
            np->have[s] = np->ports;
            np->frames_upto = t;
        }
    }
}

// a client's own save data is sent from slot 0 (it moves to its port at START)
static void client_send(Netplay *np) {
    uint8_t buf[1400];
    if (np->state == NP_LOBBY) {
        if (np->now_ms - np->last_send_ms < (np->local_port < 0 ? HELLO_EVERY_MS : LOBBY_EVERY_MS)) return;
        np->last_send_ms = np->now_ms;
        if (np->local_port < 0) {
            Wr w = packet(buf, sizeof buf, P_HELLO);
            np->t.send(np->t.ctx, 0, buf, w.n);
            return;
        }
        int sent = 0;
        for (int k = 0; k < np->prof_total[0] && sent < CHUNKS_PER_SEND; k++) {
            if (np->prof_have[0] & (1ull << k)) continue;
            int len = np->profile_len[0] - k * NP_CHUNK;
            if (len > NP_CHUNK) len = NP_CHUNK;
            if (len < 0) len = 0;
            Wr w = packet(buf, sizeof buf, P_PROF);
            w8(&w, k); w8(&w, np->prof_total[0]); w16(&w, len);
            wbytes(&w, np->profile[0] + k * NP_CHUNK, len);
            np->t.send(np->t.ctx, 0, buf, w.n);
            sent++;
        }
        Wr w = packet(buf, sizeof buf, P_PEERACK);
        for (int p = 0; p < NP_PORTS; p++) {
            // the host's chunks are collected in our port's slot until START
            int q = p == 0 ? np->local_port : p == np->local_port ? -1 : p;
            w64(&w, q < 0 ? 0 : np->prof_have[q]);
        }
        np->t.send(np->t.ctx, 0, buf, w.n);
        return;
    }
    if (np->now_ms - np->last_send_ms < SEND_EVERY_MS) return;
    np->last_send_ms = np->now_ms;
    Wr w = packet(buf, sizeof buf, P_INPUT);
    int32_t first = np->input_acked + 1;
    int count = np->local_upto - first + 1;
    if (count < 0) count = 0;
    if (count > PER_PACKET) count = PER_PACKET;
    w32(&w, (uint32_t)np->frames_upto);
    w32(&w, (uint32_t)np->hash_tick);
    w32(&w, np->hash);
    w32(&w, (uint32_t)first);
    w8(&w, count);
    for (int k = 0; k < count; k++) {
        int s = (int)((uint32_t)(first + k) % NP_WINDOW);
        w32(&w, np->tick_of[s] == first + k ? np->inputs[s][np->local_port] : 0);
    }
    np->t.send(np->t.ctx, 0, buf, w.n);
}

void np_poll(Netplay *np, double now_ms) {
    if (np->state == NP_OFF || np->state == NP_FAILED) return;
    np->now_ms = now_ms;
    if (!np->is_host && !np->last_heard_ms) np->last_heard_ms = now_ms;
    uint8_t buf[1500];
    int peer, n;
    while ((n = np->t.recv(np->t.ctx, buf, sizeof buf, &peer)) > 0) {
        Rd r = {buf, n, 4, 0};
        if (n < 4 || buf[0] != 'C' || buf[1] != 'N' || buf[2] != NP_VERSION) continue;
        if (np->is_host) host_packet(np, peer, &r, buf[3]);
        else if (peer == 0) client_packet(np, &r, buf[3]);
    }
    if (np->is_host) {
        for (int id = 1; id < NP_PORTS; id++) {
            NpPeer *c = &np->peer[id];
            if (!c->used || now_ms - c->heard_ms < NP_TIMEOUT_MS) continue;
            if (np->state == NP_LOBBY) host_drop(np, id);
            else {
                char msg[64];
                snprintf(msg, sizeof msg, "Player %d left the game", c->port + 1);
                fail(np, msg);
                return;
            }
        }
        host_send(np);
    } else {
        // waiting for a host to answer HELLO has no time limit (the app offers a cancel)
        if (np->local_port >= 0 && now_ms - np->last_heard_ms >= NP_TIMEOUT_MS) {
            fail(np, "Lost the connection to the host");
            return;
        }
        client_send(np);
    }
}
