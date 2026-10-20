// Netplay test: a host and three clients in one process, over localhost UDP that loses a
// share of the packets. Each runs a stand-in game whose state folds in every input; the
// test passes when all four ran the same ticks with the same inputs, every save profile
// arrived intact, and a hash that differs on purpose is caught.
//   gcc -O2 -Wall -Isrc -o nettest tools/nettest.c src/netplay.c src/net_posix.c
//   ./nettest [loss=0.2] [ticks=600]
#include "netplay.h"
#include "net_posix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 4
#define PORT 47310

static Netplay np[N];
static uint32_t state[N], trail[N][4096];
static int32_t ran[N];

static uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u + 0x9E3779B9u; }

int main(int argc, char **argv) {
    double loss = argc > 1 ? atof(argv[1]) : 0.2;
    int ticks = argc > 2 ? atoi(argv[2]) : 600;
    if (ticks > 4096) ticks = 4096;
    char err[128];
    NpTransport t[N];
    static const int sizes[N] = {2500, 0, 30000, NP_PROFILE_MAX};
    static uint8_t own[N][NP_PROFILE_MAX];
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < sizes[i]; k++) own[i][k] = (uint8_t)(k * 31 + i * 7 + k / 251);
        memcpy(np[i].profile[0], own[i], sizes[i]);
        np[i].profile_len[0] = sizes[i];
        if (!(i ? net_udp_join(&t[i], "127.0.0.1", PORT, err, sizeof err) : net_udp_host(&t[i], PORT, err, sizeof err))) {
            printf("FAIL: %s\n", err);
            return 1;
        }
        net_udp_set_loss(&t[i], loss);
        if (i) np_join(&np[i], t[i]);
        else np_host(&np[0], t[0], 3);
    }

    int drift = ticks / 2 / 30 * 30;   // console 2's hash is wrong on this tick
    double now = 1;
    int started = 0, fails = 0;
    double tick_ms = 1000.0 / 30, next_tick[N] = {0};
    for (int step = 0; step < 200000; step++, now += 1) {   // 1 ms steps
        for (int i = 0; i < N; i++) np_poll(&np[i], now);
        if (!started && np_clients(&np[0]) == N - 1 && np_lobby_ready(&np[0])) {
            np_start(&np[0], 1234567);
            started = 1;
            printf("lobby done at %.0f ms\n", now);
        }
        int done = 1;
        for (int i = 0; i < N; i++) {
            if (np[i].state == NP_FAILED) { printf("FAIL: console %d: %s\n", i, np[i].error); return 1; }
            if (ran[i] < ticks) done = 0;
            if (np[i].state != NP_RUNNING || ran[i] >= ticks || now < next_tick[i]) continue;
            // a made-up controller: different on every console and tick
            np_local_input(&np[i], mix(np[i].next_tick * 7 + 1, np[i].local_port + 1) & 0xFFFFF);
            if (!np_ready(&np[i])) continue;
            uint32_t in[NP_PORTS];
            np_inputs(&np[i], in);
            int32_t tk = np[i].next_tick;
            for (int p = 0; p < NP_PORTS; p++) state[i] = mix(state[i], in[p]);
            state[i] = mix(state[i], np[i].seed);
            trail[i][tk] = state[i];
            if (tk % 30 == 0) np_check(&np[i], tk, state[i] ^ (i == 2 && tk == drift ? 1 : 0));
            np_advance(&np[i]);
            ran[i]++;
            // on a fixed clock, catching up after a stall like the game loop does
            next_tick[i] = next_tick[i] ? next_tick[i] + tick_ms : now + tick_ms;
            if (next_tick[i] < now - 2 * tick_ms) next_tick[i] = now - 2 * tick_ms;
        }
        if (done) break;
    }

    for (int i = 0; i < N; i++)
        if (ran[i] < ticks) { printf("FAIL: console %d ran %d of %d ticks\n", i, ran[i], ticks); fails++; }
    for (int i = 1; i < N && !fails; i++)
        for (int k = 0; k < ticks; k++)
            if (trail[i][k] != trail[0][k]) { printf("FAIL: console %d differs at tick %d\n", i, k); fails++; break; }
    // every console has every port's save data; a client's own moved to its port
    for (int i = 0; i < N; i++)
        for (int c = 0; c < N; c++) {
            int port = c ? np[c].local_port : 0;
            if (np[i].profile_len[port] != sizes[c] || memcmp(np[i].profile[port], own[c], sizes[c])) {
                printf("FAIL: console %d has the wrong save data for port %d (%d bytes, want %d)\n", i, port,
                       np[i].profile_len[port], sizes[c]);
                fails++;
            }
        }
    for (int i = 0; i < N; i++)
        if (np[i].desync_tick != drift) {
            printf("FAIL: console %d desync tick %d, want %d\n", i, np[i].desync_tick, drift);
            fails++;
        }
    printf("%s: %d consoles, %d ticks, %.0f%% loss, %.1f s of play in %.1f s\n", fails ? "FAIL" : "ok", N, ticks,
           loss * 100, ticks / 30.0, now / 1000);
    return fails != 0;
}
