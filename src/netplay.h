// Lockstep netplay: every console runs the whole game, and only the controller inputs travel.
// The game's scripts are deterministic once they all start from the same save data and random
// seed and see the same inputs on the same ticks, so tick N runs on every console once the
// inputs of every port for tick N are known. One console hosts: the others send it their
// inputs, and it sends everyone the full set ("frames"). A local press is used DELAY ticks
// later, which hides the round trip.
//
// Nothing here knows about sockets: a transport (net_posix.c, net_vita.c) moves the packets.
#ifndef CC_NETPLAY_H
#define CC_NETPLAY_H

#include <stdint.h>

#define NP_PORTS 4
#define NP_WINDOW 128          // ticks of inputs kept (about 4 s)
#define NP_PROFILE_MAX 64000   // bytes of one port's save data
#define NP_CHUNK 1000          // bytes per profile packet (so at most 64 chunks)
#define NP_TIMEOUT_MS 5000     // nothing heard from a peer for this long: it's gone

// Packets to and from peers. The host's peers are its clients (ids 1..3, the transport gives
// each new address the next free id); a client's only peer is the host (id 0).
typedef struct NpTransport {
    void *ctx;
    int (*send)(void *ctx, int peer, const void *buf, int len);
    int (*recv)(void *ctx, void *buf, int cap, int *peer);   // 0 when nothing is waiting
    void (*drop)(void *ctx, int peer);                        // forget a peer that left
} NpTransport;

enum { NP_OFF, NP_LOBBY, NP_RUNNING, NP_FAILED };

typedef struct NpPeer {             // the host's view of one client
    int used, port;
    double heard_ms;
    uint64_t prof_have;             // chunks of its profile received
    int prof_total;                 // 0 until the first chunk says
    uint64_t peer_have[NP_PORTS];   // chunks of the other ports' profiles it has (PEERACK)
    int started;                    // it sent inputs: it got START
    int32_t input_have;             // its inputs are known up to this tick
    int32_t frame_acked;            // it has the frames up to this tick
} NpPeer;

typedef struct Netplay {
    int state, is_host, local_port, delay;
    unsigned ports;                 // bit p: port p is played
    uint32_t seed;
    NpTransport t;
    char error[96];

    // save data per port, as the transport carried it (the app fills its own before hosting
    // or joining, and reads the others' once running)
    uint8_t profile[NP_PORTS][NP_PROFILE_MAX];
    int profile_len[NP_PORTS];
    uint64_t prof_have[NP_PORTS];   // chunks received of each port's
    int prof_total[NP_PORTS];

    // inputs: inputs[tick % NP_WINDOW][port], have[tick % NP_WINDOW] = ports known
    uint32_t inputs[NP_WINDOW][NP_PORTS];
    unsigned have[NP_WINDOW];
    int32_t tick_of[NP_WINDOW];     // which tick each slot holds
    int32_t next_tick;              // the tick the game runs next
    int32_t local_upto;             // local inputs are set up to this tick
    int32_t frames_upto;            // client: frames received contiguously up to here

    // client: what the host acknowledged, and the hash of the last checked tick
    int32_t input_acked;
    int32_t hash_tick;
    uint32_t hash;
    // host: its own hashes by check number, and the first tick that differed (-1: none)
    int32_t hash_ticks[16];
    uint32_t hashes[16];
    int32_t desync_tick;

    NpPeer peer[NP_PORTS];          // host only; index = peer id
    double now_ms, last_send_ms, last_heard_ms, lobby_ms;
} Netplay;

// Starts a session; the app has put its own save data in profile[0] / profile_len[0] (the
// host plays port 0; a client's lands in the port the host gives it).
void np_host(Netplay *np, NpTransport t, int delay);
void np_join(Netplay *np, NpTransport t);

// Call every frame: reads and sends packets. Lobby, then running, or failed (np->error).
void np_poll(Netplay *np, double now_ms);

// Host, in the lobby: how many clients joined, and whether all their save data arrived.
int np_clients(const Netplay *np);
int np_lobby_ready(const Netplay *np);
// Host: starts the game with this seed for the random numbers (everyone gets it).
void np_start(Netplay *np, uint32_t seed);

// Running. Each tick: give the local input (once per tick, before np_ready), then when
// np_ready says the tick's inputs are all in, read them, run the tick and np_advance.
void np_local_input(Netplay *np, uint32_t buttons);
int np_ready(const Netplay *np);
void np_inputs(const Netplay *np, uint32_t out[NP_PORTS]);
void np_advance(Netplay *np);
// After running a tick that is a multiple of 30: a hash of some game state, to catch the
// consoles drifting apart (np->desync_tick on the host, and on clients through the host).
void np_check(Netplay *np, int32_t tick, uint32_t hash);

#endif
