// Weapon looks in the weapon frog's armoury: see weapon_look.h.
#include "weapon_look.h"
#include "cc_host.h"
#include "custom_char.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARG(i) ((i) < argc ? argv[i] : as_undef())
#define PORTS 4
#define TYPES 64          // character numbers (31 + Paint Jr. + the custom one, with room)

// look per port (1-based) and character: 0 none, a weapon number, or CC_LOOK_CUSTOM
static int16_t g_saved[PORTS + 1][TYPES], g_session[PORTS + 1][TYPES];
static int g_off;
static char g_path[256];

static int16_t (*looks(void))[TYPES] { return g_off ? g_session : g_saved; }

void cc_look_load(const char *path) {
    snprintf(g_path, sizeof g_path, "%s", path);
    memset(g_saved, 0, sizeof g_saved);
    FILE *f = fopen(path, "r");
    if (!f) return;
    int port, t, look;
    while (fscanf(f, "%d %d %d", &port, &t, &look) == 3)
        if (port >= 1 && port <= PORTS && t >= 1 && t < TYPES) g_saved[port][t] = (int16_t)look;
    fclose(f);
}

static void save(void) {
    if (g_off || !g_path[0]) return;
    FILE *f = fopen(g_path, "w");
    if (!f) return;
    for (int port = 1; port <= PORTS; port++)
        for (int t = 1; t < TYPES; t++)
            if (g_saved[port][t]) fprintf(f, "%d %d %d\n", port, t, g_saved[port][t]);
    fclose(f);
}

void cc_look_set_active(int on) {
    g_off = !on;
    memset(g_session, 0, sizeof g_session);
}

static AsStr *A_p_type, *A_hud_pt, *A_port, *A_weapon, *A_cc_look, *A_alive, *A_x, *A_y, *A_n_weapon,
    *A_button_punch2, *A_body_y, *A_locked, *A_spot_reset, *A_spot_custom, *A_spark, *A_counter, *A_blank;

static double num(AsVm *vm, AsVal o, AsStr *k) { return as_tonum(vm, as_get(vm, o, k)); }

static int is_clip(AsVal v) { return v.t == V_OBJ && v.o->type == OBJ_MC; }

// the look slot of player clip p (its port and character), or NULL
static int16_t *slot_of(AsVm *vm, AsVal p) {
    if (!is_clip(p)) return NULL;
    AsVal hud = as_get(vm, p, A_hud_pt);
    if (hud.t != V_OBJ) return NULL;
    double port = num(vm, hud, A_port), t = num(vm, p, A_p_type);
    if (!(port >= 1 && port <= PORTS && t >= 1 && t < TYPES)) return NULL;
    return &looks()[(int)port][(int)t];
}

// ---- f_Weapon(zone): a player's weapon part clip shows their weapon's frame -----------------
static int any_look(void) {
    int16_t(*l)[TYPES] = looks();
    for (int port = 1; port <= PORTS; port++)
        for (int t = 1; t < TYPES; t++)
            if (l[port][t]) return 1;
    return 0;
}

static AsVal native_weapon(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (!any_look()) return as_call_script(vm, this_, argc, argv);   // as the game has it
    AsVal zone = ARG(0);
    AsVal p = as_call_method(vm, as_objv(&vm->root->obj), "f_GetPlayerClip", 1, &zone);
    int16_t *s = slot_of(vm, p);
    int look = s ? *s : 0;
    if (look == CC_LOOK_CUSTOM && !cc_custom_weapon()) look = 0;   // no painting (or a Wi-Fi game)
    if (is_clip(p)) as_set(vm, p, A_cc_look, as_num(look));       // for the painted weapon (as_render.c)
    if (!look || look == CC_LOOK_CUSTOM || !is_clip(zone)) return as_call_script(vm, this_, argc, argv);
    AsVal f = as_num(look);
    as_call_method(vm, zone, "gotoAndStop", 1, &f);
    return as_undef();
}

