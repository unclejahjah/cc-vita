// Paint Jr. as an add-on: see paint_jr.h.
#include "paint_jr.h"

#include <stdio.h>
#include <string.h>

static int g_available;
void cc_paintjr_set_available(int on) { g_available = on; }
int cc_paintjr_p_type(void) { return g_available ? CC_PAINTJR_P_TYPE : 0; }

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

// f_SpawnPlayer(u_num, n_type, x, y): the player clip loader.game.game.p<u_num> runs PLAYER_NEW.swf
// while it's Paint Jr and PLAYER.swf otherwise (f_CreatePlayers loaded PLAYER.swf into it before
// anyone had picked). Its variables and frame stay: the swap happens before the spawn sets it up.
static AsVal spawn_player(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    int u = argc > 0 ? as_toint32(vm, argv[0]) : 0, t = argc > 1 ? as_toint32(vm, argv[1]) : 0;
    if (u >= 1 && u <= 4 && vm->loader) {
        char path[32];
        snprintf(path, sizeof path, "loader.game.game.p%d", u);
        int found;
        AsVal p = as_resolve_path(vm, vm->root, path, &found);
        if (found && p.t == V_OBJ && p.o->type == OBJ_MC) {
            AsMc *mc = (AsMc *)p.o;
            Movie *want = vm->loader(vm, t == CC_PAINTJR_P_TYPE ? "PLAYER_NEW.swf" : "PLAYER.swf");
            if (want && mc->movie && mc->movie != want && (t == CC_PAINTJR_P_TYPE || mc->movie->is_player == 0))
                as_mc_swap_movie(vm, mc, want);
        }
    }
    if (t == CC_PAINTJR_P_TYPE) graft(vm);
    return as_call_script(vm, this_, argc, argv);
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
    if (!g_available) return;
    as_override(vm, "f_SpawnPlayer", spawn_player);
    as_override(vm, "f_ShootMagic", shoot_magic);
    as_override(vm, "f_MagicBulletDown", magic_bullet_down);
}
