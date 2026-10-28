// The functions Castle Crashers' scripts expect from the game's executable.
// They are found by running the scripts and logging what's missing; each one is
// worked out from how the scripts use it.
#include "cc_host.h"
#include "custom_char.h"
#include "paint_jr.h"
#include "custom_animal.h"
#include "strtab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define ARG(i) ((i) < argc ? argv[i] : as_undef())

// ---- Key: the controllers -----------------------------------------------------------
// f_GetButtons gives each port its own key codes (port 1: start 49, accept 2,
// back 3, select 53, d-pad 37-40, stick 117-120, punch1 65, punch2 87, jump 83,
// projectile 68, shield 81, magic 69); the platform layer sets cc_keys[] from the pads.

uint8_t cc_keys[256];

static const uint8_t PORT_KEYS[4][CCB_COUNT] = {
    {49, 2, 3, 53, 37, 38, 39, 40, 117, 118, 119, 120, 65, 87, 83, 68, 81, 69, 67, 66},
    {50, 4, 5, 54, 8, 9, 12, 13, 121, 122, 123, 124, 16, 20, 17, 18, 27, 33, 32, 34},
    {51, 6, 7, 55, 88, 89, 90, 96, 125, 126, 127, 128, 97, 100, 98, 99, 101, 103, 102, 104},
    {52, 10, 11, 56, 105, 106, 107, 108, 129, 130, 131, 132, 109, 112, 110, 111, 113, 115, 114, 116},
};

void cc_port_keys(int port, uint32_t buttons) {
    if (port < 1 || port > 4) return;
    for (int b = 0; b < CCB_COUNT; b++)
        if (buttons >> b & 1) cc_keys[PORT_KEYS[port - 1][b]] = 1;
}

int cc_bind[CC_ACTIONS] = {0, 1, 2, 3, 4, 5};

void cc_bind_reset(void) {
    for (int a = 0; a < CC_ACTIONS; a++) cc_bind[a] = a;
}

int cc_bind_is_default(void) {
    for (int a = 0; a < CC_ACTIONS; a++)
        if (cc_bind[a] != a) return 0;
    return 1;
}

int cc_net_port;
char cc_net_names[4][32];

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

AsVal cc_flash_global(AsVm *vm, const char *name) {
    return as_get(vm, as_objv(flash_globals), as_atom(vm, name));
}

void cc_set_flash_global(AsVm *vm, const char *name, AsVal v) {
    as_set(vm, as_objv(flash_globals), as_atom(vm, name), v);
}

