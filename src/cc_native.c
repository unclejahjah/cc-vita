// Native stand-ins for the game's hottest script functions. Each one does exactly what
// the function's bytecode does (same arithmetic, same comparisons, same side effects on
// the game's variables), only without the interpreter. They're attached by name when
// MAIN.swf defines the function (as_override).
#include <math.h>
#include <stdlib.h>

#include "cc_host.h"

// ---- collision: f_BSPHitTest(x1, y1, x2, y2) and its helpers ------------------------
// a_BSP is a flat array of nodes: [i+1..i+4] the splitting segment, [i+5] its type,
// [i+6], [i+7] the child node indices (< 0: none). bsp_working_line holds the query
// segment and, after a hit, the hit segment's type, index and slope.

static AsStr *A_aBSP, *A_wl, *A_x1, *A_y1, *A_x2, *A_y2, *A_n_type, *A_n_index, *A_n_slope;

typedef struct {
    AsVm *vm;
    AsObj *bsp;
    AsVal wl;
    double x1, y1, x2, y2;     // the query segment (bsp_working_line)
} Bsp;

static double node(const Bsp *b, double k) {
    int i = (int)k;
    if (b->bsp && k == i && i >= 0 && i < b->bsp->nitems) return as_tonum(b->vm, b->bsp->items[i]);
    return as_tonum(b->vm, b->bsp ? as_get(b->vm, as_objv(b->bsp), as_tostr(b->vm, as_num(k))) : as_undef());
}

static int truthy(double v) { return v != 0 && !isnan(v); }

static double check_line_hit(Bsp *b, double i) {
    double r14 = 0;
    double r2 = node(b, i + 1), r1 = node(b, i + 2), r8 = node(b, i + 3), r6 = node(b, i + 4);
    double r4 = b->x1, r3 = b->y1, r12 = b->x2, r11 = b->y2;
    double r13 = -1, r9 = -1;
    double r10 = (r1 - r3) * (r12 - r4) - (r2 - r4) * (r11 - r3);
    double r5 = (r8 - r2) * (r11 - r3) - (r6 - r1) * (r12 - r4);
    if (!(r5 == 0)) r13 = r10 / r5;
    if (!(r13 < 0) && !(r13 > 1)) {
        r10 = (r3 - r1) * (r8 - r2) - (r4 - r2) * (r6 - r1);
        r5 = (r12 - r4) * (r6 - r1) - (r11 - r3) * (r8 - r2);
        if (!(r5 == 0)) r9 = r10 / r5;
        if (!(r9 < 0) && !(r9 > 1)) {
            as_set(b->vm, b->wl, A_n_type, as_num(node(b, i + 5)));
            as_set(b->vm, b->wl, A_n_index, as_num(i));
            as_set(b->vm, b->wl, A_n_slope, as_num((r6 - r1) / (r8 - r2)));
            r14 = r9;
        }
    }
    return r14;
}

static double check_node(Bsp *b, double i, int depth) {
    double r2 = 0;
    if (depth > 4096) return 0;   // a broken tree: the script would recurse until it gave up
    double c6 = node(b, i + 6), c7 = node(b, i + 7);
    if (c6 < 0 && c7 < 0) return check_line_hit(b, i);
    double dx = node(b, i + 3) - node(b, i + 1), dy = node(b, i + 4) - node(b, i + 2);
    double r9 = b->x1 - node(b, i + 1), r8 = b->y1 - node(b, i + 2);
    double r5 = dx * r8 - dy * r9;
    r9 = b->x2 - node(b, i + 1);
    r8 = b->y2 - node(b, i + 2);
    double r4 = dx * r8 - dy * r9;
    int side = 9;
    if (r5 == 0 && r4 == 0) side = 0;
    else if (!(r5 < 0) && !(r4 < 0)) side = 1;
    else if (!(r5 > 0) && !(r4 > 0)) side = 2;
    else if (!(r5 < 0) && !(r4 > 0)) side = 3;
    else if (!(r5 > 0) && !(r4 < 0)) side = 4;
    if (side < 3) {
        if (side == 1) r2 = c6 > 0 ? check_node(b, c6, depth + 1) : 0;
        else if (side == 2) r2 = c7 > 0 ? check_node(b, c7, depth + 1) : 0;
    } else if (side == 3 || side == 4) {
        double first = side == 3 ? c6 : c7, second = side == 3 ? c7 : c6;
        if (first > 0) r2 = check_node(b, first, depth + 1);
        if (!truthy(r2)) r2 = check_line_hit(b, i);
        if (!truthy(r2) && second > 0) r2 = check_node(b, second, depth + 1);
    }
    return r2;
}

