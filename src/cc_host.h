// Castle Crashers' executable-side functions for the ActionScript engine.
#ifndef CC_HOST_H
#define CC_HOST_H

#include "as.h"

// Key codes the scripts read with Key.isDown (see f_GetButtons); set by the platform.
extern uint8_t cc_keys[256];

void cc_host_init(AsVm *vm);

// Save data per controller port (ReadStorage/WriteStorage); dirty when the game wrote to it.
double *cc_storage(int port);
extern int cc_storage_dirty;
extern int cc_save_requested;           // WriteSaveGame was called: write the save file
int cc_storage_save(const char *path);  // 1 on success
int cc_storage_load(const char *path);  // 1 if a save was read

// Music the platform plays (name: the file's base name without extension, e.g. "MUDHOLES").
extern void (*cc_music_play)(const char *name, int loop);
extern void (*cc_music_stop)(void);

#endif