// SetPortState(port, state): a player's state as the pause menu sets it (g_nPort<n>State:
// 1 playing, 2 paused, 0 dropped out, 3 quitting to the map)
static AsVal set_port_state(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    char name[32];
    snprintf(name, sizeof name, "g_nPort%dState", as_toint32(vm, ARG(0)));
    cc_set_flash_global(vm, name, as_num(as_tonum(vm, ARG(1))));
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

// GetGameMode(): what Local Game's mode menu picked (CC_MODE_*). The executable answers
// 0 for the story, 1 for All You Can Quaff, 3 for Arena.
int cc_game_mode;
// QuitTo(where) / ChangeMovie(file): a mini game is over and wants the menus back
int cc_quit_requested;

// the script function a native was called from ("" outside any)
static int called_from(AsVm *vm, const char *name) {
    return vm->cur_fn && vm->cur_fn->fname && !strcmp(vm->cur_fn->fname->s, name);
}

static AsVal game_mode(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_; (void)argc; (void)argv;
    if (cc_game_mode == CC_MODE_VOLLEYBALL)   // the PS3's mode 1; two functions there treat it as Arena
        return as_num(called_from(vm, "f_FirstTimeInitOnce") || called_from(vm, "f_ItemToggle") ? CC_MODE_ARENA : 1);
    return as_num(cc_game_mode);
}

// Volleyball, as the PS3 version has it (its MAIN.swf, SELECT.swf and VMENU.swf): after
// character select the team and court picker (VMENU.swf, from the PS3 files), then one of the
// five courts (LEVEL61-65, the same files on the PC), then back to the picker. The PC's MAIN.swf
// has none of the PS3's mode 1 differences, so they are made here: some of _root's functions
// are wrapped (the scripts' own kept on _root as cc_<name>) and behave as the PS3's do in
// mode 1. Nothing of it is saved (main.c).
static AsVal call_own(AsVm *vm, const char *name, AsVal this_, int argc, AsVal *argv) {
    char key[64];
    snprintf(key, sizeof key, "cc_%s", name);
    return as_call(vm, as_get(vm, as_objv(&vm->root->obj), as_str(vm, key)), this_, argc, argv);
}

static void set_member(AsVm *vm, AsVal o, const char *k, AsVal v) {
    if (o.t == V_OBJ) as_set(vm, o, as_atom(vm, k), v);
}

static AsVal get_member(AsVm *vm, AsVal o, const char *k) {
    return o.t == V_OBJ ? as_get(vm, o, as_atom(vm, k)) : as_undef();
}

// SELECT.swf (PC) goes on to the map or Home Castle; the PS3's goes to the court picker
static AsVal v_change_level(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsVal a[8];
    int n = argc < 8 ? argc : 8;
    for (int i = 0; i < n; i++) a[i] = argv[i];
    if (n >= 1 && a[0].t == V_STR && (strstr(a[0].s->s, "map.swf") || strstr(a[0].s->s, "level2.swf")))
        a[0] = as_strv(as_str(vm, "../vmenu/vmenu.swf"));
    return call_own(vm, "f_ChangeLevel", this_, n, a);
}

// no experience, no magic explosions
static AsVal v_nothing(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    return as_undef();
}

// f_SpawnPlayer: no animal orb
static AsVal v_assign_animal(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (called_from(vm, "f_SpawnPlayer")) return as_undef();
    return call_own(vm, "f_AssignAnimal", this_, argc, argv);
}

// f_MoveCharH: a player is always on screen and in bounds (may walk off the camera's view),
// and picks nothing up
static AsVal v_check_pickups(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (called_from(vm, "f_MoveCharH")) {
        set_member(vm, argc ? argv[0] : as_undef(), "onscreen", as_bool(1));
        return as_undef();
    }
    return call_own(vm, "f_PlayerCheckPickups", this_, argc, argv);
}

static AsVal v_on_screen(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (called_from(vm, "f_MoveCharH")) return as_bool(1);
    return call_own(vm, "f_SZ_PlayerXOnScreen", this_, argc, argv);
}

static AsVal v_safe_screen(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (called_from(vm, "f_MoveCharH")) return as_bool(1);
    return call_own(vm, "f_SZ_PlayerWithinSafeScreen", this_, argc, argv);
}

// f_FireClock: no burning damage
static AsVal v_damage(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (called_from(vm, "f_FireClock")) return as_undef();
    return call_own(vm, "f_Damage", this_, argc, argv);
}

static AsVal v_check_health(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (called_from(vm, "f_FireClock")) return as_undef();
    return call_own(vm, "f_CheckHealth", this_, argc, argv);
}

// f_Damage: no damage numbers (its f_FX(x, y, depth, "damage_val", ...) and f_ShowVal)
static AsVal v_fx(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (called_from(vm, "f_Damage") && argc >= 4 && argv[3].t == V_STR && !strcmp(argv[3].s->s, "damage_val"))
        return as_undef();
    return call_own(vm, "f_FX", this_, argc, argv);
}

static AsVal v_show_val(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (called_from(vm, "f_Damage")) return as_undef();
    return call_own(vm, "f_ShowVal", this_, argc, argv);
}

// f_PunchSet2: the heavy attack in the air while holding magic is a plain heavy attack
// (punch2_1), not the downward magic
static AsVal v_punch_set2(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsVal z = argc ? argv[0] : as_undef();
    AsVal unlocks = get_member(vm, z, "magic_unlocks");
    AsVal down = unlocks.t == V_OBJ ? as_get(vm, unlocks, as_str(vm, "4")) : as_undef();
    if (!as_tobool(vm, get_member(vm, z, "magicmode")) || !(as_tonum(vm, get_member(vm, z, "body_y")) < -5) ||
        !as_tobool(vm, down))
        return call_own(vm, "f_PunchSet2", this_, argc, argv);
    set_member(vm, z, "punching", as_bool(1));
    if (!as_tobool(vm, get_member(vm, z, "jumping"))) {
        set_member(vm, z, "freeze", as_bool(1));
        AsVal fa[2] = {z, as_num(10)};
        as_call(vm, as_get(vm, as_objv(&vm->root->obj), as_atom(vm, "f_SetFloat")), as_undef(), 2, fa);
    }
    set_member(vm, z, "punch_group", as_num(2));
    set_member(vm, z, "punch_num", as_num(1));
    AsVal label = as_strv(as_str(vm, "punch2_1"));
    as_call_method(vm, z, "gotoAndStop", 1, &label);
    return as_undef();
}

static const struct { const char *name; AsNative fn; } volley_wraps[] = {
    {"f_ChangeLevel", v_change_level},
    {"f_Exp", v_nothing},
    {"f_MagicExplosion", v_nothing},
    {"f_AssignAnimal", v_assign_animal},
    {"f_PlayerCheckPickups", v_check_pickups},
    {"f_SZ_PlayerXOnScreen", v_on_screen},
    {"f_SZ_PlayerWithinSafeScreen", v_safe_screen},
    {"f_Damage", v_damage},
    {"f_CheckHealth", v_check_health},
    {"f_FX", v_fx},
    {"f_ShowVal", v_show_val},
    {"f_PunchSet2", v_punch_set2},
};

void cc_volleyball_tick(AsVm *vm) {
    if (cc_game_mode != CC_MODE_VOLLEYBALL || !vm->root) return;
    AsVal root = as_objv(&vm->root->obj);
    // the PS3's MAIN.swf has f_IsCSDown(key) for VMENU.swf: Key.isDown on a console
    AsVal cs = as_get(vm, root, as_atom(vm, "f_IsCSDown"));
    if (cs.t == V_UNDEF && as_get(vm, root, as_atom(vm, "f_ChangeLevel")).t == V_OBJ)
        as_def_fn(vm, &vm->root->obj, "f_IsCSDown", key_isdown);
    for (size_t i = 0; i < sizeof volley_wraps / sizeof volley_wraps[0]; i++) {
        AsVal f = as_get(vm, root, as_atom(vm, volley_wraps[i].name));
        if (f.t != V_OBJ || f.o->type != OBJ_FUNC || f.o->native == volley_wraps[i].fn) continue;
        char key[64];
        snprintf(key, sizeof key, "cc_%s", volley_wraps[i].name);
        as_set(vm, root, as_atom(vm, key), f);
        as_set(vm, root, as_atom(vm, volley_wraps[i].name), as_objv(as_native(vm, volley_wraps[i].fn)));
    }
}

// Volleyball's courts 2-5 (ReadStorage(26) on the PS3: bits 1, 2, 4, 8) open with the levels
// the PS3 opens them with (its f_LoadLevelUnlocks): levels 40, 47, 21 and 31
static int volley_courts(AsVm *vm, int port) {
    char hud[16];
    snprintf(hud, sizeof hud, "hud%d", port + 1);
    AsVal lu = get_member(vm, get_member(vm, as_objv(&vm->root->obj), hud), "level_unlocks");
    static const int with[4] = {40, 47, 21, 31};
    int bits = 0;
    for (int i = 0; i < 4; i++) {
        char k[8];
        snprintf(k, sizeof k, "%d", with[i]);
        if (lu.t == V_OBJ && as_tonum(vm, as_get(vm, lu, as_str(vm, k))) >= 3) bits |= 1 << i;
    }
    return bits;
}

static AsVal quit_to(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    cc_quit_requested = 1;
    return as_undef();
}

// GetLocalSecond(): the clock's seconds (0-59); the loading screen picks its hint with it
static AsVal local_second(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_; (void)argc; (void)argv;
    if (cc_net_port) return as_num(vm->tick / 30 % 60);   // the same on every Vita
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    return as_num(tm ? tm->tm_sec : 0);
}
static AsVal ret_undef(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    return as_undef();
}

// LOGPush(value, level): the game's debug log
static AsVal log_push(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    if (AS_ENV("CC_LOG")) printf("  [log] %s\n", as_tostr(vm, ARG(0))->s);
    return as_undef();
}

// ---- save data ----------------------------------------------------------------------
// Each controller port's profile is a block of numbered values (stats, unlocks, items)
// with a cursor, like a file:
//   ReadStorage(port, address)         the value at address; the cursor moves past it
//   ReadStorage(port)                  the value at the cursor; the cursor moves on
//   WriteStorage(port, address, value) / WriteStorage(port, value): the same for writing

#define STORAGE_SIZE 65536
static double storage[4][STORAGE_SIZE];
static int cursor[4];
int cc_storage_dirty;

// the slot this call reads or writes (and the cursor moves past it); -1 if out of range
static int storage_slot(AsVm *vm, AsVal port, int has_addr, AsVal addr, int *p) {
    *p = as_toint32(vm, port);
    if (*p < 0 || *p > 3) return -1;
    int a = has_addr ? as_toint32(vm, addr) : cursor[*p];
    if (a < 0 || a >= STORAGE_SIZE) return -1;
    cursor[*p] = a + 1;
    return a;
}

static AsVal read_storage(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    if (cc_game_mode == CC_MODE_VOLLEYBALL && argc >= 2 && as_tonum(vm, argv[0]) == 26)   // the PS3's courts
        return as_num(volley_courts(vm, as_toint32(vm, argv[1])));
    int p, a = storage_slot(vm, ARG(0), argc >= 2, ARG(1), &p);
    return a < 0 ? as_num(0) : as_num(storage[p][a]);
}

static AsVal write_storage(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    int p, a = storage_slot(vm, ARG(0), argc >= 3, ARG(1), &p);
    // the executable's save is bytes: a value keeps its low 8 bits
    if (a >= 0) { storage[p][a] = (uint8_t)as_toint32(vm, argc >= 3 ? argv[2] : ARG(1)); cc_storage_dirty = 1; }
    if (a >= 0 && AS_ENV("CC_STORAGE_TRACE"))
        as_warn(vm, "storage write [%d][%d] = %g (in %s)", p, a, storage[p][a],
                vm->cur_fn && vm->cur_fn->fname ? vm->cur_fn->fname->s : "?");
    return as_undef();
}

double *cc_storage(int port) { return port >= 0 && port < 4 ? storage[port] : NULL; }

// The downloadable characters (29-31 after the 28 of the original game: Pink Knight, Hatty
// Hattington, Blacksmith) are owned on the Vita: their "unlocked" bit is set in every
// profile, as owning the DLC did. Everyone else is still earned through the game.
void cc_own_dlc_characters(void) {
    for (int p = 0; p < 4; p++)
        for (int t = 29; t <= CC_CUSTOM_P_TYPE; t++) {   // 32 Paint Jr. (paint_jr.h), 33 a custom
            if (t == CC_PAINTJR_P_TYPE && !cc_paintjr_p_type()) continue;   // character (custom_char.h)
            if (t == CC_CUSTOM_P_TYPE && !cc_custom_p_type()) continue;
            double *b = &storage[p][64 + 48 * (t - 1)];
            if (!((int)*b & 128)) *b = (int)*b | 128;
        }
}

// The weapons and the animal orb that came with the downloadable packs are owned too, as they
// were in the profile of a Steam copy with the packs: Pink Knight Pack (Lollipop, Broccoli
// Stick, Buffalo Mace, Electric Eel, Scissors), Legend of the Blacksmith (Man Catcher, Dinner
// Fork, Cattle Prod, Blacksmith's Hammer), Hatty (Gem Sword, Golden Whale), and the PS3's
// Can't Stop Crying (Ninja Claw, Lightning Bolt, Cardboard Tube). A weapon's unlock number is
// 50 + its weapon number: under 128 a bit of slots 12-27, from 128 one of 28-35 (expansion);
// animals are bits of slots 8-11. The weapons' level requirements still apply.
static const int dlc_weapons[] = { 63, 71, 75, 76, 77, 72, 78, 79, 85, 84, 74, 80, 83 };
static const int dlc_animals[] = { 29 };

void cc_own_dlc_items(void) {
    for (int p = 0; p < 4; p++) {
        for (size_t i = 0; i < sizeof dlc_weapons / sizeof *dlc_weapons; i++) {
            int id = 50 + dlc_weapons[i];
            double *b = &storage[p][id < 128 ? 12 + id / 8 : 28 + (id - 128) / 8];
            *b = (int)*b | 1 << (id % 8);
        }
        for (size_t i = 0; i < sizeof dlc_animals / sizeof *dlc_animals; i++) {
            double *b = &storage[p][8 + dlc_animals[i] / 8];
            *b = (int)*b | 1 << (dlc_animals[i] % 8);
        }
        if (cc_paintjr_p_type()) {   // Paint Jr.'s pencil (weapon 86, paint_jr.h)
            int id = 50 + 86;
            storage[p][28 + (id - 128) / 8] = (int)storage[p][28 + (id - 128) / 8] | 1 << (id % 8);
        }
        if (cc_animal_type()) {   // and a custom animal orb (custom_animal.h), if there is one
            double *b = &storage[p][8 + CC_CUSTOM_ANIMAL / 8];
            *b = (int)*b | 1 << (CC_CUSTOM_ANIMAL % 8);
        }
    }
}

// WriteSaveGame(): the game asks for its storage to be committed; the platform writes
// it out when cc_save_requested is set (see cc_storage_save).
int cc_save_requested;

static AsVal write_save_game(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    cc_save_requested = 1;
    return as_bool(1);
}

// Save file: "CCS2", count, then (port, address, value) for each non-zero slot.
// ("CCSV" files came from a build that put values in the wrong slots: they're ignored.)
int cc_storage_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint32_t n = 0;
    for (int p = 0; p < 4; p++)
        for (int a = 0; a < STORAGE_SIZE; a++) n += storage[p][a] != 0;
    fwrite("CCS2", 1, 4, f);
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
    int ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "CCS2", 4) && fread(&n, 4, 1, f) == 1;
    for (uint32_t i = 0; ok && i < n; i++) {
        uint32_t key;
        double v;
        if (fread(&key, 4, 1, f) != 1 || fread(&v, 8, 1, f) != 1) { ok = 0; break; }
        if ((key >> 16) < 4) storage[key >> 16][key & 0xffff] = v;
    }
    fclose(f);
    // The executable's checks on a loaded save: a stat over 25 goes back to 1, a level byte
    // over 0x62 (level 99) is clamped. (Characters played while the levels' developer
    // settings were on saved magic/strength 9999; this heals them.)
    for (int p = 0; p < 4; p++)
        for (int t = 1; t <= CC_CUSTOM_P_TYPE; t++) {
            double *c = &storage[p][64 + 48 * (t - 1)];
            if (c[1] > 0x62) c[1] = 0x62;
            for (int s = 8; s <= 11; s++)
                if (c[s] > 25) c[s] = 1;
        }
    return ok;
}

