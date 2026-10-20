// Castle Crashers over netplay.c: what a network game needs from the game side. Every
// console starts from the same save data for every port, the same random seed and a clock
// that counts ticks, then plays each tick with every port's buttons.
#ifndef CC_NET_H
#define CC_NET_H

#include "as.h"
#include "netplay.h"

// Before hosting or joining: this console's save (port 0 of its file) goes in the session.
int cc_net_prepare(Netplay *np, char *err, int errlen);
// Once np is running, before the game's VM is made: every port's save data from the session
// (saving afterwards writes only this console's player).
int cc_net_begin(Netplay *np, char *err, int errlen);
// The game's VM: the session's seed and the tick clock.
void cc_net_vm(Netplay *np, AsVm *vm);
// cc_keys for one tick from every port's buttons (np_ready must be true).
void cc_net_keys(Netplay *np);
// After a tick: every 30 ticks, a hash of the game's progress to compare with the others.
void cc_net_after_tick(Netplay *np, AsVm *vm);
// Port 1's key codes in cc_keys back to buttons (the PC harness scripts keys as codes).
uint32_t cc_buttons_from_keys(void);

#endif
