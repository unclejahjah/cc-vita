// Native stand-ins for the game's hottest script functions. Each one does exactly what
// the function's bytecode does (same arithmetic, same comparisons, same side effects on
// the game's variables), only without the interpreter. They're attached by name when
// MAIN.swf defines the function (as_override).
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "cc_host.h"
#include "as_render.h"

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

// ---- f_SpawnBear(x, y, enemy_level): bears start inside the walls ----------------------
// Tall Grass Field's bear boss sends helpers in from main.right + 150 at the boss's _y
// (-346) + random(200). During the boss's entrance the camera stands so that this lands
// where the top wall dips to -333 (x 1223..1567), so now and then a bear started above the
// wall and walked along the top of the field, out of reach. Players get pulled back in by
// f_CheckInsideBSP; enemies never do. Here a bear with no wall above it (nothing hit on
// the way up) starts just under the first wall below it instead; the script does the rest.
static AsVal native_spawn_bear(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsVal fv = as_get(vm, as_objv(&vm->root->obj), as_atom(vm, "f_SpawnBear"));
    if (fv.t != V_OBJ || fv.o->native != native_spawn_bear) return as_undef();
    AsVal a[3] = {ARG(0), ARG(1), ARG(2)};
    Bsp b;
    bsp_begin(vm, &b);
    b.wl = as_undef();   // the scripts' bsp_working_line stays as it was
    double x = as_tonum(vm, a[0]), y = as_tonum(vm, a[1]);
    if (b.bsp && !isnan(x) && !isnan(y)) {
        b.x1 = b.x2 = x, b.y1 = y, b.y2 = y - 1000;
        if (!truthy(check_node(&b, 0, 0))) {
            b.y2 = y + 1000;
            double r = check_node(&b, 0, 0);
            if (truthy(r)) a[1] = as_num(y + r * 1000 + 10);
        }
    }
    // the script's own f_SpawnBear with the (maybe) moved y
    AsObj *f = fv.o;
    f->native = NULL;
    AsVal r = as_call(vm, fv, this_, argc < 3 ? 3 : argc, a);
    f->native = native_spawn_bear;
    return r;
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
// The game's buttons are actions, so they show the button the player has put them on (cc_bind).
static int button_frame(int code) {
    switch (code) {
    case 1: case 2: case 3: case 4: case 5: case 6: return cc_bind[code - 1] + 1;
    case 16: return 3;
    case 17: return 4;
    case 18: return 2;
    case 19: return 1;
    case 22: return cc_bind[CC_ACT_LIGHT] + 1;
    case 23: return cc_bind[CC_ACT_HEAVY] + 1;
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
static const int MENU_DEPTH[CC_MENU_ITEMS] = {9, 13, 17, 11};
int cc_menu_wifi;   // the co-op build: the online button comes back as Wi-Fi Game, in the 2nd slot
#define MENU_SCALE 125     // the panel, larger than the original's
// per item: its width (_xscale) and where it sits (_x)
static float item_w[CC_MENU_ITEMS] = {100, 100, 100, 100}, item_x[CC_MENU_ITEMS];
#define HIGHLIGHT_DEPTH 21   // the chosen button's glowing frame and its button icon
static float hl_pad[4];      // how far it reaches past a button: left, top, right, bottom (twips)

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
    for (int i = 0; i < CC_MENU_ITEMS; i++) item_w[i] = 100;
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
    // the highlight frame (with its button icon) sits over Local Game in the file: how far it
    // reaches past the button, to put it around whichever is chosen (cc_menu_highlight)
    AsMc *hl = panel_child(panel, HIGHLIGHT_DEPTH), *lg = panel_child(panel, 9);
    if (hl && lg) {
        float hb[4], gb[4];
        render_mc_shape_bounds(hl, hl->m, hb);
        render_mc_bounds(lg, lg->m, gb);
        float p[4] = {gb[0] - hb[0], gb[1] - hb[1], hb[2] - gb[2], hb[3] - gb[3]};
        memcpy(hl_pad, p, sizeof p);
    }
    static const int hide[] = {11, 15, 3, 5, 7, 19, 21, 24, 26, 28};   // online (+ its icon), download, the 3 icon buttons (+ icons), merch
    for (size_t i = cc_menu_wifi; i < sizeof hide / sizeof *hide; i++) {
        AsMc *b = panel_child(panel, hide[i]);
        if (b) b->visible = 0;
    }
    static const char *hide_text[] = {"txtClip.textMultiplayer", "txtClip.textDownload", "txtClip.textMerch"};
    for (size_t i = cc_menu_wifi; i < 3; i++) {
        AsMc *t = panel_path(vm, panel, hide_text[i]);
        if (t) t->visible = 0;
    }
    // Help & Options moves up into the 2nd slot (57 px apart), its label with it, in line
    // with Local Game's left edge
    AsMc *opt = panel_child(panel, 13), *tile = panel_child(panel, 17), *local = panel_child(panel, 9);
    AsMc *topt = panel_path(vm, panel, "txtClip.textOptions"), *tquit = panel_path(vm, panel, "txtClip.textQuit");
    // the co-op build: Wi-Fi Game (the online button) in the 2nd slot, Help & Options the 3rd
    AsMc *wifi = cc_menu_wifi ? panel_child(panel, 11) : NULL;
    AsMc *twifi = cc_menu_wifi ? panel_path(vm, panel, "txtClip.textMultiplayer") : NULL;
    AsMc *tlocal = panel_path(vm, panel, "txtClip.textLocalGame");
    if (tquit) tquit->visible = 0;
    if (opt && local && topt && tlocal) {
        float lb[4], tb[4];
        render_mc_bounds(local, local->m, lb);
        // a button (and its label) into slot k under Local Game (57 px apart), in line with its
        // left edge and as wide (Help & Options' art is a little narrower)
        void place(AsMc *b, AsMc *label, int k, int item) {
            float ob[4];
            render_mc_bounds(b, b->m, ob);
            double dy = local->m.ty / 20 + 57 * k - b->m.ty / 20;
            double dx = lb[2] > lb[0] && ob[2] > ob[0] ? (lb[0] - ob[0]) / 20 : 0;
            set_num(vm, b, "_y", b->m.ty / 20 + dy);
            set_num(vm, b, "_x", b->m.tx / 20 + dx);
            if (lb[2] > lb[0] && ob[2] > ob[0]) item_w[item] = 100 * (lb[2] - lb[0]) / (ob[2] - ob[0]);
            if (label) {
                set_num(vm, label, "_y", label->m.ty / 20 + dy);
                set_num(vm, label, "_x", label->m.tx / 20 + dx);
            }
        }
        int k = 1;
        if (wifi) place(wifi, twifi, k++, CC_MENU_WIFI);
        place(opt, topt, k, CC_MENU_OPTIONS);
        if (tile) {
            // Quit is the power button at the bottom left, as on the newer menu: its button
            // stays as the place (hidden) and the tile is drawn there (cc_menu_power_button)
            render_mc_bounds(tile, tile->m, tb);
            if (lb[2] > lb[0] && tb[2] > tb[0]) {
                set_num(vm, tile, "_x", (tile->m.tx + lb[0] - tb[0]) / 20);
                set_num(vm, tile, "_y", (tile->m.ty + ((lb[3] - lb[1]) + 8 * 20) * k + (lb[3] - lb[1]) + lb[1] - tb[1]) / 20 + 6);
            }
            tile->visible = 0;
        }
    }
    // larger, and in the bottom left corner: what's left of the panel, MENU_SCALE %, its
    // bottom-left corner a margin in from the screen's
    if (item_w[CC_MENU_OPTIONS] != 100) set_num(vm, opt, "_xscale", item_w[CC_MENU_OPTIONS]);
    if (cc_menu_wifi && item_w[CC_MENU_WIFI] != 100) set_num(vm, panel_child(panel, 11), "_xscale", item_w[CC_MENU_WIFI]);
    float bb[4] = {1e30f, 1e30f, -1e30f, -1e30f};
    for (int i = 0; i < CC_MENU_ITEMS; i++) {
        AsMc *b = panel_child(panel, MENU_DEPTH[i]);
        if (!b || (i == CC_MENU_WIFI && !cc_menu_wifi)) continue;
        float ib[4];
        render_mc_bounds(b, b->m, ib);
        if (ib[2] <= ib[0]) continue;
        if (ib[0] < bb[0]) bb[0] = ib[0];
        if (ib[1] < bb[1]) bb[1] = ib[1];
        if (ib[2] > bb[2]) bb[2] = ib[2];
        if (ib[3] > bb[3]) bb[3] = ib[3];
    }
    if (bb[2] > bb[0] && vm->root->movie) {
        float k = MENU_SCALE / 100.0f, sh = vm->root->movie->stage_h, margin = sh * 0.06f;
        set_num(vm, panel, "_xscale", MENU_SCALE);
        set_num(vm, panel, "_yscale", MENU_SCALE);
        set_num(vm, panel, "_x", margin - bb[0] / 20 * k);
        set_num(vm, panel, "_y", sh - margin - bb[3] / 20 * k);
    }
    for (int i = 0; i < CC_MENU_ITEMS; i++) {   // for cc_menu_highlight
        AsMc *b = panel_child(panel, MENU_DEPTH[i]);
        if (b) item_x[i] = b->m.tx / 20;
    }
    return panel;
}

// the power button's square, in the panel's twips: on the (hidden) Quit button's bottom-left
// corner, a little taller than the buttons
static int power_rect(AsMc *panel, float r[4]) {
    AsMc *b = panel_child(panel, MENU_DEPTH[CC_MENU_QUIT]);
    if (!b) return 0;
    float bb[4];
    render_mc_bounds(b, b->m, bb);
    if (bb[2] <= bb[0]) return 0;
    float size = (bb[3] - bb[1]) * 1.15f;
    r[0] = bb[0];
    r[1] = bb[3] - size;
    r[2] = bb[0] + size;
    r[3] = bb[3];
    return 1;
}

static int hl_item;   // which button it is round

// what to draw over the menu, and its matrix in the panel: the highlight's icon (its frame's
// art won't go round every button, so cc_menu_power_button draws the glow)
AsMc *cc_menu_highlight_clip(AsMc *panel, Mat *m) {
    AsMc *hl = panel_child(panel, HIGHLIGHT_DEPTH);
    *m = (Mat){1, 0, 0, 1, 0, 0};
    if (!hl) return NULL;
    for (int i = 0; i < hl->ndisp; i++)
        if (hl->disp[i].kind == DISP_MC && hl->disp[i].mc) {
            *m = hl->m;
            return hl->disp[i].mc;
        }
    return NULL;
}


void cc_menu_highlight(AsVm *vm, AsMc *panel, int item) {
    // the buttons keep their size; the chosen one glows (drawn, cc_menu_power_button) with the
    // accept button (Cross) on its bottom-right corner, as the newer menu shows the power button
    AsMc *hl = panel_child(panel, HIGHLIGHT_DEPTH), *b = panel_child(panel, MENU_DEPTH[item]);
    if (!hl || !b) return;
    hl_item = item;
    float r[4], pr[4], nb[4];
    if (!power_rect(panel, pr)) return;
    if (item == CC_MENU_QUIT) memcpy(r, pr, sizeof r);
    else render_mc_bounds(b, b->m, r);
    // the frame fitted to a power-button-sized square on that corner puts the icon there
    float size = pr[2] - pr[0];
    r[0] = r[2] - size;
    r[1] = r[3] - size;
    r[0] -= hl_pad[0]; r[1] -= hl_pad[1]; r[2] += hl_pad[2]; r[3] += hl_pad[3];
    render_mc_shape_bounds(hl, (Mat){1, 0, 0, 1, 0, 0}, nb);   // the frame, not its icon
    if (nb[2] <= nb[0] || nb[3] <= nb[1]) return;
    float sx = (r[2] - r[0]) / (nb[2] - nb[0]), sy = (r[3] - r[1]) / (nb[3] - nb[1]);
    set_num(vm, hl, "_xscale", sx * 100);
    set_num(vm, hl, "_yscale", sy * 100);
    set_num(vm, hl, "_x", (r[0] - nb[0] * sx) / 20);
    set_num(vm, hl, "_y", (r[1] - nb[1] * sy) / 20);
    for (int i = 0; i < hl->ndisp; i++) {   // the icon stays round, and shows Cross
        AsMc *icon = hl->disp[i].mc;
        if (!icon) continue;
        set_num(vm, icon, "_xscale", 100 * sy / sx);
        as_mc_goto(vm, icon, button_frame(16) - 1, 0);
    }
}

// ---- the power button (the newer menu's, drawn: the original file has no art for it) ----
typedef struct { CcVert *v; int n, max; } Tris;

static void tri(Tris *t, float x0, float y0, float x1, float y1, float x2, float y2, const uint8_t c[4]) {
    if (t->n + 3 > t->max) return;
    float xy[6] = {x0, y0, x1, y1, x2, y2};
    for (int k = 0; k < 3; k++) {
        CcVert *v = &t->v[t->n++];
        v->x = xy[k * 2];
        v->y = xy[k * 2 + 1];
        memcpy(v->rgba, c, 4);
    }
}

// a w x h box with its corners cut by c, inset by e from (x, y)
static void octagon_wh(Tris *t, float x, float y, float w, float h, float cut, float e, float dy, const uint8_t col[4]) {
    float x0 = x + e, y0 = y + e + dy, x1 = x + w - e, y1 = y + h - e + dy, c = cut - e * 0.4f;
    float p[16] = {x0 + c, y0, x1 - c, y0, x1, y0 + c, x1, y1 - c, x1 - c, y1, x0 + c, y1, x0, y1 - c, x0, y0 + c};
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    for (int i = 0; i < 8; i++) tri(t, cx, cy, p[i * 2], p[i * 2 + 1], p[(i + 1) % 8 * 2], p[(i + 1) % 8 * 2 + 1], col);
}

// the band between two of those insets (e0 outside e1): a glow that leaves the inside alone
static void band_wh(Tris *t, float x, float y, float w, float h, float cut, float e0, float e1, const uint8_t col[4]) {
    float p[2][16];
    for (int k = 0; k < 2; k++) {
        float e = k ? e1 : e0, x0 = x + e, y0 = y + e, x1 = x + w - e, y1 = y + h - e, c = cut - e * 0.4f;
        float q[16] = {x0 + c, y0, x1 - c, y0, x1, y0 + c, x1, y1 - c, x1 - c, y1, x0 + c, y1, x0, y1 - c, x0, y0 + c};
        memcpy(p[k], q, sizeof q);
    }
    for (int i = 0; i < 8; i++) {
        int j = (i + 1) % 8;
        tri(t, p[0][i * 2], p[0][i * 2 + 1], p[0][j * 2], p[0][j * 2 + 1], p[1][j * 2], p[1][j * 2 + 1], col);
        tri(t, p[0][i * 2], p[0][i * 2 + 1], p[1][j * 2], p[1][j * 2 + 1], p[1][i * 2], p[1][i * 2 + 1], col);
    }
}

static void octagon(Tris *t, float x, float y, float size, float cut, float e, float dy, const uint8_t col[4]) {
    octagon_wh(t, x, y, size, size, cut, e, dy, col);
}

static void disc(Tris *t, float cx, float cy, float r, const uint8_t col[4]) {
    for (int i = 0; i < 20; i++) {
        float a0 = i * 6.2831853f / 20, a1 = (i + 1) * 6.2831853f / 20;
        tri(t, cx, cy, cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, col);
    }
}

// a thick line with round ends
static void stroke(Tris *t, float x0, float y0, float x1, float y1, float w, const uint8_t col[4]) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l > 0) {
        float nx = -dy / l * w / 2, ny = dx / l * w / 2;
        tri(t, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, col);
        tri(t, x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, col);
    }
    disc(t, x0, y0, w / 2, col);
    disc(t, x1, y1, w / 2, col);
}

