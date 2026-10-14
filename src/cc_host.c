// The functions Castle Crashers' scripts expect from the game's executable.
// They are found by running the scripts and logging what's missing; each one is
// worked out from how the scripts use it.
#include "cc_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARG(i) ((i) < argc ? argv[i] : as_undef())

// ---- Key: the controllers -----------------------------------------------------------
// f_GetButtons gives each port its own key codes (port 1: start 49, accept 2,
// back 3, select 53, d-pad 37-40, stick 117-120, punch1 65, punch2 87, jump 83,
// projectile 68, shield 81, magic 69); the platform layer sets cc_keys[] from the pads.

uint8_t cc_keys[256];

static AsVal key_isdown(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    int code = as_toint32(vm, ARG(0));
    return as_bool(code > 0 && code < 256 && cc_keys[code]);
}

// ---- flash globals: settings shared between the executable and the scripts ----------
// (kept across level loads, unlike _root variables)

static AsObj *flash_globals;

static AsVal get_flash_global(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    return as_get(vm, as_objv(flash_globals), as_tostr(vm, ARG(0)));
}

static AsVal set_flash_global(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    as_set(vm, as_objv(flash_globals), as_tostr(vm, ARG(0)), ARG(1));
    return as_undef();
}

// ---- console settings -------------------------------------------------------------

static AsVal ret_true(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    return as_bool(1);
}
static AsVal ret_false(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    return as_bool(0);
}
static AsVal ret_zero(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    return as_num(0);
}
static AsVal ret_undef(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    return as_undef();
}

// LOGPush(value, level): the game's debug log
static AsVal log_push(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    if (getenv("CC_LOG")) printf("  [log] %s\n", as_tostr(vm, ARG(0))->s);
    return as_undef();
}

// ---- save data: ReadStorage(port, address) / WriteStorage(port, address, value) ----
// Each controller port's profile is a block of numbered values (stats, unlocks, items).

#define STORAGE_SIZE 65536
static double storage[4][STORAGE_SIZE];
int cc_storage_dirty;

static int storage_slot(AsVm *vm, AsVal port, AsVal addr, int *p) {
    *p = as_toint32(vm, port);
    int a = as_toint32(vm, addr);
    if (*p < 0 || *p > 3 || a < 0 || a >= STORAGE_SIZE) return -1;
    return a;
}

static AsVal read_storage(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    int p, a = storage_slot(vm, ARG(0), ARG(1), &p);
    return a < 0 ? as_num(0) : as_num(storage[p][a]);
}

static AsVal write_storage(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    int p, a = storage_slot(vm, ARG(0), ARG(1), &p);
    if (a >= 0) { storage[p][a] = as_tonum(vm, ARG(2)); cc_storage_dirty = 1; }
    return as_undef();
}

double *cc_storage(int port) { return port >= 0 && port < 4 ? storage[port] : NULL; }

// WriteSaveGame(): the game asks for its storage to be committed; the platform writes
// it out when cc_save_requested is set (see cc_storage_save).
int cc_save_requested;

static AsVal write_save_game(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    cc_save_requested = 1;
    return as_bool(1);
}

// Save file: "CCSV", count, then (port, address, value) for each non-zero slot.
int cc_storage_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint32_t n = 0;
    for (int p = 0; p < 4; p++)
        for (int a = 0; a < STORAGE_SIZE; a++) n += storage[p][a] != 0;
    fwrite("CCSV", 1, 4, f);
    fwrite(&n, 4, 1, f);
    for (int p = 0; p < 4; p++)
        for (int a = 0; a < STORAGE_SIZE; a++)
            if (storage[p][a] != 0) {
                uint32_t key = (uint32_t)p << 16 | a;
                fwrite(&key, 4, 1, f);
                fwrite(&storage[p][a], 8, 1, f);
            }
    int ok = !ferror(f);
    fclose(f);
    cc_storage_dirty = 0;
    return ok;
}

int cc_storage_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char magic[4];
    uint32_t n = 0;
    int ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "CCSV", 4) && fread(&n, 4, 1, f) == 1;
    for (uint32_t i = 0; ok && i < n; i++) {
        uint32_t key;
        double v;
        if (fread(&key, 4, 1, f) != 1 || fread(&v, 8, 1, f) != 1) { ok = 0; break; }
        if ((key >> 16) < 4) storage[key >> 16][key & 0xffff] = v;
    }
    fclose(f);
    return ok;
}

// SetTextNumeric(textField, value): shows a number in a text field
static AsVal set_text_numeric(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    AsVal tf = ARG(0);
    if (tf.t == V_OBJ) as_set(vm, tf, as_atom(vm, "text"), as_strv(as_tostr(vm, ARG(1))));
    return as_undef();
}

// ---- music: RegisterMusic("music/NAME.wav", id), PlayMusic(id, loop), StopMusic() ------