// ---- profiles over Wi-Fi ----
// A port's profile as (address u16 LE, value u8) for each non-zero slot: the values are bytes,
// as WriteStorage keeps them.
int cc_storage_pack(int port, uint8_t *out, int max) {
    int n = 0;
    for (int a = 0; a < STORAGE_SIZE; a++) {
        if (storage[port][a] == 0) continue;
        if (n + 3 > max) return -1;
        out[n++] = a & 255;
        out[n++] = a >> 8;
        out[n++] = (uint8_t)(int)storage[port][a];
    }
    return n;
}

void cc_storage_unpack(int port, const uint8_t *in, int len) {
    memset(storage[port], 0, sizeof storage[port]);
    for (int i = 0; i + 3 <= len; i += 3) storage[port][in[i] | in[i + 1] << 8] = in[i + 2];
}

void cc_storage_clear(void) {
    memset(storage, 0, sizeof storage);
    memset(cursor, 0, sizeof cursor);
}

// The save file with port 0 (this Vita's own player) replaced by from_port's profile; the
// file's other ports are kept. For a Wi-Fi game, where this Vita's player is on another port.
int cc_storage_save_as(const char *path, int from_port) {
    uint32_t *keys = NULL, nkeys = 0;
    double *vals = NULL;
    FILE *f = fopen(path, "rb");
    if (f) {
        char magic[4];
        uint32_t n = 0;
        if (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "CCS2", 4) && fread(&n, 4, 1, f) == 1 && n <= 4 * STORAGE_SIZE) {
            keys = malloc(n * sizeof *keys + 1);
            vals = malloc(n * sizeof *vals + 1);
            for (uint32_t i = 0; keys && vals && i < n; i++) {
                uint32_t k;
                double v;
                if (fread(&k, 4, 1, f) != 1 || fread(&v, 8, 1, f) != 1) break;
                if ((k >> 16) == 0 || (k >> 16) >= 4) continue;   // port 0 is replaced
                keys[nkeys] = k;
                vals[nkeys++] = v;
            }
        }
        fclose(f);
    }
    f = fopen(path, "wb");
    int ok = 0;
    if (f) {
        uint32_t n = nkeys;
        for (int a = 0; a < STORAGE_SIZE; a++) n += storage[from_port][a] != 0;
        fwrite("CCS2", 1, 4, f);
        fwrite(&n, 4, 1, f);
        for (int a = 0; a < STORAGE_SIZE; a++)
            if (storage[from_port][a] != 0) {
                uint32_t key = (uint32_t)a;
                fwrite(&key, 4, 1, f);
                fwrite(&storage[from_port][a], 8, 1, f);
            }
        for (uint32_t i = 0; i < nkeys; i++) {
            fwrite(&keys[i], 4, 1, f);
            fwrite(&vals[i], 8, 1, f);
        }
        ok = !ferror(f);
        fclose(f);
    }
    free(keys);
    free(vals);
    cc_storage_dirty = 0;
    return ok;
}

