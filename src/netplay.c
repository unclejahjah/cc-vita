// The game's side of Wi-Fi co-op. See netplay.h.
#include "netplay.h"
#include "cc_host.h"
#include "as_render.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>

static uint32_t g_clock_tick, g_clock_calls;   // getTimer(): the frame's time, and a step a call
static uint8_t g_req, g_done;                  // level files asked for / ready on this Vita
static uint8_t g_loads_min;                    // ready on every Vita (this frame)
static char g_url[128];                        // the file being waited for ("" none)
#define CHECKS 64
static uint16_t g_checks[CHECKS];              // netplay_check after each frame lately

// 30 frames a second, and a millisecond more on each call in the same frame (so a script that
// waits for the clock to move still gets there)
static double net_time(AsVm *vm) {
    if (vm->tick != g_clock_tick) { g_clock_tick = vm->tick; g_clock_calls = 0; }
    return vm->tick * (1000.0 / 30) + g_clock_calls++;
}

void netplay_begin(AsVm *vm) {
    vm->rand_state = net.seed;
    vm->time_ms = net_time;
    cc_net_port = net.port;
    memcpy(cc_net_names, net.names, sizeof cc_net_names);
    cc_storage_clear();
    for (int p = 1; p <= NET_PLAYERS; p++) {
        int len;
        const uint8_t *pr = net_profile(p, &len);
        if (pr) cc_storage_unpack(p - 1, pr, len);
    }
    cc_own_dlc_characters();
    cc_own_dlc_items();
    g_req = g_done = g_loads_min = 0;
    g_url[0] = 0;
    g_clock_tick = g_clock_calls = 0;
    memset(g_checks, 0, sizeof g_checks);
}

void netplay_end(void) {
    cc_net_port = 0;
    memset(cc_net_names, 0, sizeof cc_net_names);
}

int netplay_step(AsVm *vm, uint32_t t, NetInput *mine, int no_start) {
    const NetFrame *f = net_frame(t);
    if (!f) return 0;
    mine->loads = g_done;
    // the state after frame t - 1, and the check that goes with these buttons: every Vita's for
    // frame k is of the state after k - slowest - 1 (the host's buttons go out sooner than the
    // others', so it sends an older one)
    if (t > 0) g_checks[(t - 1) % CHECKS] = netplay_check(vm);
    int k = (int)(t + net.delay) - net.slowest - 1;
    mine->check = k >= 0 && (int)t - 1 - k < CHECKS ? g_checks[k % CHECKS] : 0;
    net_submit(t + net.delay, mine);
    memset(cc_keys, 0, sizeof cc_keys);
    int first = 1;
    for (int p = 0; p < NET_PLAYERS; p++) {
        const NetInput *in = &f->in[p];
        if (in->state) {
            char name[32];
            snprintf(name, sizeof name, "g_nPort%dState", p + 1);
            cc_set_flash_global(vm, name, as_num(in->state - 1));
        }
        if (!(f->active >> p & 1)) continue;
        uint32_t b = in->buttons;
        if (no_start) b &= ~(1u << CCB_START);
        cc_port_keys(p + 1, b);
        if (first || (int8_t)(in->loads - g_loads_min) < 0) g_loads_min = in->loads;
        first = 0;
    }
    return 1;
}

int netplay_prefetch(AsVm *vm, const char *url) {
    (void)vm;
    const char *base = strrchr(url, '/');
    base = base ? base + 1 : url;
    if (strcasecmp(g_url, base)) {   // a new file: everyone starts on it now
        snprintf(g_url, sizeof g_url, "%s", base);
        g_req++;
    }
    if ((int8_t)(g_loads_min - g_req) < 0) return 1;   // someone hasn't got it yet
    g_url[0] = 0;
    return 0;
}

const char *netplay_file_wanted(void) {
    return g_url[0] && g_done != g_req ? g_url : NULL;
}

