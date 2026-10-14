// AVM1 movie clips: live display lists, timelines, frame scripts, clip events,
// properties, paths and loadMovie.
//
// Depths: timeline depths d live at d - 16384 (as in Flash), so they sit below
// every depth a script uses (attachMovie, swapDepths).
//
// A clip's timeline is applied by diffing against the parser's precomputed
// display state for the target frame, matched by placement id, so going to any
// frame (forwards or back) works the same way.
#include "as.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TIMELINE_DEPTH(d) ((d) - 16384)

// clip event bits (PlaceObject2 clip actions, SWF6+)
enum { EV_LOAD = 0x01, EV_ENTERFRAME = 0x02, EV_UNLOAD = 0x04, EV_INIT = 0x200, EV_CONSTRUCT = 0x40000 };

double as_time_ms(void) {
    static struct timespec t0;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    if (!t0.tv_sec && !t0.tv_nsec) t0 = t;
    return (t.tv_sec - t0.tv_sec) * 1000.0 + (t.tv_nsec - t0.tv_nsec) / 1e6;
}

// ---- event queue ------------------------------------------------------------------

typedef struct {
    AsMc *mc;
    int kind;                  // Q_FRAME (scripts of mc->frame) or Q_EVENT (clip event bits in arg)
    int arg;
} QItem;

enum { Q_FRAME, Q_EVENT };

static QItem *q;
static int qn, qcap, qhead;

static void enqueue(AsMc *mc, int kind, int arg) {
    if (qn == qcap) {
        qcap = qcap ? qcap * 2 : 256;
        q = realloc(q, sizeof(QItem) * qcap);
    }
    q[qn++] = (QItem){mc, kind, arg};
}

void as_queue_frame(AsVm *vm, AsMc *mc) {
    (void)vm;
    enqueue(mc, Q_FRAME, mc->frame);
}

static void run_clip_events(AsVm *vm, AsMc *mc, int mask) {
    if (!mc->events_len || mc->removed) return;
    const uint8_t *d = mc->parent ? mc->parent->movie->data : mc->movie->data;
    const Movie *code = mc->parent ? mc->parent->movie : mc->movie;
    uint32_t p = mc->events_off, end = mc->events_off + mc->events_len;
    p += 2 + 4;   // reserved, all event flags
    while (p + 8 <= end) {
        uint32_t flags = d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24);
        if (!flags) break;
        uint32_t size = d[p + 4] | (d[p + 5] << 8) | (d[p + 6] << 16) | ((uint32_t)d[p + 7] << 24);
        uint32_t body = p + 8;
        if (flags & 0x20000) body++;   // key press: key code
        if (flags & mask) as_run_block(vm, code, body, size - (body - (p + 8)), mc);
        p += 8 + size;
    }
}

static void run_frame_scripts(AsVm *vm, AsMc *mc, int frame) {
    if (mc->removed || !mc->def) return;
    if (getenv("AS_DEBUG") && !mc->parent) as_warn(vm, "root frame %d scripts", frame + 1);
    for (int i = 0; i < mc->def->nactions; i++) {
        const ActionBlock *ab = &mc->def->actions[i];
        if (ab->frame == frame) as_run_block(vm, mc->movie, ab->off, ab->len, mc);
        if (mc->removed) return;
    }
}

static void drain_queue(AsVm *vm) {
    while (qhead < qn) {
        QItem it = q[qhead++];
        if (it.mc->removed) continue;
        if (it.kind == Q_FRAME) run_frame_scripts(vm, it.mc, it.arg);
        else run_clip_events(vm, it.mc, it.arg);
    }
    qhead = qn = 0;
}

// ---- display list -----------------------------------------------------------------