const BitmapFill *cc_look_custom_part(AsMc *mc) {
    if (!mc->def || !mc->movie->is_player || !cc_custom_weapon()) return NULL;
    if (!cc_custom_is_weapon_part(mc->movie, mc->def)) return NULL;
    AsMc *p = mc->parent;
    for (int i = 0; p && i < 8; i++, p = p->parent) {
        if (!as_own(&p->obj, A_p_type)) continue;
        AsProp *l = as_own(&p->obj, A_cc_look);
        return l && l->v.t == V_NUM && l->v.n == CC_LOOK_CUSTOM ? cc_custom_weapon() : NULL;
    }
    return NULL;
}

// ---- the armoury (LEVEL15's f_CheckGetWeapon, every frame) ------------------------------------
// The rack starts at the "counter" clip (x 362): players behind it (y above its) take a weapon
// with light attack. The first section of rope, from the pillar at x 338, is copied this far to
// the left (a pillar of its own), and the two spots hang on it.
#define ROPE_SHIFT 230
#define SPOT_Y (-203)
static const int SPOT_X[2] = {200, 265};   // reset, custom (only the reset one: between them)

static AsMc *child(AsMc *mc, AsStr *name) { return as_mc_child(mc, name); }

static void set_scale(AsVm *vm, AsMc *mc, double s) {
    as_mc_set_prop(vm, mc, as_mc_prop_index("_xscale"), as_num(s));
    as_mc_set_prop(vm, mc, as_mc_prop_index("_yscale"), as_num(s));
}

static int depth_free(AsMc *mc, int depth) {
    for (int i = 0; i < mc->ndisp; i++)
        if (mc->disp[i].depth == depth) return 0;
    return 1;
}

static void build(AsVm *vm, AsMc *game, AsMc *w3) {
    int n = game->ndisp;
    for (int i = 0; i < n; i++) {   // the rope's first section: pillar, rope, wraps, knot
        AsDisp e = game->disp[i];
        Mat m = e.kind == DISP_MC && e.mc ? e.mc->m : e.m;
        float x = m.tx / 20, y = m.ty / 20;
        if (x < 330 || x > 370 || y < -240 || y > -110 || !e.cid) continue;
        if (e.kind == DISP_MC && (!e.mc || (e.mc->name && strncmp(e.mc->name->s, "instance", 8)))) continue;
        if (!depth_free(game, e.depth + 1)) continue;
        m.tx -= ROPE_SHIFT * 20;
        as_mc_place_char(vm, game, e.cid, m, e.kind == DISP_MC ? e.mc->cx : e.cx, NULL, e.depth + 1);
    }
    int cid = 0, depth = 0;
    for (int i = 0; i < game->ndisp; i++)
        if (game->disp[i].mc == w3) cid = game->disp[i].cid, depth = game->disp[i].depth;
    if (!cid) return;
    int custom = cc_custom_weapon() != NULL;
    for (int k = 0; k <= custom; k++) {
        Mat m = w3->m;
        m.tx = (custom ? SPOT_X[k] : (SPOT_X[0] + SPOT_X[1]) / 2) * 20.0f;
        m.ty = SPOT_Y * 20.0f;
        for (int d = depth + 1; d < depth + 40; d++)
            if (depth_free(game, d)) {
                AsMc *s = as_mc_place_char(vm, game, cid, m, w3->cx, k ? "w_look_custom" : "w_look_reset", d);
                if (s && k) {
                    int f = as_mc_frame_of(s, as_strv(A_blank));
                    if (f >= 0) as_mc_goto(vm, s, f, 0);
                }
                break;
            }
    }
}

static int prev_down[PORTS + 1];

