// Paint Jr. as an add-on: see paint_jr.h.
#include "paint_jr.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static int g_available, g_new_player, g_fresh;
void cc_paintjr_set_available(int new_player, int pj_parts, int paint_jr) {
    g_new_player = new_player;
    g_available = pj_parts && paint_jr;
}

static int g_off;   // a Wi-Fi game: off, so every Vita runs the same characters
void cc_paintjr_set_active(int on) { g_off = !on; }
int cc_paintjr_p_type(void) { return g_available && !g_off ? CC_PAINTJR_P_TYPE : 0; }
int cc_fresh_available(void) { return g_new_player; }
void cc_fresh_set(int on) { g_fresh = on; }
int cc_fresh(void) { return g_fresh && g_new_player; }

const char *cc_player_file(const char *base) {
    return cc_fresh() && !strcasecmp(base, "player.swf") ? "PLAYER_NEW.swf" : base;
}

int cc_portrait_frame(int frame, int nframes) {
    int t = frame - 2;   // the character
    if (t < 1 || t > CC_PAINTJR_P_TYPE || (t != cc_paintjr_p_type() && !cc_fresh())) return -1;
    int f = CC_NEW_PORTRAITS + t - 1;
    return f < nframes ? f : -1;
}

// ---- his pieces of PLAYER.swf's part clips (PJPARTS.swf) ----------------------------------

// The part clips call one of MAIN's selectors on their first frame (_root.f_Helmet(this) ...);
// they are told apart by selector and their order in the file, as custom_char.c does. PJPARTS.swf
// exports the PC's version of each as "pj_<selector>_<n>" (tools/paintjr/make_paintjr.py).
static const char *const SELECTOR[] = {"f_Helmet", "f_Belt", "f_Weapon", "f_Shield"};
static const char *const GROUP[] = {"helmet", "belt", "weapon", "shield"};
#define NGROUPS 4
#define MAX_PARTS 64
#define PENCIL 86   // his weapon: empty in the original PLAYER.swf's weapon clip

static struct {
    const Movie *player;   // the PLAYER.swf the table is for
    int n;
    uint16_t id[MAX_PARTS];     // its part clip
    const Sprite *pj[MAX_PARTS];   // and PJPARTS.swf's version of it
    uint8_t weapon[MAX_PARTS];
} g_parts;
static Movie *g_pjparts;
static int g_pjparts_missing;

static int calls(const Movie *m, const Sprite *s, const char *fn) {
    size_t l = strlen(fn) + 1;   // with its NUL: a whole name in the constant pool
    for (int i = 0; i < s->nactions && s->actions[i].frame == 0; i++) {
        const uint8_t *p = m->data + s->actions[i].off, *e = p + s->actions[i].len;
        for (; p + l <= e; p++)
            if (*p == (uint8_t)fn[0] && !memcmp(p, fn, l)) return 1;
    }
    return 0;
}

static const Sprite *export_sprite(const Movie *m, const char *name) {
    for (int c = 1; c < 65536; c++)
        if (m->chars[c].type == CHAR_SPRITE && m->chars[c].export && !strcmp(m->names[m->chars[c].export], name))
            return m->chars[c].ptr;
    return NULL;
}

static void index_parts(AsVm *vm, const Movie *player) {
    memset(&g_parts, 0, sizeof g_parts);
    g_parts.player = player;
    if (!g_pjparts && !g_pjparts_missing) {
        g_pjparts = vm && vm->loader ? vm->loader(vm, "PJPARTS.swf") : NULL;
        if (!g_pjparts) g_pjparts_missing = 1;
    }
    if (!g_pjparts) return;
    int nth[NGROUPS] = {0};
    for (int id = 0; id < 65536 && g_parts.n < MAX_PARTS; id++) {
        if (player->chars[id].type != CHAR_SPRITE) continue;
        const Sprite *s = player->chars[id].ptr;
        for (int g = 0; g < NGROUPS; g++) {
            if (!calls(player, s, SELECTOR[g])) continue;
            char name[32];
            snprintf(name, sizeof name, "pj_%s_%d", GROUP[g], nth[g]++);
            const Sprite *pj = export_sprite(g_pjparts, name);
            if (pj) {
                g_parts.id[g_parts.n] = (uint16_t)id;
                g_parts.pj[g_parts.n] = pj;
                g_parts.weapon[g_parts.n++] = g == 2;
            }
            break;
        }
    }
}

