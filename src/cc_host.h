// Castle Crashers' executable-side functions for the ActionScript engine.
#ifndef CC_HOST_H
#define CC_HOST_H

#include "as.h"

// Key codes the scripts read with Key.isDown (see f_GetButtons); set by the platform.
extern uint8_t cc_keys[256];

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

// In-game achievements: the game calls UnlockAchievement(port, id) with the Xbox/Steam ids
// 1..12. Unlocked ones are a bit set kept in a file of their own (not in the game's save).
#define CC_ACHIEVEMENTS 12
const char *cc_achievement_name(int id);        // id 1..12
const char *cc_achievement_how(int id);         // what it asks for
unsigned cc_achievements(void);                 // bit id set: unlocked
int cc_achievement_take_new(void);              // an id unlocked since the last call, or 0
void cc_achievements_load(const char *path);    // also where unlocks are written

// Puts back layer placements a cut-short level file lost; returns how many. Call after swf_load.
int cc_repair_level(Movie *m);

// Music the platform plays (name: the file's base name without extension, e.g. "MUDHOLES").
extern void (*cc_music_play)(const char *name, int loop);
extern void (*cc_music_stop)(void);

// the game's loading screen (LOADING.swf) over everything, played around level changes
void cc_loading_attach(AsVm *vm);
#endif
