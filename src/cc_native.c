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
// Character data: 64 + 48 * (p_type - 1): level - 1, 4 exp bytes, weapon, animal,
// strength, defense, magic, agility.
static void seed_new_character(AsVm *vm, AsVal hud, int p_type, int weapon) {
    double *s = cc_storage(as_toint32(vm, as_get(vm, hud, as_atom(vm, "port"))) - 1);
    if (!s) return;
    double *c = s + 64 + 48 * (p_type - 1);
    for (int i = 0; i <= 10; i++)
        if (i != 6 && c[i] != 0) return;   // played before (the animal can be set alone)
    c[5] = weapon;
    c[7] = c[8] = c[9] = c[10] = 1;
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

void cc_native_init(AsVm *vm) {
    as_override(vm, "f_GetDefaultWeapon", native_get_default_weapon);
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
