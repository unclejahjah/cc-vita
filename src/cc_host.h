// Castle Crashers' executable-side functions for the ActionScript engine.
#ifndef CC_HOST_H
#define CC_HOST_H

#include "as.h"

// Key codes the scripts read with Key.isDown (see f_GetButtons); set by the platform.
extern uint8_t cc_keys[256];

// The controller buttons the platform reads (bit CCB_x); cc_press adds one port's to
// cc_keys as that port's key codes (cc_port_keys).
enum {
    CCB_LEFT, CCB_UP, CCB_RIGHT, CCB_DOWN,                  // full tilt or D-pad: run
    CCB_WALK_LEFT, CCB_WALK_UP, CCB_WALK_RIGHT, CCB_WALK_DOWN,   // light tilt: walk
    CCB_ACCEPT, CCB_BACK, CCB_START, CCB_SELECT, CCB_PUNCH1, CCB_PUNCH2, CCB_JUMP, CCB_PROJECTILE,
    CCB_SHIELD, CCB_MAGIC, CCB_L2, CCB_R2, CCB_COUNT
};
extern uint8_t cc_port_keys[4][CCB_COUNT];
void cc_press(int port, uint32_t buttons);

void cc_host_init(AsVm *vm);

// The executable-side settings the scripts read with GetFlashGlobal.
AsVal cc_flash_global(AsVm *vm, const char *name);
void cc_set_flash_global(AsVm *vm, const char *name, AsVal v);
void cc_native_init(AsVm *vm);   // native versions of hot game functions (cc_native.c); called by cc_host_init
// For files shown without MAIN.swf (the title menu): the _root functions their prompt icons
// call (f_GetButtonArt), defined on that root.
void cc_native_root_functions(AsVm *vm, AsObj *root);
// The title's menu panel (MENU.swf "buttonchoices"), trimmed to the Vita's three items, at x, y
// (stage px); NULL if the file has no panel. cc_menu_highlight enlarges item 0..2.
enum { CC_MENU_PLAY, CC_MENU_OPTIONS, CC_MENU_QUIT, CC_MENU_ITEMS };
AsMc *cc_menu_attach(AsVm *vm, float x, float y);
void cc_menu_highlight(AsVm *vm, AsMc *panel, int item);

// Save data per controller port (ReadStorage/WriteStorage); dirty when the game wrote to it.
double *cc_storage(int port);
void cc_own_dlc_characters(void);   // the DLC characters are unlocked in every profile
extern int cc_storage_dirty;
extern int cc_save_requested;           // WriteSaveGame was called: write the save file
int cc_storage_save(const char *path);  // 1 on success
int cc_storage_load(const char *path);  // 1 if a save was read
// Network games: a port's save data packed to send (bytes, -1 if over cap), and unpacked
// into a port. After cc_storage_net_begin, saving writes local_port's data as the file's
// player 1 and keeps the file's other players as they were.
int cc_storage_pack(int port, uint8_t *out, int cap);
void cc_storage_unpack(int port, const uint8_t *in, int len);
int cc_storage_net_begin(int local_port);

// Puts back layer placements a cut-short level file lost; returns how many. Call after swf_load.
int cc_repair_level(Movie *m);

// Music the platform plays (name: the file's base name without extension, e.g. "MUDHOLES").
extern void (*cc_music_play)(const char *name, int loop);
extern void (*cc_music_stop)(void);

#endif