// SetTextNumeric(textField, value): shows a number in a text field, as a whole number (the
// executable's is an integer: Giraffey's +10% leaves exp with a fraction that never shows)
static AsVal set_text_numeric(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    AsVal tf = ARG(0);
    if (tf.t != V_OBJ) return as_undef();
    double v = as_tonum(vm, ARG(1));
    char s[32];
    if (isfinite(v) && fabs(v) < 1e15) snprintf(s, sizeof s, "%.0f", trunc(v) + 0.0);
    else snprintf(s, sizeof s, "%s", as_tostr(vm, ARG(1))->s);
    as_set(vm, tf, as_atom(vm, "text"), as_strv(as_str(vm, s)));
    return as_undef();
}

// GetLocalGamerTag(port): the player's name, shown on unlock banners ("... found a new weapon")
// and the stat screen: the names cc_net_names has (a Wi-Fi game's players, or this Vita's user
// as player 1), else "Player N".
static AsVal gamer_tag(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    char s[40];
    int p = (int)as_tonum(vm, ARG(0));
    if (p >= 0 && p < 4 && cc_net_names[p][0]) return as_strv(as_str(vm, cc_net_names[p]));
    snprintf(s, sizeof s, "Player %d", p + 1);
    return as_strv(as_str(vm, s));
}

// AddShader(name), from a clip's frame script: a screen effect on that clip. "Heat" (lava
// world's haze) makes the picture behind the clip shimmer, which the Vita renderer draws;
// "RasterHack" (the skies) only hides seams in the cloud loops, so there's nothing to do.
// ---- achievements ----------------------------------------------------------------------
// The original's executable gives them to Steam/Xbox Live/PSN. Here they're kept in
// achievements.dat (one 32-bit little-endian word: bit n = id n) and shown by the platform.
static const char *ACH[CC_ACHIEVEMENTS + 1][2] = {
    {"", ""},
    {"Kay Eye Ess Ess", "Collect all 4 princesses' kisses in a multiplayer game"},
    {"Traditional", "Complete the game using any character"},
    {"The Traitor", "Defeat any boss by playing one of his own minions"},
    {"Deer Trainer", "Navigate the Abandoned Mill without hitting any obstacles"},
    {"Conscientious Objector", "Complete Home Castle through Barbarian Boss without attacking any foes"},
    {"Maximum Firepower", "Using a catapult, fling a knight with maximum power"},
    {"Animal Handler", "Collect all the animal orbs"},
    {"Arena Master", "Win 40 online Arena matches (online play isn't on the Vita)"},
    {"Glork", "Win 20 ranked All You Can Quaff matches (online play isn't on the Vita)"},
    {"Melee Is Best", "Defeat any end boss without using any magic"},
    {"Treasure Hunter", "Find and dig up ten buried items"},
    {"Medic!", "In a 4-player game, revive each fallen comrade at least once"},
};
static unsigned ach_bits;
static int ach_new;
static char ach_path[256];

const char *cc_achievement_name(int id) { return id >= 1 && id <= CC_ACHIEVEMENTS ? ACH[id][0] : ""; }
const char *cc_achievement_how(int id) { return id >= 1 && id <= CC_ACHIEVEMENTS ? ACH[id][1] : ""; }
unsigned cc_achievements(void) { return ach_bits; }
int cc_achievement_take_new(void) { int id = ach_new; ach_new = 0; return id; }

void cc_achievements_load(const char *path) {
    snprintf(ach_path, sizeof ach_path, "%s", path);
    FILE *f = fopen(path, "rb");
    uint8_t b[4] = {0};
    if (f) {
        if (fread(b, 1, 4, f) == 4) ach_bits = b[0] | b[1] << 8 | b[2] << 16 | (unsigned)b[3] << 24;
        fclose(f);
    }
}

static AsVal unlock_achievement(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    int id = as_toint32(vm, ARG(1));
    // in a Wi-Fi game every Vita runs everyone's unlocks: only this Vita's player's count
    // (the port is 0-based here, as in ReadStorage)
    if (cc_net_port && as_toint32(vm, ARG(0)) != cc_net_port - 1) return as_undef();
    if (id < 1 || id > CC_ACHIEVEMENTS || (ach_bits >> id & 1)) return as_undef();
    ach_bits |= 1u << id;
    ach_new = id;
    if (ach_path[0]) {
        FILE *f = fopen(ach_path, "wb");
        if (f) {
            uint8_t b[4] = {ach_bits & 255, ach_bits >> 8 & 255, ach_bits >> 16 & 255, ach_bits >> 24};
            fwrite(b, 1, 4, f);
            fclose(f);
        }
    }
    return as_undef();
}

static AsVal add_shader(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (this_.t == V_OBJ && this_.o->type == OBJ_MC && !strcmp(as_tostr(vm, ARG(0))->s, "Heat"))
        ((AsMc *)this_.o)->shader = 1;
    return as_undef();
}

// RemoveShader(name): the clip's screen effect ends
static AsVal remove_shader(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    if (this_.t == V_OBJ && this_.o->type == OBJ_MC) ((AsMc *)this_.o)->shader = 0;
    return as_undef();
}

// IsSameProfile(portA, portB): the shops unlock a bought animal for every player on the
// buyer's profile. On the Vita each port is its own profile.
static AsVal is_same_profile(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    return as_bool(as_toint32(vm, ARG(0)) == as_toint32(vm, ARG(1)));
}

// ---- music: RegisterMusic(id, "music/NAME.wav"), PlayMusic(id, loop), StopMusic() ------