static void bsp_begin(AsVm *vm, Bsp *b) {
    b->vm = vm;
    AsVal a = as_get(vm, as_objv(&vm->root->obj), A_aBSP);
    b->bsp = a.t == V_OBJ ? a.o : NULL;
    b->wl = as_get(vm, as_objv(&vm->root->obj), A_wl);
    b->x1 = as_tonum(vm, as_get(vm, b->wl, A_x1));
    b->y1 = as_tonum(vm, as_get(vm, b->wl, A_y1));
    b->x2 = as_tonum(vm, as_get(vm, b->wl, A_x2));
    b->y2 = as_tonum(vm, as_get(vm, b->wl, A_y2));
}

#define ARG(i) ((i) < argc ? argv[i] : as_undef())

static AsVal native_bsp_hit_test(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    AsVal wl = as_get(vm, as_objv(&vm->root->obj), A_wl);
    as_set(vm, wl, A_x1, ARG(0));
    as_set(vm, wl, A_y1, ARG(1));
    as_set(vm, wl, A_x2, ARG(2));
    as_set(vm, wl, A_y2, ARG(3));
    as_set(vm, wl, A_n_type, as_num(0));
    as_set(vm, wl, A_n_index, as_num(0));
    Bsp b;
    bsp_begin(vm, &b);
    return as_num(check_node(&b, 0, 0));
}

static AsVal native_check_node(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    Bsp b;
    bsp_begin(vm, &b);
    return as_num(check_node(&b, as_tonum(vm, ARG(0)), 0));
}

static AsVal native_check_line_hit(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    Bsp b;
    bsp_begin(vm, &b);
    return as_num(check_line_hit(&b, as_tonum(vm, ARG(0))));
}

// ---- f_GetDefaultWeapon(hud): the weapon each character starts with --------------------
// Same table as the script. One addition: f_LoadCharacterData calls it right after
// reading the saved weapon and just before the four stats, and a character that was
// never played reads all zeros there. The executable seeds a new character with its
// default weapon and 1 in each stat (2 * level + 2 = 4 points at level 1); our storage
// starts empty, so a fresh character here is seeded the same way before the stats are
// read. (With weapon 0 a swapped-out weapon dropped as an empty pickup that never showed
// its stats bubble; with stats 0 the stat screen offered 4 unspent points at level 1.)
// Character data at 64 + 48 * (p_type - 1): 128 (unlocked), level - 1, 4 exp bytes, weapon,
// animal, strength, defense, magic, agility, ... (f_SaveCharacterData's order).
static void seed_new_character(AsVm *vm, AsVal hud, int p_type, int weapon) {
    double *s = cc_storage(as_toint32(vm, as_get(vm, hud, as_atom(vm, "port"))) - 1);
    if (!s) return;
    double *c = s + 64 + 48 * (p_type - 1);
    for (int i = 1; i <= 11; i++)
        if (i != 7 && c[i] != 0) return;   // played before (the animal can be set alone)
    c[6] = weapon;
    c[8] = c[9] = c[10] = c[11] = 1;
    cc_storage_dirty = 1;
}

static const uint8_t default_weapon[32] = {
    0, 3, 25, 39, 56, 2, 19, 10, 18, 34, 27, 47, 36, 12, 33, 15,
    15, 6, 45, 26, 57, 43, 20, 2, 35, 31, 48, 50, 68, 63, 85, 84,
};

