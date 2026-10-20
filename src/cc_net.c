// Castle Crashers over netplay.c (see cc_net.h).
#include "cc_net.h"
#include "cc_host.h"

#include <stdio.h>
#include <string.h>

int cc_net_prepare(Netplay *np, char *err, int errlen) {
    int n = cc_storage_pack(0, np->profile[0], NP_PROFILE_MAX);
    if (n < 0) {
        snprintf(err, errlen, "The save is too big to send (over %d bytes)", NP_PROFILE_MAX);
        return 0;
    }
    np->profile_len[0] = n;
    return 1;
}

int cc_net_begin(Netplay *np, char *err, int errlen) {
    // the file's other players are kept aside first: the session's go in their places
    if (!cc_storage_net_begin(np->local_port)) {
        snprintf(err, errlen, "Out of memory for the save");
        return 0;
    }
    for (int p = 0; p < NP_PORTS; p++) {
        if (np->ports & (1u << p)) cc_storage_unpack(p, np->profile[p], np->profile_len[p]);
        else cc_storage_unpack(p, NULL, 0);   // nobody plays it: the same empty port everywhere
    }
    return 1;
}

void cc_net_vm(Netplay *np, AsVm *vm) {
    vm->rand_state = np->seed;
    vm->fixed_clock = 1;
}

void cc_net_keys(Netplay *np) {
    uint32_t in[NP_PORTS];
    np_inputs(np, in);
    memset(cc_keys, 0, sizeof cc_keys);
    for (int p = 0; p < NP_PORTS; p++)
        if (np->ports & (1u << p)) cc_press(p, in[p]);
}

static uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

void cc_net_after_tick(Netplay *np, AsVm *vm) {
    int32_t tick = np->next_tick;   // the tick that just ran (np_advance comes after)
    if (tick % 30) return;
    // the random numbers drawn and the actions run: both change as soon as two games differ
    uint32_t h = mix(mix(2166136261u, vm->rand_state), (uint32_t)vm->ops);
    np_check(np, tick, mix(h, (uint32_t)(vm->ops >> 32)));
}

uint32_t cc_buttons_from_keys(void) {
    uint32_t b = 0;
    for (int i = 0; i < CCB_COUNT; i++)
        if (cc_port_keys[0][i] && cc_keys[cc_port_keys[0][i]]) b |= 1u << i;
    return b;
}