void netplay_file_ready(void) { g_done = g_req; }

static uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

uint16_t netplay_check(AsVm *vm) {
    uint32_t h = mix(2166136261u, vm->rand_state);
    h = mix(h, vm->tick);
    static const char *const fields[] = {"x", "y", "health"};
    for (int p = 1; p <= NET_PLAYERS; p++) {
        char path[48];
        snprintf(path, sizeof path, "loader.game.game.p%d", p);
        int found;
        AsVal pl = as_resolve_path(vm, vm->root, path, &found);
        if (!found || pl.t != V_OBJ) continue;
        for (int k = 0; k < 3; k++) {
            AsVal v = as_get(vm, pl, as_atom(vm, fields[k]));
            h = mix(h, v.t == V_NUM ? (uint32_t)(int32_t)(v.n * 16) : v.t);
        }
    }
    uint16_t c = h ^ h >> 16;
    return c ? c : 1;
}

// The knight's clip has its feet at 0,0; the tag sits this far above them (twips, the clip's own
// units, so it follows the camera's zoom), clear of the helmet.
#define TAG_HEAD_TWIPS (-1750)

void netplay_nametags(AsVm *vm, Mat screen, DrawList *out) {
    for (int p = 1; p <= NET_PLAYERS; p++) {
        const char *name = net.names[p - 1];
        if (!name[0]) continue;
        char path[48];
        snprintf(path, sizeof path, "loader.game.game.p%d", p);
        int found;
        AsVal pl = as_resolve_path(vm, vm->root, path, &found);
        if (!found || pl.t != V_OBJ || pl.o->type != OBJ_MC) continue;
        AsMc *mc = (AsMc *)pl.o;
        int shown = !mc->removed;
        for (AsMc *c = mc; c && shown; c = c->parent) shown = c->visible && c->alpha > 0;
        if (!shown) continue;
        Mat g = mat_mul(screen, as_mc_global_matrix(mc));
        float x = g.c * TAG_HEAD_TWIPS + g.tx, y = g.d * TAG_HEAD_TWIPS + g.ty;
        if (x < -100 || x > 1060 || y < -40 || y > 600) continue;   // off screen
        CXform cx = {{1, 1, 1, 1}, {0, 0, 0, 0}};
        as_render_host_text_outlined(name, roundf(x), roundf(y), 1.1f, 1.1f, 2, 2, (Mat){1, 0, 0, 1, 0, 0}, cx, out);
    }
}

// the HUD's name over each portrait (_root.hud<n>.stats.textstuff.gamertag and its shadow "bg"):
// the game leaves "gamertag" there for the executable to fill
void netplay_hud_names(AsVm *vm) {
    static const char *const fields[] = {"gamertag", "gamertagbg"};
    AsStr *text = as_atom(vm, "text");
    for (int h = 1; h <= NET_PLAYERS; h++) {
        char name[8];
        snprintf(name, sizeof name, "hud%d", h);
        AsMc *hud = as_mc_child(vm->root, as_atom(vm, name));
        if (!hud) continue;
        int port = as_toint32(vm, as_get(vm, as_objv(&hud->obj), as_atom(vm, "port")));
        if (port < 1 || port > NET_PLAYERS || !net.names[port - 1][0]) continue;
        AsMc *stats = as_mc_child(hud, as_atom(vm, "stats"));
        AsMc *box = stats ? as_mc_child(stats, as_atom(vm, "textstuff")) : NULL;
        for (int f = 0; box && f < 2; f++) {
            AsMc *tf = as_mc_child(box, as_atom(vm, fields[f]));
            if (!tf) continue;
            AsVal tv = as_objv(&tf->obj), now = as_get(vm, tv, text);
            if (now.t == V_STR && !strcmp(now.s->s, net.names[port - 1])) continue;
            as_set(vm, tv, text, as_strv(as_str(vm, net.names[port - 1])));
        }
    }
}