int cc_menu_power_button(AsMc *panel, Mat screen, CcVert *out, int max) {
    int lit = hl_item == CC_MENU_QUIT;
    float pr[4];
    if (!power_rect(panel, pr)) return 0;
    Mat m = mat_mul(screen, mat_mul(panel->parent ? panel->parent->m : (Mat){1, 0, 0, 1, 0, 0}, panel->m));
    float x = m.a * pr[0] + m.c * pr[1] + m.tx, y = m.b * pr[0] + m.d * pr[1] + m.ty;
    float size = m.a * (pr[2] - pr[0]), cut = size * 0.17f;
    Tris t = {out, 0, max};
    // colours read off the newer menu's tile: a red frame lit along the top, a dark well, and
    // in it a raised grey key (its top face, and its front below that), a faint ring on it
    static const uint8_t outline[4] = {64, 54, 52, 255}, red[4] = {190, 80, 79, 255}, red_dark[4] = {115, 64, 61, 255},
                         red_light[4] = {231, 129, 125, 255}, well[4] = {88, 90, 87, 255}, white[4] = {255, 255, 252, 255};
    uint8_t top[4] = {120, 120, 118, 255}, front[4] = {98, 97, 95, 255}, edge[4] = {132, 132, 130, 255},
            halo[4] = {134, 134, 132, 255};
    static const uint8_t haze[4] = {255, 244, 196, 70}, ring[4] = {255, 246, 206, 255};
    if (lit) {   // chosen: the face lights up, in the cream glow
        for (int k = 0; k < 3; k++) { top[k] += 30; front[k] += 24; edge[k] += 30; halo[k] += 30; }
        octagon(&t, x, y, size, cut, -size * 0.11f, 0, haze);
        octagon(&t, x, y, size, cut, -size * 0.06f, 0, ring);
    } else {   // the same glow round the chosen one of the others (drawn over them: a band)
        AsMc *b = panel_child(panel, MENU_DEPTH[hl_item]);
        float bb[4];
        if (b && (render_mc_bounds(b, b->m, bb), bb[2] > bb[0])) {
            float bx = m.a * bb[0] + m.c * bb[1] + m.tx, by = m.b * bb[0] + m.d * bb[1] + m.ty;
            float bw = m.a * (bb[2] - bb[0]), bh = m.d * (bb[3] - bb[1]);
            band_wh(&t, bx, by, bw, bh, bh * 0.17f, -size * 0.11f, -size * 0.06f, haze);
            band_wh(&t, bx, by, bw, bh, bh * 0.17f, -size * 0.06f, size * 0.01f, ring);
        }
    }
    octagon(&t, x, y, size, cut, 0, 0, outline);
    octagon(&t, x, y, size, cut, size * 0.045f, 0, red_dark);               // the frame: dark at the bottom,
    octagon(&t, x, y, size, cut, size * 0.045f, -size * 0.035f, red_light);  // lit along the top
    octagon(&t, x, y, size, cut, size * 0.07f, -size * 0.02f, red);
    octagon(&t, x, y, size, cut, size * 0.12f, 0, well);
    float kc = size * 0.1f;   // the key: its front (the side you see below it), then its top face
    octagon_wh(&t, x + size * 0.155f, y + size * 0.15f, size * 0.715f, size * 0.73f, kc, 0, 0, front);
    octagon_wh(&t, x + size * 0.155f, y + size * 0.15f, size * 0.715f, size * 0.63f, kc, 0, 0, edge);
    octagon_wh(&t, x + size * 0.155f, y + size * 0.15f, size * 0.715f, size * 0.63f, kc, size * 0.018f, 0, top);
    float cx = x + size * 0.512f, cy = y + size * 0.45f;
    for (int i = 0; i < 40; i++) {   // the faint ring round the sign
        float a0 = i * 6.2831853f / 40, a1 = (i + 1) * 6.2831853f / 40, r0 = size * 0.252f, r1 = size * 0.277f;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        tri(&t, cx + c0 * r0, cy + s0 * r0, cx + c0 * r1, cy + s0 * r1, cx + c1 * r1, cy + s1 * r1, halo);
        tri(&t, cx + c0 * r0, cy + s0 * r0, cx + c1 * r1, cy + s1 * r1, cx + c1 * r0, cy + s1 * r0, halo);
    }
    // the glint: a white stripe in the well's top-right corner
    float gx0 = x + size - size * 0.12f - cut + size * 0.03f, gy0 = y + size * 0.12f;
    float gx1 = x + size - size * 0.12f, gy1 = y + size * 0.12f + cut - size * 0.03f;
    float gw = size * 0.06f;
    tri(&t, gx0 - gw, gy0, gx0, gy0, gx1, gy1 + gw, white);
    tri(&t, gx0, gy0, gx1, gy1, gx1, gy1 + gw, white);
    // the sign: a ring open at the top and a bar, round-ended
    float r = size * 0.175f, w = size * 0.05f;
    for (int i = 0; i < 28; i++) {   // the ring as a band, round at its two ends
        float a0 = (40 + 280.0f * i / 28) * 3.14159265f / 180, a1 = (40 + 280.0f * (i + 1) / 28) * 3.14159265f / 180;
        float r0 = r - w / 2, r1 = r + w / 2, s0 = sinf(a0), c0 = cosf(a0), s1 = sinf(a1), c1 = cosf(a1);
        tri(&t, cx + s0 * r0, cy - c0 * r0, cx + s0 * r1, cy - c0 * r1, cx + s1 * r1, cy - c1 * r1, white);
        tri(&t, cx + s0 * r0, cy - c0 * r0, cx + s1 * r1, cy - c1 * r1, cx + s1 * r0, cy - c1 * r0, white);
    }
    for (int k = 0; k < 2; k++) {
        float a = (k ? 320 : 40) * 3.14159265f / 180;
        disc(&t, cx + sinf(a) * r, cy - cosf(a) * r, w / 2, white);
    }
    stroke(&t, cx, cy - r * 1.28f, cx, cy - r * 0.2f, w, white);
    return t.n;
}