void (*cc_music_play)(const char *name, int loop);
void (*cc_music_stop)(void);

#define MAX_MUSIC 64
static struct { int id; char name[64]; } music[MAX_MUSIC];
static int nmusic;

static AsVal register_music(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    const char *path = as_tostr(vm, ARG(0))->s, *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    int id = as_toint32(vm, ARG(1)), i;
    for (i = 0; i < nmusic && music[i].id != id; i++) {}
    if (i == nmusic) {
        if (nmusic == MAX_MUSIC) return as_undef();
        nmusic++;
    }
    music[i].id = id;
    snprintf(music[i].name, sizeof music[i].name, "%s", base);
    char *dot = strrchr(music[i].name, '.');
    if (dot) *dot = 0;   // the platform picks the file type
    return as_undef();
}

static AsVal play_music(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    int id = as_toint32(vm, ARG(0));
    for (int i = 0; i < nmusic; i++)
        if (music[i].id == id) {
            if (cc_music_play) cc_music_play(music[i].name, argc > 1 ? as_tobool(vm, argv[1]) : 1);
            break;
        }
    return as_undef();
}

static AsVal stop_music(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    if (cc_music_stop) cc_music_stop();
    return as_undef();
}

static AsVal clear_all_music(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    nmusic = 0;
    return stop_music(vm, this_, argc, argv);
}

// f_LoadNewLevel waits for loader.b_loaded after a loadMovie. Menus set it from
// their own frame script, but level files never do: the executable sets it
// once the file is in.
static void on_loaded(AsVm *vm, AsMc *target) {
    as_set(vm, as_objv(&target->obj), as_atom(vm, "b_loaded"), as_bool(1));
}

void cc_host_init(AsVm *vm) {
    vm->loaded = on_loaded;
    AsObj *g = vm->global;
    AsObj *key = as_obj_new(vm, OBJ_PLAIN, vm->object_proto);
    as_def(vm, g, "Key", as_objv(key));
    as_def_fn(vm, key, "isDown", key_isdown);
    flash_globals = as_obj_new(vm, OBJ_PLAIN, vm->object_proto);
    as_def(vm, g, "__flash_globals", as_objv(flash_globals));   // keeps it alive for the GC
    as_def_fn(vm, g, "GetFlashGlobal", get_flash_global);
    as_def_fn(vm, g, "SetFlashGlobal", set_flash_global);
    as_def_fn(vm, g, "GetGameMode", ret_zero);
    as_def_fn(vm, g, "GetWidescreen", ret_true);   // the Vita screen is 16:9
    as_def_fn(vm, g, "IsPalMode", ret_false);
    as_def_fn(vm, g, "GetGore", ret_true);
    as_def_fn(vm, g, "LOGPush", log_push);
    as_def_fn(vm, g, "ShowLoadIcon", ret_undef);
    as_def_fn(vm, g, "HideLoadIcon", ret_undef);
    as_def_fn(vm, g, "RegisterMusic", register_music);
    as_def_fn(vm, g, "PlayMusic", play_music);
    as_def_fn(vm, g, "StopMusic", stop_music);
    as_def_fn(vm, g, "ClearAllMusic", clear_all_music);
    // online play isn't there (yet): never a network game or lobby
    as_def_fn(vm, g, "IsNetGame", ret_false);
    as_def_fn(vm, g, "IsNetLobby", ret_false);
    as_def_fn(vm, g, "SetRichPresence", ret_undef);
    as_def_fn(vm, g, "GetLanguage", ret_zero);   // 0: English
    as_def_fn(vm, g, "SetKeyboardMapping", ret_undef);
    as_def_fn(vm, g, "ShowMouse", ret_undef);
    as_def_fn(vm, g, "HideMouse", ret_undef);
    // HiFps_*: the PC version's frame interpolation; the Vita runs the game's 30 fps logic as is
    as_def_fn(vm, g, "HiFps_Reset", ret_undef);
    as_def_fn(vm, g, "HiFps_ResetRecursive", ret_undef);
    as_def_fn(vm, g, "HiFps_Adjust", ret_undef);
    // sessions: everyone on this Vita is a local player in the one session
    as_def_fn(vm, g, "JoinSession", ret_true);
    as_def_fn(vm, g, "StartSession", ret_true);
    as_def_fn(vm, g, "WriteSaveGame", write_save_game);
    as_def_fn(vm, g, "IsLocalPlayerInSession", ret_true);
    as_def_fn(vm, g, "WriteAllStats", ret_undef);   // leaderboards: not on the Vita
    as_def_fn(vm, g, "SetTextNumeric", set_text_numeric);
    as_def_fn(vm, g, "ReadStorage", read_storage);
    as_def_fn(vm, g, "WriteStorage", write_storage);
    as_def_fn(vm, g, "IsPortTalking", ret_false);   // voice chat
    as_def_fn(vm, g, "FadeOut", ret_undef);
}