static AsVm *g_vm;

int cc_paintjr_part(const Movie *m, const Sprite *s, int frame, int p_type, Movie **mv, const Frame **fr) {
    if (!cc_paintjr_p_type() || !m->is_player) return 0;
    if (g_parts.player != m) index_parts(g_vm, m);
    for (int i = 0; i < g_parts.n; i++) {
        if (g_parts.id[i] != s->id) continue;
        const Sprite *pj = g_parts.pj[i];
        int f = g_parts.weapon[i] ? (frame + 1 == PENCIL ? PENCIL - 1 : -1)
                                  : (p_type == CC_PAINTJR_P_TYPE ? CC_PAINTJR_P_TYPE : -1);
        if (f < 0 || f >= pj->nframes) return 0;
        *mv = g_pjparts;
        *fr = &pj->frames[f];
        return 1;
    }
    return 0;
}

// ---- his magic ----------------------------------------------------------------------------

// his functions from the PC's MAIN.swf, defined on _root the first time he's needed (MAIN's own
// are in by then: they are defined on its first frame)
static int g_grafted;

static void graft(AsVm *vm) {
    if (g_grafted) return;
    g_grafted = 1;
    Movie *m = vm->loader ? vm->loader(vm, "PAINTJR.swf") : NULL;
    if (!m) { as_warn(vm, "Paint Jr: PAINTJR.swf missing: no magic"); return; }
    for (int i = 0; i < m->root.nactions; i++)
        if (m->root.actions[i].frame == 0) as_run_block(vm, m, m->root.actions[i].off, m->root.actions[i].len, vm->root);
}

static AsVal call_root(AsVm *vm, const char *name, int argc, AsVal *argv) {
    AsVal root = as_objv(&vm->root->obj);
    AsVal fn = as_get(vm, root, as_atom(vm, name));
    return fn.t == V_OBJ ? as_call(vm, fn, root, argc, argv) : as_undef();
}

static int is_paint_jr_magic(AsVm *vm, AsVal zone) {
    return zone.t == V_OBJ && as_tonum(vm, as_get(vm, zone, as_atom(vm, "magic_type"))) == CC_PAINTJR_P_TYPE + 1;
}

// f_ShootMagic(zone) and f_MagicBulletDown(zone): his magic type (33) throws his paint minion,
// as the PC's versions of both do (the original MAIN has no type 33)
static AsVal paint_jr_magic(AsVm *vm, AsVal zone, int down) {
    graft(vm);
    AsVal args[2] = {zone, as_bool(down)};
    return call_root(vm, "f_PaintJrMagicSplash", 2, args);
}

static AsVal shoot_magic(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (argc > 0 && is_paint_jr_magic(vm, argv[0])) { paint_jr_magic(vm, argv[0], 0); return as_undef(); }
    return as_call_script(vm, this_, argc, argv);
}

static AsVal magic_bullet_down(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (argc > 0 && is_paint_jr_magic(vm, argv[0])) { paint_jr_magic(vm, argv[0], 1); return as_undef(); }
    return as_call_script(vm, this_, argc, argv);
}

void cc_paintjr_init(AsVm *vm) {
    g_grafted = 0;
    g_vm = vm;
    g_parts.player = NULL;   // a new VM: the files are looked up again
    g_pjparts = NULL;
    g_pjparts_missing = 0;
    if (!cc_paintjr_p_type()) return;
    as_override(vm, "f_ShootMagic", shoot_magic);
    as_override(vm, "f_MagicBulletDown", magic_bullet_down);
}