int cc_menu_beta_place(AsMc *panel, Mat screen, Mat *out) {
    if (!CC_BETA_STICKER || !cc_menu_wifi) return 0;
    AsMc *b = panel_child(panel, MENU_DEPTH[CC_MENU_WIFI]);
    float bb[4];
    if (!b || !b->visible || (render_mc_bounds(b, b->m, bb), bb[2] <= bb[0])) return 0;
    Mat m = mat_mul(screen, mat_mul(panel->parent ? panel->parent->m : (Mat){1, 0, 0, 1, 0, 0}, panel->m));
    // stuck on at the button's top right corner, over its edge, tilted up a little
    float x = bb[2] - 47 * 20, y = bb[1] + 2 * 20, a = -8 * 3.14159265f / 180, k = m.a;
    *out = (Mat){cosf(a) * k, sinf(a) * k, -sinf(a) * k, cosf(a) * k, m.a * x + m.c * y + m.tx, m.b * x + m.d * y + m.ty};
    return 1;
}

void cc_native_root_functions(AsVm *vm, AsObj *root) {
    as_def_fn(vm, root, "f_GetButtonArt", native_get_button_art);
}

void cc_native_init(AsVm *vm) {
    as_override(vm, "f_GetDefaultWeapon", native_get_default_weapon);
    as_override(vm, "f_GetButtonArt", native_get_button_art);
    as_override(vm, "f_SpawnBear", native_spawn_bear);
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