static void armoury(AsVm *vm) {
    int found;
    AsVal gv = as_resolve_path(vm, vm->root, "loader.game.game", &found);
    if (!is_clip(gv)) return;
    AsMc *game = (AsMc *)gv.o;
    AsMc *w3 = child(game, as_atom(vm, "w3")), *counter = child(game, A_counter);
    if (!w3 || !counter) return;
    if (!child(game, A_spot_reset)) build(vm, game, w3);
    AsMc *spot[2] = {child(game, A_spot_reset), child(game, A_spot_custom)};
    double cx = counter->m.tx / 20, cy = counter->m.ty / 20;
    int near_any[2] = {0, 0}, shown = 0;
    for (int port = 1; port <= PORTS; port++) {
        char name[4] = {'p', (char)('0' + port), 0};
        AsMc *pm = child(game, as_atom(vm, name));
        if (!pm) continue;
        AsVal p = as_objv(&pm->obj);
        int alive = as_tobool(vm, as_get(vm, p, A_alive));
        double x = num(vm, p, A_x), y = num(vm, p, A_y);
        int16_t *s = slot_of(vm, p);
        int key = (int)num(vm, p, A_button_punch2);
        int down = key > 0 && key < 256 && cc_keys[key];
        int pressed = down && !prev_down[port];
        prev_down[port] = down;
        if (!alive || !s) continue;
        int real = (int)num(vm, p, A_weapon);
        if (!shown) shown = real;   // the reset spot shows the first player's own weapon...
        int at = -1;
        if (x < cx && y < cy)
            for (int k = 0; k < 2; k++) {
                if (!spot[k]) continue;
                double d = x - spot[k]->m.tx / 20;
                if (d > -50 && d < 50 && (at < 0 || abs((int)d) < abs((int)(x - spot[at]->m.tx / 20)))) at = k;
            }
        if (at >= 0) {
            near_any[at] = 1;
            if (at == 0) shown = real;   // ...or the one in front of it
        }
        if (!pressed) continue;
        int look = -1;
        AsMc *took = NULL;
        if (at >= 0) {
            look = at == 0 ? 0 : CC_LOOK_CUSTOM;
            took = spot[at];
        } else {
            AsVal nw = as_get(vm, p, A_n_weapon);   // the rack weapon the script has picked out
            if (nw.t == V_NUM && nw.n >= 1) {
                char wn[8];
                snprintf(wn, sizeof wn, "w%d", (int)nw.n);
                took = child(game, as_atom(vm, wn));
                if (took && !as_tobool(vm, as_get(vm, as_objv(&took->obj), A_locked)))
                    look = (int)nw.n == real ? 0 : (int)nw.n;
            }
        }
        if (look < 0 || look == *s) continue;
        *s = (int16_t)look;
        as_set(vm, p, A_cc_look, as_num(look));
        save();
        AsMc *spark = took ? child(took, A_spark) : NULL;
        if (spark) as_call_method(vm, as_objv(&spark->obj), "play", 0, NULL);
        if (num(vm, p, A_body_y) >= 0)   // show it now, as the script does on a weapon swap
            as_call_method(vm, p, "fp_StandAnim", 1, &p);
    }
    for (int k = 0; k < 2; k++)
        if (spot[k]) set_scale(vm, spot[k], near_any[k] ? 120 : 100);
    if (spot[0] && shown > 0 && spot[0]->frame != shown - 1) as_mc_goto(vm, spot[0], shown - 1, 0);
}

static AsVal native_check_get_weapon(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsVal r = as_call_script(vm, this_, argc, argv);
    armoury(vm);
    return r;
}

const BitmapFill *cc_look_custom_spot(AsMc *mc, Mat *at) {
    if (mc->name != A_spot_custom || !A_spot_custom) return NULL;
    const BitmapFill *f = cc_custom_weapon();
    if (!f) return NULL;
    *at = MAT_IDENTITY;   // the painting's weapon has its grip at its origin, blade up, as the rack's
    return f;
}

void cc_look_init(AsVm *vm) {
    A_p_type = as_atom(vm, "p_type");
    A_hud_pt = as_atom(vm, "hud_pt");
    A_port = as_atom(vm, "port");
    A_weapon = as_atom(vm, "weapon");
    A_cc_look = as_atom(vm, "cc_look");
    A_alive = as_atom(vm, "alive");
    A_x = as_atom(vm, "x");
    A_y = as_atom(vm, "y");
    A_n_weapon = as_atom(vm, "n_weapon");
    A_button_punch2 = as_atom(vm, "button_punch2");
    A_body_y = as_atom(vm, "body_y");
    A_locked = as_atom(vm, "locked");
    A_spot_reset = as_atom(vm, "w_look_reset");
    A_spot_custom = as_atom(vm, "w_look_custom");
    A_spark = as_atom(vm, "spark");
    A_counter = as_atom(vm, "counter");
    A_blank = as_atom(vm, "blank");
    memset(prev_down, 0, sizeof prev_down);
    as_override(vm, "f_Weapon", native_weapon);
    as_override(vm, "f_CheckGetWeapon", native_check_get_weapon);
}
