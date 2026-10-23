// The game's side of Wi-Fi co-op (net.h): starting a game VM the same way on every Vita and
// running each frame from everyone's buttons.
#ifndef NETPLAY_H
#define NETPLAY_H

#include "as.h"
#include "net.h"

// A new game VM in the Wi-Fi game: the host's seed, a clock from the frame count, every
// player's profile on their port, their names. After cc_host_init, before the first tick.
void netplay_begin(AsVm *vm);
void netplay_end(void);

// Frame t (from 0): 0 while it can't run yet. Else submits this Vita's input for t +
// net.delay, sets cc_keys[] and the ports' states from frame t, and returns 1: run as_tick.
// no_start: Start means nothing to the game now (the pause menu has it in levels and the map).
int netplay_step(AsVm *vm, uint32_t t, NetInput *mine, int no_start);

// The level file the game is waiting for: everyone must have it before it goes on (how long a
// file takes differs between Vitas). Use as vm->prefetch; the platform reads the file in the
// background and says when it's here with netplay_file_ready.
int netplay_prefetch(AsVm *vm, const char *url);
const char *netplay_file_wanted(void);   // the file asked for and not ready here yet, or NULL
void netplay_file_ready(void);

// Each player's name over their knight's head (the Wi-Fi game), in the game font, through
// screen (the game's root -> screen pixels).
#include "render.h"
void netplay_nametags(AsVm *vm, Mat screen, DrawList *out);

uint16_t netplay_check(AsVm *vm);   // a summary of the game's state, to compare between Vitas

#endif