static AsVal native_get_default_weapon(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    AsVal zone = ARG(0);
    double t = as_tonum(vm, as_get(vm, zone, as_atom(vm, "p_type")));
    int w = t >= 1 && t <= 31 && t == (int)t ? default_weapon[(int)t] : 0;
    AsStr *A_weapon = as_atom(vm, "weapon");
    if (w && zone.t == V_OBJ && as_tonum(vm, as_get(vm, zone, A_weapon)) == 0) {
        as_set(vm, zone, A_weapon, as_num(w));
        seed_new_character(vm, zone, (int)t, w);
    }
    return as_num(w);
}

// ---- f_GetButtonArt(icon, code): which button a prompt icon shows ----------------------
// Every prompt icon is an 8-frame clip: 1 X, 2 Y, 3 A, 4 B, 5 LT, 6 RT (Xbox art, turned into
// Square, Triangle, Cross, Circle, L, R by cc_buttons.c), 7 a keyboard key, 8 blank. MAIN.swf
// only has the PC developer version (codes 1-4, anything else the left trigger); the
// executable answers every code the files use. 1-6 are the game's buttons in that order;
// the others are actions: 16 accept/continue (A), 17 back (B), 18 invite (Y), 19 select (X),
// 22 the map's "Return Home" (light attack, X) and 23 its "Stats" (heavy attack, Y).
static int button_frame(int code) {
    switch (code) {
    case 1: case 2: case 3: case 4: case 5: case 6: return code;
    case 16: return 3;
    case 17: return 4;
    case 18: return 2;
    case 19: return 1;
    case 22: return 1;
    case 23: return 2;
    default: return 8;
    }
}

static AsVal native_get_button_art(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    AsVal icon = ARG(0);
    if (icon.t == V_OBJ && icon.o->type == OBJ_MC)
        as_mc_goto(vm, (AsMc *)icon.o, button_frame(as_toint32(vm, ARG(1))) - 1, 0);
    return as_undef();
}

// ---- the title menu ---------------------------------------------------------------------
// MENU.swf's "buttonchoices" panel as the Vita shows it: Local Game, Help & Options and Quit
// (the online, download, leaderboard and store buttons are hidden), evenly spaced in the
// panel's own top three slots. Buttons by timeline depth; labels in panel.txtClip.
static const int MENU_DEPTH[CC_MENU_ITEMS] = {9, 13, 17};

static AsMc *panel_child(AsMc *panel, int depth) {
    for (int i = 0; panel && i < panel->ndisp; i++)
        if (panel->disp[i].depth == depth - 16384 && panel->disp[i].kind == DISP_MC) return panel->disp[i].mc;
    return NULL;
}

static void set_num(AsVm *vm, AsMc *mc, const char *k, double v) {
    if (mc) as_set(vm, as_objv(&mc->obj), as_atom(vm, k), as_num(v));
}

static AsMc *panel_path(AsVm *vm, AsMc *panel, const char *path) {
    int found;
    AsVal v = as_resolve_path(vm, panel, path, &found);
    return found && v.t == V_OBJ && v.o->type == OBJ_MC ? (AsMc *)v.o : NULL;
}