void (*cc_music_play)(const char *name, int loop);
void (*cc_music_stop)(void);

#define MAX_MUSIC 64
static struct { int id; char name[64]; } music[MAX_MUSIC];
static int nmusic;

static AsVal register_music(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    const char *path = as_tostr(vm, ARG(1))->s, *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    int id = as_toint32(vm, ARG(0)), i;
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

// ---- level files cut short --------------------------------------------------------
// Some extracted level files lose their last bytes, and with them the root placements of
// the last parallax layers. The layer sprites themselves are still there: a container
// whose first frame holds the inner layer (bg0_c holds bg0, game holds game). Put back
// any container the root is missing, on top, positioned like the others.

static int sprite_holds(const Movie *m, const Sprite *s, const char *inner) {
    if (!s->nframes) return 0;
    const Frame *f = &s->frames[0];
    for (int i = 0; i < f->count; i++)
        if (f->places[i].name && !strcmp(m->names[f->places[i].name], inner)) return 1;
    return 0;
}

static int cc_repair_cut_short(Movie *m);

int cc_repair_level(Movie *m) {
    if (cc_repair_cut_short(m)) return 1;
    static const char *layers[][2] = {{"bg3_c", "bg3"}, {"bg2_c", "bg2"}, {"bg1_c", "bg1"}, {"game", "game"}, {"bg0_c", "bg0"}};
    if (!m->root.nframes) return 0;
    Frame *f = &m->root.frames[0];
    const Place *model = NULL;   // an existing layer placement to copy the position from
    int maxdepth = 0, added = 0;
    for (int i = 0; i < f->count; i++) {
        if (f->places[i].depth > maxdepth) maxdepth = f->places[i].depth;
        for (int k = 0; k < 5; k++)
            if (f->places[i].name && !strcmp(m->names[f->places[i].name], layers[k][0])) model = &f->places[i];
    }
    // Only a file that lost its tail can be missing layers. LEVEL40 (Pirate Ship) is whole and
    // names its layers b0-b3; adding bg1_c/bg3_c on top of the game drew the sea over the ship.
    if (!model || !m->truncated) return 0;   // not a level, nothing to copy from, or a whole file
    Place tmpl = *model;
    for (int k = 0; k < 5; k++) {
        int present = 0;
        for (int i = 0; i < f->count; i++)
            present |= f->places[i].name && !strcmp(m->names[f->places[i].name], layers[k][0]);
        if (present) continue;
        int cid = -1;   // the last sprite holding the inner layer
        for (int c = 0; c < 65536; c++)
            if (m->chars[c].type == CHAR_SPRITE && sprite_holds(m, m->chars[c].ptr, layers[k][1])) cid = c;
        if (cid < 0) continue;
        m->names = realloc(m->names, sizeof(char *) * (m->nnames + 1));
        m->names[m->nnames] = strdup(layers[k][0]);
        Place p = tmpl;
        p.cid = cid;
        p.name = m->nnames++;
        p.depth = maxdepth += 2;
        p.clip = 0;
        p.uid = 0x7f000000u + k;
        p.events_off = p.events_len = 0;
        p.cx = CX_IDENTITY;
        // every frame of the root shows it (places stay sorted by depth: these go last)
        for (int fr = 0; fr < m->root.nframes; fr++) {
            Frame *ff = &m->root.frames[fr];
            ff->places = realloc(ff->places, sizeof(Place) * (ff->count + 1));
            ff->places[ff->count++] = p;
        }
        f = &m->root.frames[0];
        added++;
    }
    return added;
}

// Most of the game's files are missing their last ~128 bytes. That's usually only the
// final End tags, but a character file can lose its last frame's display list: the boss's
// final labelled frame ("die" in EBARBOSS, "die4" in EWIZARD) should put up his death
// animation, and without it he vanishes (no chest, no ending). That animation is the one
// sprite nothing places (EBARBOSS 168: the chest drops on frame 73; EWIZARD 176). Put it back
// at depth 1, where the earlier bodies go, on the last frame.
static void mark_placed(const Sprite *s, uint8_t *placed) {
    for (int f = 0; f < s->nframes; f++)
        for (int i = 0; i < s->frames[f].count; i++) placed[s->frames[f].places[i].cid] = 1;
}

static int cc_repair_cut_short(Movie *m) {
    int n = m->root.nframes;
    if (!m->truncated || n < 2 || m->root.nlabels < 1) return 0;
    const Label *last = &m->root.labels[m->root.nlabels - 1];
    if (last->frame != n - 1) return 0;
    Frame *f = &m->root.frames[n - 1], *before = &m->root.frames[n - 2];
    for (int i = 0; i < f->count; i++) {   // something new at depth 1 on that frame: not cut
        if (f->places[i].depth != 1) continue;
        int same = 0;
        for (int k = 0; k < before->count; k++) same |= before->places[k].uid == f->places[i].uid;
        if (!same) return 0;
    }
    uint8_t *placed = calloc(65536, 1);
    if (!placed) return 0;
    mark_placed(&m->root, placed);
    int orphan = -1;
    for (int c = 0; c < 65536; c++)
        if (m->chars[c].type == CHAR_SPRITE) mark_placed(m->chars[c].ptr, placed);
    for (int c = 0; c < 65536; c++)   // the last-defined unplaced, unexported sprite
        if (m->chars[c].type == CHAR_SPRITE && !placed[c] && !m->chars[c].export) orphan = c;
    free(placed);
    if (orphan < 0 || ((Sprite *)m->chars[orphan].ptr)->nframes < 10) return 0;
    // the body goes where the earlier animations put theirs (depth 1, same matrix)
    const Place *model = NULL;
    for (int fr = n - 2; fr >= 0 && !model; fr--)
        for (int i = 0; i < m->root.frames[fr].count; i++)
            if (m->root.frames[fr].places[i].depth == 1) { model = &m->root.frames[fr].places[i]; break; }
    int body = 0;
    for (int i = 1; i < m->nnames; i++)
        if (!strcmp(m->names[i], "body")) body = i;
    int named_body = 0;   // only character files: their timelines put a "body" at depth 1
    for (int fr = 0; fr < n && body; fr++)
        for (int i = 0; i < m->root.frames[fr].count; i++)
            named_body |= m->root.frames[fr].places[i].depth == 1 && m->root.frames[fr].places[i].name == body;
    if (!model || !named_body) return 0;
    Place p = *model;
    p.cid = orphan;
    p.name = body;
    p.clip = 0;
    p.uid = 0x7e000000u;
    p.events_off = p.events_len = 0;
    int k = 0;   // drop whatever the cut frame kept at depth 1, then put the body first
    for (int i = 0; i < f->count; i++)
        if (f->places[i].depth != 1) f->places[k++] = f->places[i];
    f->places = realloc(f->places, sizeof(Place) * (k + 1));
    memmove(f->places + 1, f->places, sizeof(Place) * k);
    f->places[0] = p;
    f->count = k + 1;
    if (getenv("CC_REPAIR_DEBUG")) fprintf(stderr, "repaired: frame %d (%s) shows sprite %d\n", n, last->name, orphan);
    return 1;
}
// ---- collision for the shipping ("console") path ---------------------------------------
// With console_version on, a level calls f_BSPLoadLevel("bspN") and the executable loads a
// precomputed tree (bsps/BSPn.PDAG). The scripts' developer path builds the same tree from
// the level's own edge clips instead: show the game clip's frame 2 (the edges), and once
// their first-frame scripts have run, f_InitLevelBSP(), then frame 3. Do that here.
// Arenas (LEVEL24, 46-49) have four layouts of blocks, "bspNa".."bspNd": the level shows
// layout k on frame 2k+1 and its edges on the frame after, then asks for that tree. There
// the edges are the next frame and the game goes back to the layout (going on to frame 3
// showed layout b's blocks over layout a's walls: players walked through blocks and out).

static int bsp_pending, bsp_end_frame;

static AsVal bsp_load_level(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    AsVal pg = as_get(vm, as_objv(&vm->root->obj), as_atom(vm, "p_game"));
    if (pg.t == V_OBJ && pg.o->type == OBJ_MC) {
        AsMc *g = (AsMc *)pg.o;
        AsStr *name = argc > 0 ? as_tostr(vm, argv[0]) : NULL;
        char last = name && name->len ? name->s[name->len - 1] : 0;
        if (last >= 'a' && last <= 'd') {
            bsp_end_frame = g->frame;
            as_mc_goto(vm, g, g->frame + 1, 0);
        } else {
            bsp_end_frame = 2;
            as_mc_goto(vm, g, 1, 0);
        }
        bsp_pending = 1;
    }
    return as_undef();
}

static void loading_exit_check(AsVm *vm);

static void after_frame_scripts(AsVm *vm) {
    loading_exit_check(vm);
    if (!bsp_pending) return;
    bsp_pending = 0;
    as_call_method(vm, as_objv(&vm->root->obj), "f_InitLevelBSP", 0, NULL);
    AsVal pg = as_get(vm, as_objv(&vm->root->obj), as_atom(vm, "p_game"));
    if (pg.t == V_OBJ && pg.o->type == OBJ_MC) as_mc_goto(vm, (AsMc *)pg.o, bsp_end_frame, 0);
}

// ---- the loading screen -----------------------------------------------------------
// The executable runs LOADING.swf over the game: around a level change it sets g_bLoading,
// plays the intro, holds the file until the intro sets g_bReadyToLoad (the screen is
// covered), then plays the exit (which calls HideLoadingScreen). So does this.
#define LOADING_DEPTH 1048500
static int load_wait, loading_shown, exit_wait = -1;

static AsMc *loading_screen(AsVm *vm) {
    return as_mc_child(vm->root, as_atom(vm, "cc_loading"));
}

// LOADING.swf never reads g_bLoading itself: the executable plays its loadingScreen clip
// (stopped on frame 1; frame 20 "loading" sets g_bReadyToLoad and stops) and sends it to
// "exit" once the file is in. Without that the screen stayed black for the whole timeout.
static void loading_play(AsVm *vm, AsMc *ls, const char *label) {
    AsMc *scr = as_mc_child(ls, as_atom(vm, "loadingScreen"));
    if (!scr) return;
    if (!label) { as_mc_goto(vm, scr, 1, 1); return; }
    int f = as_mc_frame_of(scr, as_strv(as_str(vm, label)));
    if (f >= 0) as_mc_goto(vm, scr, f, 1);
}

void cc_loading_attach(AsVm *vm) {
    Movie *m = vm->loader ? vm->loader(vm, "LOADING.swf") : NULL;
    if (!m) return;
    AsMc *mc = as_mc_create_empty(vm, vm->root, as_atom(vm, "cc_loading"), LOADING_DEPTH);
    as_mc_load_movie(vm, mc, m);
    mc->visible = 0;   // shown from the first level change (hold_load)
    loading_shown = 0;
    exit_wait = -1;
}

static void loading_begin(AsVm *vm, AsMc *ls) {
    loading_shown = 1;
    load_wait = 0;
    exit_wait = -1;
    ls->visible = 1;
    cc_set_flash_global(vm, "g_bLoading", as_num(1));
    cc_set_flash_global(vm, "g_bReadyToLoad", as_num(0));
    // its hint text asks _root for f_GetHint, which its own first frame defines
    as_set(vm, as_objv(&vm->root->obj), as_atom(vm, "f_GetHint"),
           as_get(vm, as_objv(&ls->obj), as_atom(vm, "f_GetHint")));
    loading_play(vm, ls, NULL);
}

// The loading screen as the game left it (which knight, his gems, the hint), for the title's
// copy to carry on with: each clip's frame and colour, in the order of a walk of the tree
typedef struct { int nframes, frame; uint8_t playing, visible; CXform cx; char text[160]; } LoadingClip;
static LoadingClip held[256];
static int nheld;

static void hold_clip(AsVm *vm, AsMc *mc) {
    if (nheld >= 256) return;
    LoadingClip *c = &held[nheld++];
    c->nframes = mc->def ? mc->def->nframes : 0;
    c->frame = mc->frame;
    c->playing = mc->playing;
    c->visible = mc->visible;
    c->cx = mc->cx;
    c->text[0] = 0;
    if (mc->text) {
        AsVal t = as_get(vm, as_objv(&mc->obj), as_atom(vm, "text"));
        if (t.t != V_UNDEF) snprintf(c->text, sizeof c->text, "%s", as_tostr(vm, t)->s);
    }
    for (int i = 0; i < mc->ndisp; i++)
        if (mc->disp[i].mc) hold_clip(vm, mc->disp[i].mc);
}

static int put_clip(AsVm *vm, AsMc *mc, int *at) {
    if (*at >= nheld) return 0;
    LoadingClip *c = &held[(*at)++];
    if (c->nframes != (mc->def ? mc->def->nframes : 0)) return 0;   // not the same tree: leave it
    if (c->frame != mc->frame && c->frame >= 0) as_mc_goto(vm, mc, c->frame, c->playing);
    mc->playing = c->playing;
    mc->visible = c->visible;
    mc->cx = c->cx;
    if (mc->text && c->text[0]) as_set(vm, as_objv(&mc->obj), as_atom(vm, "text"), as_strv(as_str(vm, c->text)));
    for (int i = 0; i < mc->ndisp; i++)
        if (mc->disp[i].mc && !put_clip(vm, mc->disp[i].mc, at)) return 0;
    return 1;
}

// Exit to Main Menu: the loading screen comes in over the game, which stands still under it
// (only the screen's own clip runs). 1 once it covers the screen and the game can go.
int cc_loading_cover(AsVm *vm) {
    AsMc *ls = loading_screen(vm);
    if (!ls) return 1;
    if (!loading_shown) loading_begin(vm, ls);
    as_tick_clip(vm, ls);
    if (as_tonum(vm, cc_flash_global(vm, "g_bReadyToLoad")) != 1 && ++load_wait < 90) return 0;
    nheld = 0;
    hold_clip(vm, ls);
    return 1;
}

// ...and goes out over the title: the title's own copy starts where the game's left off
// (covering the screen), then plays its exit once nothing is loading
void cc_loading_reveal(AsVm *vm) {
    cc_loading_attach(vm);
    AsMc *ls = loading_screen(vm);
    AsMc *scr = NULL;
    for (int i = 0; ls && !scr && i < 10; i++) {   // the screen is placed on the file's second frame
        as_tick_clip(vm, ls);
        scr = as_mc_child(ls, as_atom(vm, "loadingScreen"));
    }
    if (!scr) { nheld = 0; return; }
    ls->visible = 1;
    loading_shown = 1;
    as_set(vm, as_objv(&vm->root->obj), as_atom(vm, "f_GetHint"),
           as_get(vm, as_objv(&ls->obj), as_atom(vm, "f_GetHint")));
    loading_play(vm, ls, "loading");
    // its clips pick a knight in their first frames' scripts: those run first, then the
    // game's picture goes over them (twice, for the clips the first pass made)
    for (int pass = 0; pass < 2 && nheld; pass++) {
        as_drain(vm);
        int at = 0;
        put_clip(vm, ls, &at);
    }
    as_drain(vm);
    nheld = 0;
    exit_wait = 0;
}

static int is_level_slot(AsVm *vm, AsMc *target) {
    return target->parent == vm->root && target->name && !strcmp(target->name->s, "loader");
}

static int hold_load(AsVm *vm, AsMc *target, const char *url) {
    AsMc *ls = loading_screen(vm);
    if (ls && is_level_slot(vm, target) && !as_tobool(vm, cc_flash_global(vm, "g_bNoLoadingScreen"))) {
        if (!loading_shown) loading_begin(vm, ls);
        // the intro takes 20 frames; don't wait forever if it never says so
        if (as_tonum(vm, cc_flash_global(vm, "g_bReadyToLoad")) != 1 && ++load_wait < 90) return 1;
    }
    // the level file is read on another thread while the loading screen runs on. Not the
    // files a level asks for itself (mounts, enemies, sky): its clips set up what they load
    // into on the next frames, and a late file wipes that (the Abandoned Mill's deer)
    return is_level_slot(vm, target) && vm->prefetch && vm->prefetch(vm, url);
}

// The loading screen plays its exit once the level is in and the files its first frames
// asked for (sky, enemies, mounts) are in too: two frames with no load waiting.
static void loading_exit_check(AsVm *vm) {
    if (exit_wait < 0) return;
    if (vm->loads) { exit_wait = 0; return; }
    if (++exit_wait < 2) return;
    exit_wait = -1;
    loading_shown = 0;
    cc_set_flash_global(vm, "g_bLoading", as_num(0));
    AsMc *ls = loading_screen(vm);
    if (ls) loading_play(vm, ls, "exit");
}

int cc_loading_busy(AsVm *vm) {
    AsMc *ls = loading_screen(vm);
    return ls && ls->visible;
}

// HideLoadingScreen(): the exit is over; the clip stops being drawn until the next load
static AsVal hide_loading_screen(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_; (void)argc; (void)argv;
    AsMc *ls = loading_screen(vm);
    if (ls && !loading_shown) ls->visible = 0;
    return as_undef();
}

// f_LoadNewLevel waits for loader.b_loaded after a loadMovie. Menus set it from
// their own frame script, but level files never do: the executable sets it
// once the file is in.
// _root.f_SafePlayerType(p_type): the uncut LOBBY.swf asks MAIN for a character number it
// can index the save with; the original MAIN.swf doesn't have it, so the character select's
// levels all read as 1. Every character this build has is safe as it is.
static AsVal safe_player_type(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    int t = argc > 0 ? (int)as_tonum(vm, argv[0]) : 1;
    return as_num(t >= 1 && (t <= CC_CHARACTERS || t == cc_paintjr_p_type() || t == cc_custom_p_type()) ? t : 1);
}

static void on_loaded(AsVm *vm, AsMc *target) {
    AsStr *fsp = as_atom(vm, "f_SafePlayerType");
    if (as_get(vm, as_objv(&vm->root->obj), fsp).t == V_UNDEF)
        as_def_fn(vm, &vm->root->obj, "f_SafePlayerType", safe_player_type);
    as_set(vm, as_objv(&target->obj), as_atom(vm, "b_loaded"), as_bool(1));
    if (is_level_slot(vm, target) && loading_shown) exit_wait = 0;   // its exit is next
}

// Volleyball's court picker (VMENU.swf) comes from the PS3, whose string table isn't in any file
// (it's in the encrypted executable): its numbers past the PC's are the PS3's, so their words are
// here (strings_vita_en.txt can replace them as vita.volley.N), worked out from the fields'
// placeholder words and what the scripts do with them
static const char *volley_text(int n) {
    static const struct { int n; const char *words; } table[] = {
        {444, "Ready!"}, {452, "Ready"}, {716, "Matches"}, {717, "Best of"}, {719, "CPU"},
        {730, "Each team needs a player"}, {731, "Each team needs a player"}, {733, "Fill Teams"},
    };
    char key[32];
    snprintf(key, sizeof key, "vita.volley.%d", n);
    const char *s = strtab_get(key);
    if (s) return s;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (table[i].n == n) return table[i].words;
    return NULL;
}

static const char *text_for_number(AsVm *vm, int n) {
    const char *s = cc_game_mode == CC_MODE_VOLLEYBALL ? volley_text(n) : NULL;
    if (!s) s = cc_animal_text(vm, n);   // the Vet's words for a custom animal orb
    return s ? s : strtab_number(n);
}

// Auto Potion: inside f_CheckHealthPots(zone), active_players reads 2 when it's on and zone's
// selected item is the potion (equippeditem 9), so the last player up drinks it too
static int auto_potion;
static AsVal auto_potion_players(AsVm *vm) {
    if (!auto_potion || vm->cur_argc < 1) return as_undef();
    AsVal item = as_get(vm, vm->cur_argv[0], as_atom(vm, "equippeditem"));
    return as_tonum(vm, item) == 9 ? as_num(2) : as_undef();
}

void cc_set_auto_potion(AsVm *vm, int on) {
    (void)vm;
    auto_potion = on;
}

void cc_host_init(AsVm *vm) {
    bsp_pending = 0;   // a new game: nothing left over from the last one
    memset(cursor, 0, sizeof cursor);
    cc_custom_init(vm);   // before MAIN's functions are defined: a painted character, if there is one
    cc_paintjr_init(vm);  // and Paint Jr., if the PC files for him are there
    vm->loaded = on_loaded;
    vm->hold_load = hold_load;
    vm->text_for_number = text_for_number;
    vm->after_frame_scripts = after_frame_scripts;
    cc_native_init(vm);
    cc_animal_init(vm);   // a custom animal orb, if there is one
    // console_version: the shipping executable's value is true. All the way, that hands the
    // front end (menus, profiles, QuitTo between files) to the executable, which isn't
    // reproduced yet; so the scripts' own front end runs, and the shipping behaviour is
    // switched on where the developer one is wrong for players:
    static const char *console_fns[] = {
        "f_LoadCharacterData",   // your saved character, not a level-40 test one
        "f_HudWaitEnd",          // "press START" on the empty player slots
        "f_Depth",               // no debug trace
        "f_RemovePlayer",        // after dying: wait on the HUD, back to the map when all are down
        "f_Walk",                // a full tilt runs, a light one walks (not keyboard double-taps)
        "f_SelectingChar",       // character select: each knight's saved level (else the
        "f_ChangeSelectedChar",  //   field keeps its placeholder "99")
        "f_Goal",                // world map: the location's name under the cursor
        "f_GuyInit",
        "f_InitStatScreen",      // stat screen: gold, exp and unspent points, Cross accepts
        "f_StatScreen",
        "f_SetupNameOverlay",    // the name over each knight's head (PLAYER.swf): the player's
                                 //   name and number, not the placeholder "Gamertag"
    };
    if (!getenv("CC_DEV_PATHS")) {
        for (size_t i = 0; i < sizeof console_fns / sizeof *console_fns; i++)
            as_pin(vm, "console_version", as_bool(1), console_fns[i]);
        // Level files are true all the way. Each level's f_Init writes console_version =
        // false and then, where it's still false, applies its developer test settings:
        // p1.magic, magic_pow, strength, punch_pow = 9999 (that was the "999 damage" and
        // "magic maxed" on new characters), Final Battle enemies with 3 health. In the
        // executable the write never sticks; the level then loads its BSP through
        // f_BSPLoadLevel (bsp_load_level below).
        as_pin_in_levels(vm, "console_version", as_bool(1));
    }
    as_pin_fn(vm, "active_players", "f_CheckHealthPots", auto_potion_players);   // Auto Potion
    as_def_fn(vm, vm->global, "f_BSPLoadLevel", bsp_load_level);
    AsObj *g = vm->global;
    AsObj *key = as_obj_new(vm, OBJ_PLAIN, vm->object_proto);
    as_def(vm, g, "Key", as_objv(key));
    as_def_fn(vm, key, "isDown", key_isdown);
    flash_globals = as_obj_new(vm, OBJ_PLAIN, vm->object_proto);
    as_def(vm, g, "__flash_globals", as_objv(flash_globals));   // keeps it alive for the GC
    as_def_fn(vm, g, "GetFlashGlobal", get_flash_global);
    as_def_fn(vm, g, "SetFlashGlobal", set_flash_global);
    as_def_fn(vm, g, "SetPortState", set_port_state);
    for (int p = 1; p <= 4; p++) {   // everyone starts out playing
        char name[32];
        snprintf(name, sizeof name, "g_nPort%dState", p);
        cc_set_flash_global(vm, name, as_num(1));
    }
    // character select offers every unlocked character, not just the four knights (the
    // executable sets this for the full game; the DLC characters are treated as owned)
    cc_set_flash_global(vm, "g_bAllowHiddenCharacters", as_bool(1));
    cc_own_dlc_characters();
    cc_own_dlc_items();
    as_def_fn(vm, g, "GetGameMode", game_mode);
    as_def_fn(vm, g, "UseCircleSelect", ret_false);   // (PS3 files: Circle never confirms)
    // QuitTo("menu.cok6") / ChangeMovie(quitto): back to the executable's menus (backing out of
    // character select, the end of a one-player Arena match, leaving All You Can Quaff)
    cc_quit_requested = 0;
    as_def_fn(vm, g, "QuitTo", quit_to);
    as_def_fn(vm, g, "ChangeMovie", quit_to);
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
    as_def_fn(vm, g, "GetLocalGamerTag", gamer_tag);
    as_def_fn(vm, g, "PreloadNews", ret_undef);     // the menu's online news panel: no news
    as_def_fn(vm, g, "IsNewsLoaded", ret_true);
    // GetCheat(n): MAIN.swf ships a developer version on _root that turns most cheats on
    // (every combo and magic at level 1, every character, animals without unlocking). The
    // executable's answer is no cheats, so both the global and MAIN's own are replaced.
    as_def_fn(vm, g, "GetCheat", ret_false);
    as_override(vm, "GetCheat", ret_false);
    // MAIN defines its own IsFullGame on _root, but level scripts call the executable's
    // (they can't see _root's functions): this is the full game
    as_def_fn(vm, g, "IsFullGame", ret_true);
    as_def_fn(vm, g, "RichPresence", ret_undef);       // platform presence text
    as_def_fn(vm, g, "UnlockAchievement", unlock_achievement);
    as_def_fn(vm, g, "VibrateController", ret_undef);  // the Vita has no rumble
    as_def_fn(vm, g, "HiFps_SetEnabled", ret_undef);
    as_def_fn(vm, g, "HiFps_Loop", ret_undef);
    as_def_fn(vm, g, "AddShader", add_shader);
    as_def_fn(vm, g, "RemoveShader", remove_shader);
    as_def_fn(vm, g, "IsSameProfile", is_same_profile);
    // the rest the level and menu files call that matter offline: profiles are signed in,
    // nothing is ranked, and the PC/online extras (skins, avatar items, the loading screen's
    // own hide call, interpolation, session ends) have nothing to do here
    as_def_fn(vm, g, "IsPlayerSignedIn", ret_true);
    as_def_fn(vm, g, "TryBindUserToController", ret_true);
    as_def_fn(vm, g, "IsRankedMatch", ret_false);
    as_def_fn(vm, g, "ReadSaveGame", ret_true);
    as_def_fn(vm, g, "GetRegion", ret_zero);
    as_def_fn(vm, g, "AttachSkinGraphic", ret_undef);
    as_def_fn(vm, g, "UnlockAvatarItem", ret_undef);
    as_def_fn(vm, g, "HideLoadingScreen", hide_loading_screen);
    as_def_fn(vm, g, "GetLocalSecond", local_second);
    as_def_fn(vm, g, "InterpolationOff", ret_undef);
    as_def_fn(vm, g, "SetFlashController", ret_undef);
    as_def_fn(vm, g, "CheckSlowest", ret_undef);
    as_def_fn(vm, g, "EndSession", ret_undef);
    as_def_fn(vm, g, "LeaveSession", ret_undef);
    as_def_fn(vm, g, "SetFramerate", ret_undef);   // the boss's death slow motion: kept at full speed
    as_def_fn(vm, g, "SetScores", ret_undef);   // arena scoreboards (online stats)
    as_def_fn(vm, g, "Error", ret_undef);       // the executable's error log
    as_def_fn(vm, g, "FadeIn", ret_undef);      // screen fades (the scripts draw their own)
    as_def_fn(vm, g, "FadeOut", ret_undef);
}
