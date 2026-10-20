// The functions Castle Crashers' scripts expect from the game's executable.
// They are found by running the scripts and logging what's missing; each one is
// worked out from how the scripts use it.
#include "cc_host.h"
#include "strtab.h"

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
        for (int t = 29; t <= 31; t++) {
            double *b = &storage[p][64 + 48 * (t - 1)];
            if (!((int)*b & 128)) *b = (int)*b | 128;
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
        for (int t = 1; t <= 31; t++) {
            double *c = &storage[p][64 + 48 * (t - 1)];
            if (c[1] > 0x62) c[1] = 0x62;
            for (int s = 8; s <= 11; s++)
                if (c[s] > 25) c[s] = 1;
        }
    return ok;
}

// SetTextNumeric(textField, value): shows a number in a text field
static AsVal set_text_numeric(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    AsVal tf = ARG(0);
    if (tf.t == V_OBJ) as_set(vm, tf, as_atom(vm, "text"), as_strv(as_tostr(vm, ARG(1))));
    return as_undef();
}

// GetLocalGamerTag(port): the player's name, shown on unlock banners ("... found a new weapon")
// and the stat screen. The Vita has one local profile per port, so "Player N".
static AsVal gamer_tag(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    char s[16];
    snprintf(s, sizeof s, "Player %d", (int)as_tonum(vm, ARG(0)) + 1);
    return as_strv(as_str(vm, s));
}

// AddShader(name), from a clip's frame script: a screen effect on that clip. "Heat" (lava
// world's haze) makes the picture behind the clip shimmer, which the Vita renderer draws;
// "RasterHack" (the skies) only hides seams in the cloud loops, so there's nothing to do.
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
    if (!model) return 0;   // not a level, or nothing to copy from
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

static int bsp_pending;

static AsVal bsp_load_level(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_; (void)argc; (void)argv;
    AsVal pg = as_get(vm, as_objv(&vm->root->obj), as_atom(vm, "p_game"));
    if (pg.t == V_OBJ && pg.o->type == OBJ_MC) {
        as_mc_goto(vm, (AsMc *)pg.o, 1, 0);
        bsp_pending = 1;
    }
    return as_undef();
}

static void after_frame_scripts(AsVm *vm) {
    if (!bsp_pending) return;
    bsp_pending = 0;
    as_call_method(vm, as_objv(&vm->root->obj), "f_InitLevelBSP", 0, NULL);
    AsVal pg = as_get(vm, as_objv(&vm->root->obj), as_atom(vm, "p_game"));
    if (pg.t == V_OBJ && pg.o->type == OBJ_MC) as_mc_goto(vm, (AsMc *)pg.o, 2, 0);
}

// f_LoadNewLevel waits for loader.b_loaded after a loadMovie. Menus set it from
// their own frame script, but level files never do: the executable sets it
// once the file is in.
static void on_loaded(AsVm *vm, AsMc *target) {
    as_set(vm, as_objv(&target->obj), as_atom(vm, "b_loaded"), as_bool(1));
}

static const char *text_for_number(AsVm *vm, int n) {
    (void)vm;
    return strtab_number(n);
}

void cc_host_init(AsVm *vm) {
    vm->loaded = on_loaded;
    vm->text_for_number = text_for_number;
    vm->after_frame_scripts = after_frame_scripts;
    cc_native_init(vm);
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
    as_def_fn(vm, g, "UnlockAchievement", ret_undef);  // no achievements on the Vita (yet)
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
    as_def_fn(vm, g, "HideLoadingScreen", ret_undef);
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