AsMc *cc_menu_attach(AsVm *vm, float x, float y) {
    AsVal args[3] = {as_strv(as_str(vm, "buttonchoices")), as_strv(as_str(vm, "menupanel")), as_num(5000)};
    AsVal p = as_call_method(vm, as_objv(&vm->root->obj), "attachMovie", 3, args);
    if (p.t != V_OBJ || p.o->type != OBJ_MC) return NULL;
    AsMc *panel = (AsMc *)p.o;
    set_num(vm, panel, "_x", x);
    set_num(vm, panel, "_y", y);
    int found;
    static const char *news_box[] = {"body.newsfeed", "body.scroll"};   // the empty news box
    for (int i = 0; i < 2; i++) {
        AsVal news = as_resolve_path(vm, vm->root, news_box[i], &found);
        if (found) as_set(vm, news, as_atom(vm, "_visible"), as_bool(0));
    }
    AsVal body = as_resolve_path(vm, vm->root, "body", &found);   // and its unnamed frame (depth 63)
    if (found && body.t == V_OBJ && body.o->type == OBJ_MC) {
        AsMc *frame = panel_child((AsMc *)body.o, 63);
        if (frame) frame->visible = 0;
    }
    static const int hide[] = {11, 15, 3, 5, 7, 19, 21, 24, 26, 28};   // online (+ its icon), download, the 3 icon buttons (+ icons), merch
    for (size_t i = 0; i < sizeof hide / sizeof *hide; i++) {
        AsMc *b = panel_child(panel, hide[i]);
        if (b) b->visible = 0;
    }
    static const char *hide_text[] = {"txtClip.textMultiplayer", "txtClip.textDownload", "txtClip.textMerch"};
    for (size_t i = 0; i < 3; i++) {
        AsMc *t = panel_path(vm, panel, hide_text[i]);
        if (t) t->visible = 0;
    }
    // Help & Options and Quit move up into the 2nd and 3rd slots (57 px apart), labels with them
    AsMc *opt = panel_child(panel, 13), *quit = panel_child(panel, 17), *local = panel_child(panel, 9);
    AsMc *topt = panel_path(vm, panel, "txtClip.textOptions"), *tquit = panel_path(vm, panel, "txtClip.textQuit");
    AsMc *tlocal = panel_path(vm, panel, "txtClip.textLocalGame");
    if (opt && local && topt && tlocal) {
        double ly = local->m.ty / 20, dy = ly + 57 - opt->m.ty / 20;
        set_num(vm, opt, "_y", opt->m.ty / 20 + dy);
        set_num(vm, topt, "_y", topt->m.ty / 20 + dy);
        if (quit && tquit) {
            // Quit is a half-width button drawn left of its origin: centre it under the others
            double qdy = ly + 114 - quit->m.ty / 20, qdx = local->m.tx / 20 - quit->m.tx / 20 + 68;
            set_num(vm, quit, "_y", quit->m.ty / 20 + qdy);
            set_num(vm, quit, "_x", quit->m.tx / 20 + qdx);
            set_num(vm, tquit, "_y", tquit->m.ty / 20 + qdy);
            set_num(vm, tquit, "_x", tquit->m.tx / 20 + qdx);
        }
    }
    return panel;
}

void cc_menu_highlight(AsVm *vm, AsMc *panel, int item) {
    for (int i = 0; i < CC_MENU_ITEMS; i++) {
        AsMc *b = panel_child(panel, MENU_DEPTH[i]);
        set_num(vm, b, "_xscale", i == item ? 112 : 100);
        set_num(vm, b, "_yscale", i == item ? 112 : 100);
    }
}

void cc_native_root_functions(AsVm *vm, AsObj *root) {
    as_def_fn(vm, root, "f_GetButtonArt", native_get_button_art);
}

void cc_native_init(AsVm *vm) {
    as_override(vm, "f_GetDefaultWeapon", native_get_default_weapon);
    as_override(vm, "f_GetButtonArt", native_get_button_art);
    A_aBSP = as_atom(vm, "a_BSP");
    A_wl = as_atom(vm, "bsp_working_line");
    A_x1 = as_atom(vm, "x1");
    A_y1 = as_atom(vm, "y1");
    A_x2 = as_atom(vm, "x2");
    A_y2 = as_atom(vm, "y2");
    A_n_type = as_atom(vm, "n_type");
    A_n_index = as_atom(vm, "n_index");
    A_n_slope = as_atom(vm, "n_slope");
    if (getenv("CC_NO_NATIVE")) return;   // run the scripts' own versions (for comparing)
    as_override(vm, "f_BSPHitTest", native_bsp_hit_test);
    as_override(vm, "f_CheckNode", native_check_node);
    as_override(vm, "f_CheckLineHit", native_check_line_hit);
}