static int disp_find_depth(const AsMc *mc, int depth) {
    int lo = 0, hi = mc->ndisp - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (mc->disp[mid].depth == depth) return mid;
        if (mc->disp[mid].depth < depth) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static AsDisp *disp_insert(AsMc *mc, int depth) {
    int i = disp_find_depth(mc, depth);
    if (i >= 0) return &mc->disp[i];
    if (mc->ndisp == mc->capdisp) {
        mc->capdisp = mc->capdisp ? mc->capdisp * 2 : 8;
        mc->disp = realloc(mc->disp, sizeof(AsDisp) * mc->capdisp);
    }
    int at = 0;
    while (at < mc->ndisp && mc->disp[at].depth < depth) at++;
    memmove(&mc->disp[at + 1], &mc->disp[at], sizeof(AsDisp) * (mc->ndisp - at));
    mc->ndisp++;
    AsDisp *e = &mc->disp[at];
    memset(e, 0, sizeof *e);
    e->depth = depth;
    return e;
}

static void disp_remove_at(AsMc *mc, int i) {
    memmove(&mc->disp[i], &mc->disp[i + 1], sizeof(AsDisp) * (mc->ndisp - i - 1));
    mc->ndisp--;
}

static void decompose(AsMc *mc) {
    Mat *m = &mc->m;
    mc->xscale = sqrtf(m->a * m->a + m->b * m->b) * 100;
    mc->yscale = sqrtf(m->c * m->c + m->d * m->d) * 100;
    if (m->a * m->d - m->b * m->c < 0) mc->yscale = -mc->yscale;   // a flip shows as negative _yscale
    mc->rotation = atan2f(m->b, m->a) * 180.0f / 3.14159265f;
}

static void recompose(AsMc *mc) {
    float r = mc->rotation * 3.14159265f / 180.0f, cs = cosf(r), sn = sinf(r);
    float xs = mc->xscale / 100, ys = mc->yscale / 100;
    mc->m.a = xs * cs;
    mc->m.b = xs * sn;
    mc->m.c = -ys * sn;
    mc->m.d = ys * cs;
}

static void mc_unload(AsVm *vm, AsMc *mc);

AsMc *as_mc_new(AsVm *vm, const Movie *m, const Sprite *def, AsMc *parent, int depth) {
    AsMc *mc = (AsMc *)as_obj_new(vm, OBJ_MC, vm->mc_proto);
    mc->movie = m;
    mc->def = def;
    mc->parent = parent;
    mc->depth = depth;
    mc->playing = 1;
    mc->visible = 1;
    mc->alpha = 100;
    mc->m = MAT_IDENTITY;
    mc->cx = CX_IDENTITY;
    mc->xscale = mc->yscale = 100;
    mc->frame = -1;
    mc->born = vm->tick;
    return mc;
}

// Puts a timeline placement (or a script-made clip) at its depth.
static AsMc *place(AsVm *vm, AsMc *parent, const Place *p, int depth) {
    const Character *c = &parent->movie->chars[p->cid];
    AsDisp *e = disp_insert(parent, depth);
    e->cid = p->cid;
    e->clip = p->clip;
    e->uid = p->uid;
    e->m = p->m;
    e->cx = p->cx;
    e->mc = NULL;
    if (c->type == CHAR_SHAPE) { e->kind = DISP_SHAPE; return NULL; }
    if (c->type == CHAR_EDITTEXT) {
        // a text field: a clip-like object (name, _x, _visible...) whose "text" holds its contents
        e->kind = DISP_TEXT;
        AsMc *tf = as_mc_new(vm, parent->movie, NULL, parent, depth);
        tf->text = c->ptr;
        tf->m = p->m;
        tf->cx = p->cx;
        decompose(tf);
        tf->uid = p->uid;
        tf->name = p->name ? as_atom(vm, parent->movie->names[p->name]) : NULL;
        tf->frame = 0;
        tf->playing = 0;
        as_set(vm, as_objv(&tf->obj), as_atom(vm, "text"), as_strv(as_str(vm, tf->text->text)));
        e->mc = tf;
        return tf;
    }
    if (c->type != CHAR_SPRITE) { e->kind = DISP_TEXT; return NULL; }
    e->kind = DISP_MC;
    AsMc *mc = as_mc_new(vm, parent->movie, c->ptr, parent, depth);
    mc->m = p->m;
    mc->cx = p->cx;
    decompose(mc);
    mc->uid = p->uid;
    mc->clip = p->clip;
    mc->name = p->name ? as_atom(vm, parent->movie->names[p->name]) : NULL;
    mc->events_off = p->events_off;
    mc->events_len = p->events_len;
    e->mc = mc;
    // first frame, then onClipEvent(load)
    as_mc_goto(vm, mc, 0, 1);
    if (mc->events_len) {
        run_clip_events(vm, mc, EV_INIT | EV_CONSTRUCT);
        enqueue(mc, Q_EVENT, EV_LOAD);
    }
    return mc;
}

static void apply_frame(AsVm *vm, AsMc *mc, int frame) {
    const Frame *fr = &mc->def->frames[frame];
    // drop or update what the timeline placed
    for (int i = mc->ndisp - 1; i >= 0; i--) {
        AsDisp *e = &mc->disp[i];
        if (!e->uid) continue;
        const Place *keep = NULL;
        for (int k = 0; k < fr->count; k++)
            if (fr->places[k].uid == e->uid) { keep = &fr->places[k]; break; }
        if (!keep) {
            if (e->depth >= 0) continue;   // moved by swapDepths: the script owns it now
            AsMc *child = e->mc;
            disp_remove_at(mc, i);
            if (child) mc_unload(vm, child);
            continue;
        }
        if (e->depth != TIMELINE_DEPTH(keep->depth)) continue;
        e->m = keep->m;
        e->cx = keep->cx;
        if (e->mc && !e->mc->scripted) {
            e->mc->m = keep->m;
            e->mc->cx = keep->cx;
            decompose(e->mc);
        }
    }
    // add new placements
    for (int k = 0; k < fr->count; k++) {
        const Place *p = &fr->places[k];
        int present = 0;
        for (int i = 0; i < mc->ndisp && !present; i++) present = mc->disp[i].uid == p->uid;
        if (!present) place(vm, mc, p, TIMELINE_DEPTH(p->depth));
    }
}

void as_mc_goto(AsVm *vm, AsMc *mc, int frame, int play) {
    if (!mc->def || !mc->def->nframes || mc->removed) return;
    if (frame < 0) frame = 0;
    if (frame >= mc->def->nframes) frame = mc->def->nframes - 1;
    mc->playing = play;
    if (frame == mc->frame) return;
    mc->frame = frame;
    // a timeline's frame script runs before the scripts of the children it places
    enqueue(mc, Q_FRAME, frame);
    apply_frame(vm, mc, frame);
}

int as_mc_frame_of(AsMc *mc, AsVal label) {
    if (!mc->def) return -1;
    if (label.t == V_NUM) return (int)label.n - 1;
    if (label.t != V_STR) return -1;
    const char *s = label.s->s;
    char *end;
    long n = strtol(s, &end, 10);
    if (*s && !*end) return (int)n - 1;
    for (int i = 0; i < mc->def->nlabels; i++)
        if (!strcasecmp(mc->def->labels[i].name, s)) return mc->def->labels[i].frame;
    return -1;
}

void as_mc_goto_value(AsVm *vm, AsMc *mc, AsVal frame, int play) {
    // "clip:frame" paths are allowed in a frame string
    if (frame.t == V_STR) {
        const char *colon = strrchr(frame.s->s, ':');
        if (colon) {
            char path[256];
            size_t n = (size_t)(colon - frame.s->s);
            if (n >= sizeof path) n = sizeof path - 1;
            memcpy(path, frame.s->s, n);
            path[n] = 0;
            int found;
            AsVal t = as_resolve_path(vm, mc, path, &found);
            if (!found || t.t != V_OBJ || t.o->type != OBJ_MC) return;
            mc = (AsMc *)t.o;
            frame = as_strv(as_str(vm, colon + 1));
        }
    }
    int f = as_mc_frame_of(mc, frame);
    if (f < 0) return;
    as_mc_goto(vm, mc, f, play);
}

static void mc_unload(AsVm *vm, AsMc *mc) {
    if (mc->removed) return;
    run_clip_events(vm, mc, EV_UNLOAD);
    AsVal f = as_get(vm, as_objv(&mc->obj), as_atom(vm, "onUnload"));
    if (f.t == V_OBJ) as_call(vm, f, as_objv(&mc->obj), 0, NULL);
    mc->removed = 1;
    for (int i = 0; i < mc->ndisp; i++)
        if (mc->disp[i].mc) mc_unload(vm, mc->disp[i].mc);
}

void as_mc_remove(AsVm *vm, AsMc *mc) {
    AsMc *p = mc->parent;
    if (!p) return;
    for (int i = 0; i < p->ndisp; i++)
        if (p->disp[i].mc == mc) { disp_remove_at(p, i); break; }
    mc_unload(vm, mc);
}

AsMc *as_mc_child(AsMc *mc, AsStr *atom) {
    for (int i = 0; i < mc->ndisp; i++)
        if (mc->disp[i].mc && mc->disp[i].mc->name == atom) return mc->disp[i].mc;
    return NULL;
}

static int find_export(const Movie *m, AsStr *linkage) {
    for (int i = 0; i < 65536; i++)
        if (m->chars[i].export && !strcasecmp(m->names[m->chars[i].export], linkage->s)) return i;
    return -1;
}

AsMc *as_mc_attach(AsVm *vm, AsMc *mc, AsStr *linkage, AsStr *name, int depth) {
    int cid = find_export(mc->movie, linkage);
    if (cid < 0 || mc->movie->chars[cid].type != CHAR_SPRITE) {
        as_warn(vm, "attachMovie: no symbol '%s'", linkage->s);
        return NULL;
    }
    int at = disp_find_depth(mc, depth);
    if (at >= 0) {   // replaces what was there
        AsMc *old = mc->disp[at].mc;
        disp_remove_at(mc, at);
        if (old) mc_unload(vm, old);
    }
    Place p;
    memset(&p, 0, sizeof p);
    p.cid = cid;
    p.m = MAT_IDENTITY;
    p.cx = CX_IDENTITY;
    AsMc *child = place(vm, mc, &p, depth);
    if (child) child->name = name ? as_intern(vm, name) : NULL;
    return child;
}

AsMc *as_mc_create_empty(AsVm *vm, AsMc *parent, AsStr *name, int depth) {
    int at = disp_find_depth(parent, depth);
    if (at >= 0) {
        AsMc *old = parent->disp[at].mc;
        disp_remove_at(parent, at);
        if (old) mc_unload(vm, old);
    }
    AsDisp *e = disp_insert(parent, depth);
    e->kind = DISP_MC;
    AsMc *mc = as_mc_new(vm, parent->movie, NULL, parent, depth);
    mc->name = as_intern(vm, name);
    mc->frame = 0;
    e->mc = mc;
    return mc;
}

AsMc *as_mc_duplicate(AsVm *vm, AsMc *mc, AsStr *name, int depth) {
    AsMc *p = mc->parent;
    if (!p || !mc->def) return NULL;
    int at = disp_find_depth(p, depth);
    if (at >= 0) {
        AsMc *old = p->disp[at].mc;
        disp_remove_at(p, at);
        if (old) mc_unload(vm, old);
    }
    AsDisp *e = disp_insert(p, depth);
    e->kind = DISP_MC;
    AsMc *c = as_mc_new(vm, mc->movie, mc->def, p, depth);
    c->m = mc->m;
    c->cx = mc->cx;
    c->alpha = mc->alpha;
    c->xscale = mc->xscale;
    c->yscale = mc->yscale;
    c->rotation = mc->rotation;
    c->scripted = 1;
    c->events_off = mc->events_off;
    c->events_len = mc->events_len;
    c->name = as_intern(vm, name);
    e->mc = c;
    as_mc_goto(vm, c, 0, 1);
    if (c->events_len) enqueue(c, Q_EVENT, EV_LOAD);
    return c;
}

void as_mc_swap_depths(AsVm *vm, AsMc *mc, AsVal target) {
    AsMc *p = mc->parent;
    if (!p) return;
    int to;
    if (target.t == V_OBJ && target.o->type == OBJ_MC) {
        AsMc *o = (AsMc *)target.o;
        if (o->parent != p) return;
        to = o->depth;
    } else {
        double n = as_tonum(vm, target);
        if (isnan(n)) return;
        to = (int)n;
    }
    int from = mc->depth;
    if (to == from) return;
    int i = disp_find_depth(p, from);
    if (i < 0) return;
    AsDisp mine = p->disp[i];
    disp_remove_at(p, i);
    int j = disp_find_depth(p, to);
    if (j >= 0) {   // whatever was there takes our old depth
        AsDisp other = p->disp[j];
        disp_remove_at(p, j);
        other.depth = from;
        if (other.mc) other.mc->depth = from;
        *disp_insert(p, from) = other;
    }
    mine.depth = to;
    mc->depth = to;
    *disp_insert(p, to) = mine;
}

Mat as_mc_global_matrix(AsMc *mc) {
    Mat m = MAT_IDENTITY;
    for (AsMc *c = mc; c; c = c->parent) m = mat_mul(c->m, m);
    return m;
}

void as_mc_load_movie(AsVm *vm, AsMc *mc, Movie *m) {
    for (int i = 0; i < mc->ndisp; i++)
        if (mc->disp[i].mc) mc_unload(vm, mc->disp[i].mc);
    mc->ndisp = 0;
    mc->movie = m;
    mc->def = &m->root;
    mc->frame = -1;
    mc->playing = 1;
    // a loaded movie's variables start fresh
    free(mc->obj.props.slots);
    memset(&mc->obj.props, 0, sizeof mc->obj.props);
    for (int i = 0; i < m->ninit; i++) as_run_block(vm, m, m->init[i].off, m->init[i].len, mc);
    as_mc_goto(vm, mc, 0, 1);
}

// ---- paths ------------------------------------------------------------------------

AsStr *as_mc_target_path(AsVm *vm, AsMc *mc) {
    char buf[1024];
    char *parts[64];
    int n = 0;
    for (AsMc *m = mc; m && m->parent && n < 64; m = m->parent) parts[n++] = m->name ? m->name->s : "";
    size_t len = 0;
    len += snprintf(buf + len, sizeof buf - len, "_level0");
    for (int i = n - 1; i >= 0 && len < sizeof buf; i--) len += snprintf(buf + len, sizeof buf - len, ".%s", parts[i]);
    return as_str(vm, buf);
}

AsVal as_resolve_path(AsVm *vm, AsMc *from, const char *path, int *found) {
    *found = 0;
    AsVal cur = as_objv(&from->obj);
    const char *s = path;
    if (*s == '/') { cur = as_objv(&vm->root->obj); s++; }
    while (*s) {
        char seg[128];
        int n = 0;
        while (*s && *s != '.' && *s != '/' && *s != ':' && n < 127) seg[n++] = *s++;
        seg[n] = 0;
        if (*s) s++;
        if (!n) continue;
        if (!strcmp(seg, "..")) {
            AsMc *m = cur.t == V_OBJ && cur.o->type == OBJ_MC ? (AsMc *)cur.o : NULL;
            if (!m || !m->parent) return as_undef();
            cur = as_objv(&m->parent->obj);
        } else if (!strcasecmp(seg, "_root") || !strcasecmp(seg, "_level0")) {
            cur = as_objv(&vm->root->obj);
        } else if (!strcasecmp(seg, "_global")) {
            cur = as_objv(vm->global);
        } else if (!strcasecmp(seg, "this")) {
            // stays
        } else {
            cur = as_get(vm, cur, as_atom(vm, seg));
            if (cur.t == V_UNDEF) return cur;
        }
    }
    *found = 1;
    return cur;
}

AsMc *as_mc_resolve_target(AsVm *vm, AsMc *base, AsVal t) {
    if (t.t == V_OBJ && t.o->type == OBJ_MC) return (AsMc *)t.o;
    if (t.t == V_UNDEF || t.t == V_NULL) return NULL;   // a missing clip is no target (not "this one")
    if (!base) base = vm->root;
    AsStr *s = as_tostr(vm, t);
    if (!s->len) return base;
    int found;
    AsVal v = as_resolve_path(vm, base, s->s, &found);
    return found && v.t == V_OBJ && v.o->type == OBJ_MC ? (AsMc *)v.o : NULL;
}

void as_mc_get_url(AsVm *vm, AsMc *base, AsVal url, AsVal target, int flags) {
    (void)flags;
    AsMc *t = as_mc_resolve_target(vm, base, target);
    AsStr *u = as_tostr(vm, url);
    if (!t) {
        AsStr *ts = as_tostr(vm, target);
        if (!strncasecmp(u->s, "FSCommand:", 10)) { as_warn(vm, "fscommand %s %s", u->s + 10, ts->s); return; }
        as_warn(vm, "getURL '%s' into unknown target '%s'", u->s, ts->s);
        return;
    }
    AsLoad *l = calloc(1, sizeof *l);
    l->target = t;
    snprintf(l->url, sizeof l->url, "%s", u->s);
    AsLoad **tail = &vm->loads;
    while (*tail) tail = &(*tail)->next;
    *tail = l;
}

static void do_loads(AsVm *vm) {
    while (vm->loads) {
        AsLoad *l = vm->loads;
        vm->loads = l->next;
        if (getenv("AS_LOADS"))
            as_warn(vm, "load '%s' into %s%s", l->url, as_mc_target_path(vm, l->target)->s, l->target->removed ? " (removed)" : "");
        if (!l->target->removed) {
            if (!l->url[0]) {   // unloadMovie
                for (int i = 0; i < l->target->ndisp; i++)
                    if (l->target->disp[i].mc) mc_unload(vm, l->target->disp[i].mc);
                l->target->ndisp = 0;
                l->target->def = NULL;
            } else {
                Movie *m = vm->loader ? vm->loader(vm, l->url) : NULL;
                if (m) as_mc_load_movie(vm, l->target, m);
                else as_warn(vm, "loadMovie: can't load %s", l->url);
                drain_queue(vm);
                if (m && vm->loaded && !l->target->removed) vm->loaded(vm, l->target);
            }
        }
        free(l);
        drain_queue(vm);
    }
}

// ---- properties -------------------------------------------------------------------

static const char *PROP_NAMES[] = {"_x", "_y", "_xscale", "_yscale", "_currentframe", "_totalframes", "_alpha",
                                   "_visible", "_width", "_height", "_rotation", "_target", "_framesloaded",
                                   "_name", "_droptarget", "_url", "_highquality", "_focusrect", "_soundbuftime",
                                   "_quality", "_xmouse", "_ymouse"};
#define NPROPS (int)(sizeof PROP_NAMES / sizeof *PROP_NAMES)

int as_mc_prop_index(const char *name) {
    if (name[0] != '_') return -1;
    for (int i = 0; i < NPROPS; i++)
        if (!strcasecmp(name, PROP_NAMES[i])) return i;
    return -1;
}

extern void render_mc_bounds(AsMc *mc, Mat m, float out[4]);

static void bounds_in_parent(AsMc *mc, float b[4]) {
    render_mc_bounds(mc, mc->m, b);
}

AsVal as_mc_get_prop(AsVm *vm, AsMc *mc, int i) {
    switch (i) {
    case 0: return as_num(mc->m.tx / 20.0);
    case 1: return as_num(mc->m.ty / 20.0);
    case 2: return as_num(mc->xscale);
    case 3: return as_num(mc->yscale);
    case 4: return as_num(mc->frame + 1);
    case 5: case 12: return as_num(mc->def ? mc->def->nframes : 0);
    case 6: return as_num(mc->alpha);
    case 7: return as_bool(mc->visible);
    case 8: case 9: {
        float b[4];
        bounds_in_parent(mc, b);
        return as_num(b[2] > b[0] ? (i == 8 ? b[2] - b[0] : b[3] - b[1]) / 20.0 : 0);
    }
    case 10: return as_num(mc->rotation);
    case 11: {   // _target: "/a/b"
        AsStr *p = as_mc_target_path(vm, mc);
        char buf[1024];
        snprintf(buf, sizeof buf, "%s", p->s + 7);   // drop _level0
        for (char *c = buf; *c; c++) if (*c == '.') *c = '/';
        return as_strv(as_str(vm, buf[0] ? buf : "/"));
    }
    case 13: return mc->name ? as_strv(mc->name) : as_strv(vm->empty);
    case 15: return as_strv(as_atom(vm, "file:///castlecrashers.swf"));
    case 16: return as_num(1);
    case 19: return as_strv(as_atom(vm, "HIGH"));
    default: return as_num(0);
    }
}

void as_mc_set_prop(AsVm *vm, AsMc *mc, int i, AsVal v) {
    double n = as_tonum(vm, v);
    if (isnan(n) && i != 7 && i != 13) return;
    switch (i) {
    case 0: mc->m.tx = (float)(n * 20); mc->scripted = 1; break;
    case 1: mc->m.ty = (float)(n * 20); mc->scripted = 1; break;
    case 2: mc->xscale = (float)n; recompose(mc); mc->scripted = 1; break;
    case 3: mc->yscale = (float)n; recompose(mc); mc->scripted = 1; break;
    case 6: mc->alpha = (float)n; break;
    case 7: mc->visible = as_tobool(vm, v); break;
    case 8: case 9: {
        float b[4];
        Mat unscaled = mc->m;
        bounds_in_parent(mc, b);
        float cur = (i == 8 ? b[2] - b[0] : b[3] - b[1]) / 20.0f;
        (void)unscaled;
        if (cur > 0) {
            if (i == 8) mc->xscale *= (float)n / cur;
            else mc->yscale *= (float)n / cur;
            recompose(mc);
            mc->scripted = 1;
        }
        break;
    }
    case 10: mc->rotation = (float)n; recompose(mc); mc->scripted = 1; break;
    case 13: mc->name = as_intern(vm, as_tostr(vm, v)); break;
    default: break;
    }
}

int as_mc_get_member(AsVm *vm, AsMc *mc, AsStr *atom, AsVal *out) {
    const char *s = atom->s;
    if (s[0] == '_') {
        int i = as_mc_prop_index(s);
        if (i >= 0) { *out = as_mc_get_prop(vm, mc, i); return 1; }
        if (!strcasecmp(s, "_parent")) { *out = mc->parent ? as_objv(&mc->parent->obj) : as_undef(); return 1; }
        if (!strcasecmp(s, "_root") || !strcasecmp(s, "_level0")) { *out = as_objv(&vm->root->obj); return 1; }
        if (!strcasecmp(s, "_global")) { *out = as_objv(vm->global); return 1; }
    }
    if (as_own(&mc->obj, atom)) return 0;   // a variable of that name wins
    AsMc *child = as_mc_child(mc, atom);
    if (child) { *out = as_objv(&child->obj); return 1; }
    return 0;
}

int as_mc_set_member(AsVm *vm, AsMc *mc, AsStr *atom, AsVal v) {
    if (atom->s[0] != '_') return 0;
    int i = as_mc_prop_index(atom->s);
    if (i < 0) return 0;
    as_mc_set_prop(vm, mc, i, v);
    return 1;
}

// ---- frames -----------------------------------------------------------------------

static void advance(AsVm *vm, AsMc *mc) {
    if (mc->removed) return;
    // a clip placed during this pass shows its first frame (and runs its stop()) before moving on
    if (mc->playing && mc->def && mc->def->nframes > 1 && mc->born != vm->tick) {
        int next = mc->frame + 1 >= mc->def->nframes ? 0 : mc->frame + 1;
        mc->frame = next;
        enqueue(mc, Q_FRAME, next);
        apply_frame(vm, mc, next);
    }
    for (int i = 0; i < mc->ndisp; i++)
        if (mc->disp[i].mc) advance(vm, mc->disp[i].mc);
}

static AsStr *A_onEnterFrame;

static void enter_frame(AsVm *vm, AsMc *mc) {
    if (mc->removed) return;
    if (mc->events_len) enqueue(mc, Q_EVENT, EV_ENTERFRAME);
    AsProp *p = as_own(&mc->obj, A_onEnterFrame);
    if (p && p->v.t == V_OBJ) as_call(vm, p->v, as_objv(&mc->obj), 0, NULL);
    // children may be removed while we walk; copy the list (a level's game clip has thousands)
    AsMc *small[64], **kids = mc->ndisp <= 64 ? small : malloc(sizeof(AsMc *) * mc->ndisp);
    int k = 0;
    for (int i = 0; i < mc->ndisp; i++)
        if (mc->disp[i].mc) kids[k++] = mc->disp[i].mc;
    for (int i = 0; i < k; i++) enter_frame(vm, kids[i]);
    if (kids != small) free(kids);
}

void as_tick(AsVm *vm) {
    if (!A_onEnterFrame) A_onEnterFrame = as_atom(vm, "onEnterFrame");
    if (!vm->root) return;
    vm->tick++;
    if (vm->root->frame < 0) {   // first frame of the root movie
        const Movie *m = vm->root->movie;
        for (int i = 0; i < m->ninit; i++) as_run_block(vm, m, m->init[i].off, m->init[i].len, vm->root);
        as_mc_goto(vm, vm->root, 0, 1);
    } else {
        advance(vm, vm->root);
    }
    drain_queue(vm);
    enter_frame(vm, vm->root);
    drain_queue(vm);
    do_loads(vm);
}

void as_mc_mark(AsVm *vm, AsMc *mc) {
    if (mc->parent) as_mark_obj(vm, &mc->parent->obj);
    for (int i = 0; i < mc->ndisp; i++)
        if (mc->disp[i].mc) as_mark_obj(vm, &mc->disp[i].mc->obj);
}
