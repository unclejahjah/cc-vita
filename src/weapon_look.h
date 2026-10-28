// Weapon looks (Vita add-on): in the weapon frog's armoury (LEVEL15), heavy attack (Triangle) in
// front of a weapon on the rack gives your weapon that weapon's look; its stats stay your own
// weapon's. Two more spots hang on a rope near the entrance: one shows your own weapon and
// puts its look back, the other (with a custom character painting, custom_char.h) gives your
// weapon the painting's weapon art. A look stays per player and character until it's put back,
// kept in ux0:data/CastleCrashers/weapon_looks.txt.
#ifndef CC_WEAPON_LOOK_H
#define CC_WEAPON_LOOK_H

#include "as.h"
#include "swf.h"

#define CC_LOOK_CUSTOM 1000   // a look: the custom character painting's weapon

// Where the looks are kept (read now, written as they change)
void cc_look_load(const char *path);
// Off in Wi-Fi games: the looks start empty and aren't kept (the other machine has its own file)
void cc_look_set_active(int on);

// The script changes (f_Weapon, the armoury's f_CheckGetWeapon); cc_host_init calls it.
void cc_look_init(AsVm *vm);

// as_render.c: the painting's weapon to draw instead of player part clip mc, or NULL
const BitmapFill *cc_look_custom_part(AsMc *mc);
// as_render.c: the armoury's custom spot draws the painting's weapon (placed by *at) under its own
const BitmapFill *cc_look_custom_spot(AsMc *mc, Mat *at);

#endif
