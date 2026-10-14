// AVM1 core: strings, atoms, objects, conversions, calls, the interpreter and GC.
#include "as.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GC_STR, GC_OBJ, GC_SCOPE };

#define DELETED ((AsStr *)1)

// ---- heap -------------------------------------------------------------------------

static void *gc_alloc(AsVm *vm, size_t size, int kind) {
    AsGc *g = calloc(1, size);
    g->kind = kind;
    g->next = vm->heap;
    vm->heap = g;
    vm->heap_count++;
    return g;
}

// A call's activation object and scope live only as long as the call unless a function
// defined inside it keeps them (a closure). They're taken from these free lists, outside
// the collected heap, and given to the heap only when captured. (Collection never runs
// while a script does, so nothing else can be looking at them.)
static AsGc *g_free_acts, *g_free_scopes;

static void *call_alloc(AsGc **pool, size_t size, int kind) {
    AsGc *g = *pool;
    if (g) { *pool = g->next; memset(g, 0, size); }
    else g = calloc(1, size);
    g->kind = kind;
    return g;
}

static void call_keep(AsVm *vm, AsGc *g) {   // captured: from now on the collector owns it
    g->next = vm->heap;
    vm->heap = g;
    vm->heap_count++;
}

static void call_release(AsGc **pool, AsGc *g) {
    if (g->kind == GC_OBJ) free(((AsObj *)g)->props.slots);
    g->next = *pool;
    *pool = g;
}

void as_warn(AsVm *vm, const char *fmt, ...) {
    if (!vm->warn) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    vm->warn(vm, buf);
}

// ---- strings and atoms ------------------------------------------------------------

static uint32_t name_hash(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)tolower((unsigned char)s[i])) * 16777619u;
    return h;
}

AsStr *as_strn(AsVm *vm, const char *s, size_t n) {
    AsStr *str = gc_alloc(vm, sizeof(AsStr) + n + 1, GC_STR);
    str->len = (uint32_t)n;
    memcpy(str->s, s, n);
    str->s[n] = 0;
    str->hash = name_hash(s, n);
    return str;
}

AsStr *as_str(AsVm *vm, const char *s) { return as_strn(vm, s, strlen(s)); }

// Atom table: open addressing on the case-insensitive hash.
static AsStr **atoms;
static int atom_cap, atom_count;

static int same_name(const AsStr *a, const char *s, size_t n) {
    if (a->len != n) return 0;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a->s[i]) != tolower((unsigned char)s[i])) return 0;
    return 1;
}

static AsStr *atom_lookup(AsVm *vm, const char *s, size_t n, uint32_t h) {
    if (atom_count * 2 >= atom_cap) {
        int ncap = atom_cap ? atom_cap * 2 : 4096;
        AsStr **na = calloc(ncap, sizeof(AsStr *));
        for (int i = 0; i < atom_cap; i++)
            if (atoms[i]) {
                uint32_t j = atoms[i]->hash & (ncap - 1);
                while (na[j]) j = (j + 1) & (ncap - 1);
                na[j] = atoms[i];
            }
        free(atoms);
        atoms = na;
        atom_cap = ncap;
    }
    uint32_t j = h & (atom_cap - 1);
    while (atoms[j]) {
        if (atoms[j]->hash == h && same_name(atoms[j], s, n)) return atoms[j];
        j = (j + 1) & (atom_cap - 1);
    }
    AsStr *a = as_strn(vm, s, n);
    a->gc.permanent = 1;
    a->atom = a;
    atoms[j] = a;
    atom_count++;
    return a;
}

AsStr *as_atom(AsVm *vm, const char *s) {
    size_t n = strlen(s);
    return atom_lookup(vm, s, n, name_hash(s, n));
}

AsStr *as_intern(AsVm *vm, AsStr *s) {
    if (!s->atom) s->atom = atom_lookup(vm, s->s, s->len, s->hash);
    return s->atom;
}

// ---- conversions (SWF6 rules) -----------------------------------------------------

static AsVal to_primitive(AsVm *vm, AsVal v, int hint_string);

static double str_to_num(const char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (!*s) return 0;   // SWF6: "" is 0
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return (double)strtol(s, NULL, 16);
    char *end;
    double d = strtod(s, &end);
    while (isspace((unsigned char)*end)) end++;
    return *end ? NAN : d;
}

double as_tonum(AsVm *vm, AsVal v) {
    switch (v.t) {
    case V_UNDEF: case V_NULL: return 0;   // SWF6 (7+ says NaN)
    case V_BOOL: return v.b;
    case V_NUM: return v.n;
    case V_STR: return str_to_num(v.s->s);
    default: {
        AsVal p = to_primitive(vm, v, 0);
        return p.t == V_OBJ ? NAN : as_tonum(vm, p);
    }
    }
}

int as_toint32(AsVm *vm, AsVal v) {
    double d = as_tonum(vm, v);
    if (isnan(d) || isinf(d)) return 0;
    d = trunc(d);
    d = fmod(d, 4294967296.0);
    if (d < 0) d += 4294967296.0;
    return (int32_t)(uint32_t)d;
}

static void num_to_str(double d, char *buf) {
    if (isnan(d)) { strcpy(buf, "NaN"); return; }
    if (isinf(d)) { strcpy(buf, d > 0 ? "Infinity" : "-Infinity"); return; }
    if (d == 0) { strcpy(buf, "0"); return; }
    if (fabs(d) < 1e15 && d == floor(d)) { snprintf(buf, 32, "%.0f", d); return; }
    snprintf(buf, 32, "%.15g", d);
    // Flash prints exponents as e+21 / e-7 like this already; good enough
}

AsStr *as_tostr(AsVm *vm, AsVal v) {
    char buf[64];
    switch (v.t) {
    case V_UNDEF: return vm->empty;   // SWF6
    case V_NULL: return as_atom(vm, "null");
    case V_BOOL: return as_atom(vm, v.b ? "true" : "false");
    case V_NUM: num_to_str(v.n, buf); return as_str(vm, buf);
    case V_STR: return v.s;
    default: {
        AsVal p = to_primitive(vm, v, 1);
        if (p.t == V_OBJ) return as_atom(vm, p.o->type == OBJ_FUNC ? "[type Function]" : "[object Object]");
        return as_tostr(vm, p);
    }
    }
}

int as_tobool(AsVm *vm, AsVal v) {
    switch (v.t) {
    case V_UNDEF: case V_NULL: return 0;
    case V_BOOL: return v.b;
    case V_NUM: return !isnan(v.n) && v.n != 0;
    case V_STR: {   // SWF6 and older: a string is true if it is a non-zero number
        double d = str_to_num(v.s->s);
        return !isnan(d) && d != 0;
    }
    default: return 1;
    }
}

AsObj *as_toobj(AsVm *vm, AsVal v) {
    if (v.t == V_OBJ) return v.o;
    if (v.t == V_STR || v.t == V_NUM || v.t == V_BOOL) {
        AsObj *proto = v.t == V_STR ? vm->string_proto : v.t == V_NUM ? vm->number_proto : vm->boolean_proto;
        AsObj *o = as_obj_new(vm, OBJ_BOXED, proto);
        o->boxed = v;
        return o;
    }
    return NULL;
}

static AsVal to_primitive(AsVm *vm, AsVal v, int hint_string) {
    if (v.t != V_OBJ) return v;
    AsObj *o = v.o;
    if (o->type == OBJ_BOXED) return o->boxed;
    if (o->type == OBJ_MC) {   // a clip converts to its target path
        extern AsStr *as_mc_target_path(AsVm *vm, AsMc *mc);
        return as_strv(as_mc_target_path(vm, (AsMc *)o));
    }
    const char *first = hint_string ? "toString" : "valueOf", *second = hint_string ? "valueOf" : "toString";
    for (int k = 0; k < 2; k++) {
        AsVal f = as_get(vm, v, as_atom(vm, k ? second : first));
        if (f.t == V_OBJ && f.o->type == OBJ_FUNC) {
            AsVal r = as_call(vm, f, v, 0, NULL);
            if (r.t != V_OBJ) return r;
        }
    }
    return v;
}

// ---- properties -------------------------------------------------------------------

static AsProp *props_find(const AsProps *p, AsStr *atom) {
    if (!p->cap) return NULL;
    uint32_t j = atom->hash & (p->cap - 1);
    while (p->slots[j].key) {
        if (p->slots[j].key == atom) return &p->slots[j];
        j = (j + 1) & (p->cap - 1);
    }
    return NULL;
}

static AsProp *props_insert(AsProps *p, AsStr *atom) {
    AsProp *found = props_find(p, atom);
    if (found) return found;
    if ((p->used + 1) * 4 >= p->cap * 3) {
        int ncap = p->cap ? p->cap * 2 : 8;
        while (p->count * 2 >= ncap) ncap *= 2;
        AsProp *ns = calloc(ncap, sizeof(AsProp));
        for (int i = 0; i < p->cap; i++) {
            AsProp *s = &p->slots[i];
            if (!s->key || s->key == DELETED) continue;
            uint32_t j = s->key->hash & (ncap - 1);
            while (ns[j].key) j = (j + 1) & (ncap - 1);
            ns[j] = *s;
        }
        free(p->slots);
        p->slots = ns;
        p->cap = ncap;
        p->used = p->count;
    }
    uint32_t j = atom->hash & (p->cap - 1);
    while (p->slots[j].key && p->slots[j].key != DELETED) j = (j + 1) & (p->cap - 1);
    if (!p->slots[j].key) p->used++;
    p->slots[j].key = atom;
    p->slots[j].v = as_undef();
    p->slots[j].flags = 0;
    p->count++;
    return &p->slots[j];
}

AsProp *as_own(AsObj *o, AsStr *atom) { return props_find(&o->props, atom); }

static void props_delete(AsProps *p, AsStr *atom) {
    AsProp *s = props_find(p, atom);
    if (!s) return;
    s->key = DELETED;
    s->v = as_undef();
    p->count--;
}

// ---- objects ----------------------------------------------------------------------

AsObj *as_obj_new(AsVm *vm, int type, AsObj *proto) {
    AsObj *o = gc_alloc(vm, type == OBJ_MC ? sizeof(AsMc) : sizeof(AsObj), GC_OBJ);
    o->type = type;
    o->proto = proto;
    return o;
}

AsObj *as_array_new(AsVm *vm, int n) {
    AsObj *a = as_obj_new(vm, OBJ_ARRAY, vm->array_proto);
    if (n > 0) {
        a->capitems = n;
        a->items = calloc(n, sizeof(AsVal));
        a->nitems = n;
    }
    return a;
}

static void array_resize(AsObj *a, int n) {
    if (n > a->capitems) {
        int nc = a->capitems ? a->capitems : 4;
        while (nc < n) nc *= 2;
        a->items = realloc(a->items, sizeof(AsVal) * nc);
        for (int i = a->capitems; i < nc; i++) a->items[i] = as_undef();
        a->capitems = nc;
    }
    for (int i = n; i < a->nitems; i++) a->items[i] = as_undef();
    a->nitems = n;
}

void as_array_push(AsVm *vm, AsObj *a, AsVal v) {
    (void)vm;
    array_resize(a, a->nitems + 1);
    a->items[a->nitems - 1] = v;
}

AsObj *as_native(AsVm *vm, AsNative fn) {
    AsObj *f = as_obj_new(vm, OBJ_FUNC, vm->function_proto);
    f->native = fn;
    return f;
}

void as_def(AsVm *vm, AsObj *o, const char *name, AsVal v) {
    AsProp *p = props_insert(&o->props, as_atom(vm, name));
    p->v = v;
    p->flags = PROP_DONTENUM;
}

void as_def_fn(AsVm *vm, AsObj *o, const char *name, AsNative fn) {
    as_def(vm, o, name, as_objv(as_native(vm, fn)));
}

// "123" -> 123 for array indices, -1 otherwise
static int array_index(const AsStr *k) {
    if (!k->len || k->len > 9) return -1;
    int n = 0;
    for (uint32_t i = 0; i < k->len; i++) {
        if (k->s[i] < '0' || k->s[i] > '9') return -1;
        n = n * 10 + (k->s[i] - '0');
    }
    if (k->len > 1 && k->s[0] == '0') return -1;
    return n;
}

extern int as_mc_get_member(AsVm *vm, AsMc *mc, AsStr *atom, AsVal *out);
extern int as_mc_set_member(AsVm *vm, AsMc *mc, AsStr *atom, AsVal v);

static AsStr *A_length, *A_prototype, *A_proto, *A_constructor, *A_this, *A_root, *A_global,
    *A_parent, *A_level0, *A_arguments, *A_callee, *A_caller;

AsVal as_get(AsVm *vm, AsVal v, AsStr *key) {
    AsStr *atom = as_intern(vm, key);
    if (v.t == V_STR) {
        if (atom == A_length) return as_num(v.s->len);
        v = as_objv(as_toobj(vm, v));
    } else if (v.t == V_NUM || v.t == V_BOOL) {
        v = as_objv(as_toobj(vm, v));
    }
    if (v.t != V_OBJ) return as_undef();
    AsObj *o = v.o;
    if (atom == A_proto) return as_objv(o->proto);
    if (o->type == OBJ_ARRAY) {
        if (atom == A_length) return as_num(o->nitems);
        int i = array_index(atom);
        if (i >= 0) return i < o->nitems ? o->items[i] : as_undef();
    }
    if (o->type == OBJ_BOXED && o->boxed.t == V_STR && atom == A_length) return as_num(o->boxed.s->len);
    if (o->type == OBJ_MC) {
        AsVal out;
        if (as_mc_get_member(vm, (AsMc *)o, atom, &out)) return out;
    }
    for (AsObj *p = o; p; p = p->proto) {
        AsProp *pr = props_find(&p->props, atom);
        if (pr) return pr->v;
        if (p->proto == p) break;
    }
    return as_undef();
}

void as_set(AsVm *vm, AsVal v, AsStr *key, AsVal val) {
    if (v.t != V_OBJ) return;   // setting on primitives is lost
    AsObj *o = v.o;
    AsStr *atom = as_intern(vm, key);
    if (atom == A_proto) { o->proto = val.t == V_OBJ ? val.o : NULL; return; }
    if (o->type == OBJ_ARRAY) {
        if (atom == A_length) { int n = as_toint32(vm, val); array_resize(o, n < 0 ? 0 : n); return; }
        int i = array_index(atom);
        if (i >= 0) {
            if (i >= o->nitems) array_resize(o, i + 1);
            o->items[i] = val;
            return;
        }
    }
    if (o->type == OBJ_MC && as_mc_set_member(vm, (AsMc *)o, atom, val)) return;
    AsProp *p = props_insert(&o->props, atom);
    if (p->flags & PROP_READONLY) return;
    p->v = val;
}

// ---- calls ------------------------------------------------------------------------

typedef struct {
    const uint8_t *code;       // the movie's data
    uint32_t pc, end;
    const AsPool *pool;
    AsScope *scope;
    AsVal *regs;
    int nregs;
    AsVal this_;
    AsMc *target;              // timeline for gotoAndStop & co.
    AsObj *activation;         // locals (NULL on a timeline)
    const Movie *movie;
    AsObj *callee;
    AsVal ret;
    int returned;
    int captured;              // a function was defined here: its scope chain must outlive the call
} Ctx;

static void run(AsVm *vm, Ctx *c);

static AsScope *scope_new(AsVm *vm, AsObj *o, AsScope *parent) {
    AsScope *s = gc_alloc(vm, sizeof(AsScope), GC_SCOPE);
    s->obj = o;
    s->parent = parent;
    return s;
}

static AsObj *make_args(AsVm *vm, int argc, AsVal *argv, AsObj *callee) {
    AsObj *a = as_array_new(vm, argc);
    for (int i = 0; i < argc; i++) a->items[i] = argv[i];
    AsProp *p = props_insert(&a->props, A_callee);
    p->v = as_objv(callee);
    p->flags = PROP_DONTENUM;
    return a;
}

AsStr *as_mc_target_path(AsVm *vm, AsMc *mc);

static AsMc *mc_of(AsVal v) {
    return v.t == V_OBJ && v.o->type == OBJ_MC ? (AsMc *)v.o : NULL;
}

static AsVal as_call_body(AsVm *vm, AsVal fv, AsVal this_, int argc, AsVal *argv);

void as_pin(AsVm *vm, const char *name, AsVal v, const char *only_in) {
    if (vm->npinned < (int)(sizeof vm->pinned / sizeof vm->pinned[0])) {
        vm->pinned[vm->npinned].name = as_atom(vm, name);
        vm->pinned[vm->npinned].only_in = only_in ? as_atom(vm, only_in) : NULL;
        vm->pinned[vm->npinned++].v = v;
    }
}

void as_override(AsVm *vm, const char *name, AsNative fn) {
    if (vm->noverrides < (int)(sizeof vm->overrides / sizeof vm->overrides[0])) {
        vm->overrides[vm->noverrides].name = as_atom(vm, name);
        vm->overrides[vm->noverrides++].fn = fn;
    }
}

// ---- profiler (as_prof_enable): time and actions per script function -----------------

typedef struct {
    const AsObj *f;
    long long calls, ops_excl;
    double ms_incl, ms_excl;
} ProfEnt;

#define PROF_SIZE 8192
static ProfEnt *g_prof;
static double g_prof_child;     // time spent in callees of the running function
static long long g_prof_child_ops;

void as_prof_enable(int on) {
    if (on && !g_prof) g_prof = calloc(PROF_SIZE, sizeof(ProfEnt));
    if (!on) { free(g_prof); g_prof = NULL; }
}

static ProfEnt *prof_slot(const AsObj *f) {
    uint32_t h = (uint32_t)((uintptr_t)f >> 4) * 2654435761u;
    for (int n = 0; n < PROF_SIZE; n++) {
        ProfEnt *e = &g_prof[(h + n) & (PROF_SIZE - 1)];
        if (!e->f || e->f == f) { e->f = f; return e; }
    }
    return NULL;
}

static int cmp_prof(const void *a, const void *b) {
    double x = ((const ProfEnt *)a)->ms_excl, y = ((const ProfEnt *)b)->ms_excl;
    return x < y ? 1 : x > y ? -1 : 0;
}

void as_prof_report(int top, void (*out)(const char *line)) {
    if (!g_prof) return;
    ProfEnt *all = malloc(sizeof(ProfEnt) * PROF_SIZE);
    int n = 0;
    double total = 0;
    for (int i = 0; i < PROF_SIZE; i++)
        if (g_prof[i].f) { all[n++] = g_prof[i]; total += g_prof[i].ms_excl; }
    qsort(all, n, sizeof(ProfEnt), cmp_prof);
    char line[256];
    for (int i = 0; i < n && i < top; i++) {
        const AsObj *f = all[i].f;
        snprintf(line, sizeof line, "%8.1f ms excl %5.1f%%  %8.1f incl  %8lld calls  %10lld ops  %s",
                 all[i].ms_excl, total ? 100 * all[i].ms_excl / total : 0, all[i].ms_incl, all[i].calls, all[i].ops_excl,
                 f->fname ? f->fname->s : f->native ? "(native)" : "(anonymous)");
        out(line);
    }
    free(all);
}

AsVal as_call(AsVm *vm, AsVal fv, AsVal this_, int argc, AsVal *argv) {
    if (fv.t != V_OBJ || fv.o->type != OBJ_FUNC) return as_undef();
    AsObj *f = fv.o;
    if (g_prof) {   // time it, minus what its callees took
        extern double as_time_ms(void);
        ProfEnt *e = prof_slot(f);
        double t0 = as_time_ms(), child0 = g_prof_child;
        long long ops0 = vm->ops, cops0 = g_prof_child_ops;
        g_prof_child = 0;
        g_prof_child_ops = 0;
        AsVal r = as_call_body(vm, fv, this_, argc, argv);
        double dt = as_time_ms() - t0;
        long long dops = vm->ops - ops0;
        if (e) {
            e->calls++;
            e->ms_incl += dt;
            e->ms_excl += dt - g_prof_child;
            e->ops_excl += dops - g_prof_child_ops;
        }
        g_prof_child = child0 + dt;
        g_prof_child_ops = cops0 + dops;
        return r;
    }
    return as_call_body(vm, fv, this_, argc, argv);
}

static AsVal as_call_body(AsVm *vm, AsVal fv, AsVal this_, int argc, AsVal *argv) {
    AsObj *f = fv.o;
    if (f->native) return f->native(vm, this_, argc, argv);
    if (vm->depth > 200) { as_warn(vm, "call depth limit"); return as_undef(); }
    vm->depth++;

    AsObj *act = call_alloc(&g_free_acts, sizeof(AsObj), GC_OBJ);
    act->type = OBJ_ACTIVATION;
    AsScope *scope = call_alloc(&g_free_scopes, sizeof(AsScope), GC_SCOPE);
    scope->obj = act;
    scope->parent = f->scope;
    Ctx c;
    memset(&c, 0, sizeof c);
    c.code = f->code_movie->data;
    c.movie = f->code_movie;
    c.pc = f->code_off;
    c.end = f->code_off + f->code_len;
    c.pool = f->pool;
    c.scope = scope;
    c.this_ = this_;
    c.target = f->target;
    c.activation = act;
    c.callee = f;
    AsVal regs[256];
    if (f->v2) {
        c.nregs = f->nregs ? f->nregs : 1;
        c.regs = regs;
        for (int i = 0; i < c.nregs; i++) regs[i] = as_undef();
        int r = 1;
        uint16_t fl = f->flags2;
        // preload order: this, arguments, super, _root, _parent, _global
        if (fl & 0x0001) regs[r++] = this_;
        else if (!(fl & 0x0002)) props_insert(&act->props, A_this)->v = this_;
        if (fl & 0x0004) regs[r++] = as_objv(make_args(vm, argc, argv, f));
        else if (!(fl & 0x0008)) props_insert(&act->props, A_arguments)->v = as_objv(make_args(vm, argc, argv, f));
        if (fl & 0x0010) regs[r++] = as_undef();   // super: no classes in this game
        if (fl & 0x0040) regs[r++] = as_objv(&vm->root->obj);
        if (fl & 0x0080) {
            AsMc *t = mc_of(this_);
            if (!t) t = f->target;
            regs[r++] = t && t->parent ? as_objv(&t->parent->obj) : as_undef();
        }
        if (fl & 0x0100) regs[r++] = as_objv(vm->global);
        for (int i = 0; i < f->nparams; i++) {
            AsVal a = i < argc ? argv[i] : as_undef();
            if (f->param_regs[i]) { if (f->param_regs[i] < c.nregs) regs[f->param_regs[i]] = a; }
            else props_insert(&act->props, f->param_names[i])->v = a;
        }
    } else {
        c.regs = vm->global_regs;
        c.nregs = 4;
        props_insert(&act->props, A_this)->v = this_;
        props_insert(&act->props, A_arguments)->v = as_objv(make_args(vm, argc, argv, f));
        for (int i = 0; i < f->nparams; i++)
            props_insert(&act->props, f->param_names[i])->v = i < argc ? argv[i] : as_undef();
    }
    int sp0 = vm->sp;
    AsObj *prev_fn = vm->cur_fn;
    vm->cur_fn = f;
    run(vm, &c);
    vm->cur_fn = prev_fn;
    vm->sp = sp0;
    vm->depth--;
    if (c.captured) {
        call_keep(vm, &act->gc);
        call_keep(vm, &scope->gc);
    } else {
        call_release(&g_free_acts, &act->gc);
        call_release(&g_free_scopes, &scope->gc);
    }
    return c.ret;
}

AsVal as_call_method(AsVm *vm, AsVal obj, const char *name, int argc, AsVal *argv) {
    AsVal f = as_get(vm, obj, as_atom(vm, name));
    return as_call(vm, f, obj, argc, argv);
}

// ---- variables --------------------------------------------------------------------

static int has_path_chars(const AsStr *s) {
    for (uint32_t i = 0; i < s->len; i++)
        if (s->s[i] == '/' || s->s[i] == ':' || s->s[i] == '.') return 1;
    return 0;
}

static int lookup_in(AsVm *vm, AsObj *o, AsStr *atom, AsVal *out) {
    if (o->type == OBJ_MC) {
        if (as_mc_get_member(vm, (AsMc *)o, atom, out)) return 1;
    }
    for (AsObj *p = o; p; p = p->proto) {
        AsProp *pr = props_find(&p->props, atom);
        if (pr) { *out = pr->v; return 1; }
    }
    return 0;
}

static AsVal get_variable(AsVm *vm, Ctx *c, AsStr *name) {
    AsStr *atom = as_intern(vm, name);
    if (atom == A_this) {
        AsVal v;
        if (c->activation && lookup_in(vm, c->activation, atom, &v)) return v;
        return c->this_.t != V_UNDEF ? c->this_ : (c->target ? as_objv(&c->target->obj) : as_undef());
    }
    if (atom == A_root || atom == A_level0) return as_objv(&vm->root->obj);
    if (atom == A_global) return as_objv(vm->global);
    for (int i = 0; i < vm->npinned; i++)
        if (vm->pinned[i].name == atom &&
            (!vm->pinned[i].only_in || (vm->cur_fn && vm->cur_fn->fname == vm->pinned[i].only_in)))
            return vm->pinned[i].v;
    if (atom == A_parent) {
        AsVal v;
        if (c->target && as_mc_get_member(vm, c->target, atom, &v)) return v;
        return as_undef();
    }
    if (has_path_chars(name)) {
        int found;
        AsVal v = as_resolve_path(vm, c->target ? c->target : vm->root, name->s, &found);
        if (found) return v;
    }
    for (AsScope *s = c->scope; s; s = s->parent) {
        AsVal v;
        if (s->obj && lookup_in(vm, s->obj, atom, &v)) return v;
    }
    AsVal v;
    if (lookup_in(vm, vm->global, atom, &v)) return v;
    return as_undef();
}

static void set_variable(AsVm *vm, Ctx *c, AsStr *name, AsVal val) {
    if (has_path_chars(name)) {
        // "a.b.c" or "/a/b:c": resolve the owner, then set the last part
        const char *s = name->s;
        const char *cut = strrchr(s, ':');
        const char *dot = strrchr(s, '.');
        if (!cut || (dot && dot > cut)) cut = dot;
        if (cut) {
            char owner[256];
            size_t n = (size_t)(cut - s);
            if (n >= sizeof owner) n = sizeof owner - 1;
            memcpy(owner, s, n);
            owner[n] = 0;
            int found;
            AsVal o = n ? as_resolve_path(vm, c->target ? c->target : vm->root, owner, &found)
                        : as_objv(&(c->target ? c->target : vm->root)->obj);
            if (n && !found) return;
            as_set(vm, o, as_atom(vm, cut + 1), val);
            return;
        }
    }
    AsStr *atom = as_intern(vm, name);
    for (int i = 0; i < vm->npinned; i++)
        if (vm->pinned[i].name == atom && !vm->pinned[i].only_in) return;   // the host decides this one
    for (AsScope *s = c->scope; s; s = s->parent) {
        if (!s->obj) continue;
        if (props_find(&s->obj->props, atom) ||
            (s->obj->type == OBJ_MC && as_mc_prop_index(atom->s) >= 0)) {
            as_set(vm, as_objv(s->obj), atom, val);
            return;
        }
    }
    // not found: the timeline gets it (functions write to their defining clip)
    AsMc *t = c->target ? c->target : vm->root;
    as_set(vm, as_objv(&t->obj), atom, val);
}

// ---- comparison and arithmetic ----------------------------------------------------

static int strict_equals(AsVal a, AsVal b) {
    if (a.t != b.t) return 0;
    switch (a.t) {
    case V_UNDEF: case V_NULL: return 1;
    case V_BOOL: return a.b == b.b;
    case V_NUM: return a.n == b.n;
    case V_STR: return a.s == b.s || (a.s->len == b.s->len && !memcmp(a.s->s, b.s->s, a.s->len));
    default: return a.o == b.o;
    }
}

static int abstract_equals(AsVm *vm, AsVal a, AsVal b) {
    if (a.t == b.t) return strict_equals(a, b);
    if ((a.t == V_UNDEF || a.t == V_NULL) && (b.t == V_UNDEF || b.t == V_NULL)) return 1;
    if (a.t == V_UNDEF || a.t == V_NULL || b.t == V_UNDEF || b.t == V_NULL) return 0;
    if (a.t == V_NUM && b.t == V_STR) return a.n == as_tonum(vm, b);
    if (a.t == V_STR && b.t == V_NUM) return as_tonum(vm, a) == b.n;
    if (a.t == V_BOOL) return abstract_equals(vm, as_num(a.b), b);
    if (b.t == V_BOOL) return abstract_equals(vm, a, as_num(b.b));
    if (a.t == V_OBJ && b.t != V_OBJ) return abstract_equals(vm, to_primitive(vm, a, 0), b);
    if (b.t == V_OBJ && a.t != V_OBJ) return abstract_equals(vm, a, to_primitive(vm, b, 0));
    return 0;
}

static int less_than(AsVm *vm, AsVal a, AsVal b) {   // 0, 1, or -1 for undefined (NaN)
    a = to_primitive(vm, a, 0);
    b = to_primitive(vm, b, 0);
    if (a.t == V_STR && b.t == V_STR) return strcmp(a.s->s, b.s->s) < 0;
    double x = as_tonum(vm, a), y = as_tonum(vm, b);
    if (isnan(x) || isnan(y)) return -1;
    return x < y;
}

// ---- the interpreter --------------------------------------------------------------

static inline void push(AsVm *vm, AsVal v) {
    if (vm->sp < AS_MAX_STACK) vm->stack[vm->sp++] = v;
}

static inline AsVal pop(AsVm *vm) { return vm->sp > 0 ? vm->stack[--vm->sp] : as_undef(); }

static inline uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

// Constant pools are cached by position: the same block always builds the same pool.
typedef struct PoolEntry { const uint8_t *at; AsPool pool; struct PoolEntry *next; } PoolEntry;
static PoolEntry *pool_cache[1024];

static const AsPool *get_pool(AsVm *vm, const uint8_t *body, uint32_t len) {
    uintptr_t h = ((uintptr_t)body >> 3) & 1023;
    for (PoolEntry *e = pool_cache[h]; e; e = e->next)
        if (e->at == body) return &e->pool;
    PoolEntry *e = calloc(1, sizeof *e);
    e->at = body;
    int n = len >= 2 ? rd16(body) : 0;
    e->pool.s = calloc(n ? n : 1, sizeof(AsStr *));
    const char *s = (const char *)body + 2, *end = (const char *)body + len;
    for (int i = 0; i < n && s < end; i++) {
        e->pool.s[i] = as_atom(vm, s);
        s += strlen(s) + 1;
        e->pool.n++;
    }
    e->next = pool_cache[h];
    pool_cache[h] = e;
    return &e->pool;
}

static double rd_double(const uint8_t *p) {   // AVM1 doubles store the high word first
    uint8_t b[8];
    memcpy(b, p + 4, 4);
    memcpy(b + 4, p, 4);
    double d;
    memcpy(&d, b, 8);
    return d;
}

static void do_push(AsVm *vm, Ctx *c, const uint8_t *p, uint32_t len) {
    const uint8_t *end = p + len;
    while (p < end) {
        int t = *p++;
        switch (t) {
        case 0: { size_t n = strnlen((const char *)p, (size_t)(end - p)); push(vm, as_strv(as_strn(vm, (const char *)p, n))); p += n + 1; break; }
        case 1: { float f; memcpy(&f, p, 4); push(vm, as_num(f)); p += 4; break; }
        case 2: push(vm, as_null()); break;
        case 3: push(vm, as_undef()); break;
        case 4: push(vm, *p < c->nregs ? c->regs[*p] : as_undef()); p++; break;
        case 5: push(vm, as_bool(*p)); p++; break;
        case 6: push(vm, as_num(rd_double(p))); p += 8; break;
        case 7: push(vm, as_num((int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)))); p += 4; break;
        case 8: push(vm, c->pool && *p < c->pool->n ? as_strv(c->pool->s[*p]) : as_undef()); p++; break;
        case 9: { int i = rd16(p); push(vm, c->pool && i < c->pool->n ? as_strv(c->pool->s[i]) : as_undef()); p += 2; break; }
        default: return;
        }
    }
}

static AsObj *define_function(AsVm *vm, Ctx *c, const uint8_t *body, uint32_t len, int v2, uint32_t code_start,
                              AsStr **name_out) {
    AsObj *f = as_obj_new(vm, OBJ_FUNC, vm->function_proto);
    const uint8_t *p = body, *end = body + len;
    size_t n = strnlen((const char *)p, len);
    *name_out = n ? as_atom(vm, (const char *)p) : NULL;
    p += n + 1;
    f->nparams = rd16(p);
    p += 2;
    if (v2) {
        f->v2 = 1;
        f->nregs = *p++;
        f->flags2 = rd16(p);
        p += 2;
    }
    f->param_names = calloc(f->nparams ? f->nparams : 1, sizeof(AsStr *));
    f->param_regs = calloc(f->nparams ? f->nparams : 1, 1);
    for (int i = 0; i < f->nparams && p < end; i++) {
        if (v2) f->param_regs[i] = *p++;
        size_t pn = strnlen((const char *)p, (size_t)(end - p));
        f->param_names[i] = as_atom(vm, (const char *)p);
        p += pn + 1;
    }
    uint16_t size = rd16(p);
    f->code_movie = c->movie;
    f->code_off = code_start;
    f->code_len = size;
    f->pool = c->pool;
    f->scope = c->scope;
    c->captured = 1;   // the new function holds this call's scope chain
    f->target = c->target;
    f->fname = *name_out;
    // a native stand-in the host registered for this script function (same results, faster)
    for (int i = 0; f->fname && i < vm->noverrides; i++)
        if (vm->overrides[i].name == f->fname) f->native = vm->overrides[i].fn;
    AsObj *proto = as_obj_new(vm, OBJ_PLAIN, vm->object_proto);
    AsProp *cp = props_insert(&proto->props, A_constructor);
    cp->v = as_objv(f);
    cp->flags = PROP_DONTENUM;
    AsProp *pp = props_insert(&f->props, A_prototype);
    pp->v = as_objv(proto);
    pp->flags = PROP_DONTENUM;
    return f;
}

extern void as_mc_goto_value(AsVm *vm, AsMc *mc, AsVal frame, int play);
extern void as_mc_get_url(AsVm *vm, AsMc *base, AsVal url, AsVal target, int flags);
extern AsMc *as_mc_resolve_target(AsVm *vm, AsMc *base, AsVal target);

static void run(AsVm *vm, Ctx *c) {
    const uint8_t *d = c->code;
    while (c->pc < c->end && !c->returned) {
        uint8_t op = d[c->pc++];
        if (op == 0) break;
        uint32_t len = 0;
        if (op >= 0x80) { len = rd16(d + c->pc); c->pc += 2; }
        const uint8_t *body = d + c->pc;
        uint32_t next = c->pc + len;
        vm->ops++;
        switch (op) {
        case 0x04: if (c->target) as_mc_goto(vm, c->target, c->target->frame + 1, 0); break;   // NextFrame
        case 0x05: if (c->target && c->target->frame > 0) as_mc_goto(vm, c->target, c->target->frame - 1, 0); break;
        case 0x06: if (c->target) c->target->playing = 1; break;                               // Play
        case 0x07: if (c->target) c->target->playing = 0; break;                               // Stop
        case 0x08: case 0x09: break;                                                            // quality, stopSounds
        case 0x0A: { double b = as_tonum(vm, pop(vm)), a = as_tonum(vm, pop(vm)); push(vm, as_num(a + b)); break; }
        case 0x0B: { double b = as_tonum(vm, pop(vm)), a = as_tonum(vm, pop(vm)); push(vm, as_num(a - b)); break; }
        case 0x0C: { double b = as_tonum(vm, pop(vm)), a = as_tonum(vm, pop(vm)); push(vm, as_num(a * b)); break; }
        case 0x0D: { double b = as_tonum(vm, pop(vm)), a = as_tonum(vm, pop(vm)); push(vm, as_num(a / b)); break; }
        case 0x0E: { double b = as_tonum(vm, pop(vm)), a = as_tonum(vm, pop(vm)); push(vm, as_bool(a == b)); break; }
        case 0x0F: { double b = as_tonum(vm, pop(vm)), a = as_tonum(vm, pop(vm)); push(vm, as_bool(a < b)); break; }
        case 0x10: { int b = as_tobool(vm, pop(vm)), a = as_tobool(vm, pop(vm)); push(vm, as_bool(a && b)); break; }
        case 0x11: { int b = as_tobool(vm, pop(vm)), a = as_tobool(vm, pop(vm)); push(vm, as_bool(a || b)); break; }
        case 0x12: case 0x70: push(vm, as_bool(!as_tobool(vm, pop(vm)))); break;           // Not (0x70: the game's own)
        case 0x13: { AsStr *b = as_tostr(vm, pop(vm)), *a = as_tostr(vm, pop(vm)); push(vm, as_bool(!strcmp(a->s, b->s))); break; }
        case 0x14: push(vm, as_num(as_tostr(vm, pop(vm))->len)); break;
        case 0x15: {   // StringExtract (1-based)
            int n = as_toint32(vm, pop(vm)), i = as_toint32(vm, pop(vm)) - 1;
            AsStr *s = as_tostr(vm, pop(vm));
            if (i < 0) i = 0;
            if (i > (int)s->len) i = s->len;
            if (n < 0 || i + n > (int)s->len) n = s->len - i;
            push(vm, as_strv(as_strn(vm, s->s + i, n)));
            break;
        }
        case 0x17: pop(vm); break;
        case 0x18: { double v = as_tonum(vm, pop(vm)); push(vm, as_num(isnan(v) ? 0 : trunc(v))); break; }
        case 0x1C: { AsStr *n = as_tostr(vm, pop(vm)); push(vm, get_variable(vm, c, n)); break; }
        case 0x1D: { AsVal v = pop(vm); AsStr *n = as_tostr(vm, pop(vm)); set_variable(vm, c, n, v); break; }
        case 0x21: { AsStr *b = as_tostr(vm, pop(vm)), *a = as_tostr(vm, pop(vm));
            AsStr *s = gc_alloc(vm, sizeof(AsStr) + a->len + b->len + 1, GC_STR);
            memcpy(s->s, a->s, a->len); memcpy(s->s + a->len, b->s, b->len + 1);
            s->len = a->len + b->len; s->hash = name_hash(s->s, s->len);
            push(vm, as_strv(s)); break; }
        case 0x22: {   // GetProperty
            int idx = as_toint32(vm, pop(vm));
            AsMc *t = as_mc_resolve_target(vm, c->target, pop(vm));
            push(vm, t ? as_mc_get_prop(vm, t, idx) : as_undef());
            break;
        }
        case 0x23: {   // SetProperty
            AsVal v = pop(vm);
            int idx = as_toint32(vm, pop(vm));
            AsMc *t = as_mc_resolve_target(vm, c->target, pop(vm));
            if (t) as_mc_set_prop(vm, t, idx, v);
            break;
        }
        case 0x25: { AsMc *t = as_mc_resolve_target(vm, c->target, pop(vm)); if (t) as_mc_remove(vm, t); break; }
        case 0x26: { AsStr *s = as_tostr(vm, pop(vm)); if (vm->trace) vm->trace(vm, s->s); break; }
        case 0x30: {   // RandomNumber
            int n = as_toint32(vm, pop(vm));
            vm->rand_state = vm->rand_state * 1103515245u + 12345u;
            push(vm, as_num(n > 0 ? (int)((vm->rand_state >> 8) % (uint32_t)n) : 0));
            break;
        }
        case 0x34: { extern double as_time_ms(void); push(vm, as_num(as_time_ms())); break; }
        case 0x3C: {   // DefineLocal
            AsVal v = pop(vm);
            AsStr *n = as_intern(vm, as_tostr(vm, pop(vm)));
            AsObj *o = c->activation ? c->activation : &(c->target ? c->target : vm->root)->obj;
            as_set(vm, as_objv(o), n, v);
            break;
        }
        case 0x41: {   // DefineLocal2
            AsStr *n = as_intern(vm, as_tostr(vm, pop(vm)));
            AsObj *o = c->activation ? c->activation : &(c->target ? c->target : vm->root)->obj;
            if (!props_find(&o->props, n)) as_set(vm, as_objv(o), n, as_undef());
            break;
        }
        case 0x3D: {   // CallFunction
            AsStr *n = as_tostr(vm, pop(vm));
            int argc = as_toint32(vm, pop(vm));
            if (argc < 0) argc = 0;
            if (argc > vm->sp) argc = vm->sp;
            AsVal args[64];
            if (argc > 64) argc = 64;
            for (int i = 0; i < argc; i++) args[i] = pop(vm);
            AsVal f = get_variable(vm, c, n);
            if (AS_ENV("AS_CALLS"))
                as_warn(vm, "call %s()", f.t == V_OBJ && f.o->type == OBJ_FUNC && f.o->fname ? f.o->fname->s : n->s);
            if (f.t != V_OBJ || f.o->type != OBJ_FUNC) {
                as_warn(vm, "call to undefined function %s()", n->s);
                push(vm, as_undef());
                break;
            }
            AsVal self = c->target ? as_objv(&c->target->obj) : as_undef();
            push(vm, as_call(vm, f, self, argc, args));
            break;
        }
        case 0x3E: c->ret = pop(vm); c->returned = 1; break;
        case 0x3F: { double b = as_tonum(vm, pop(vm)), a = as_tonum(vm, pop(vm)); push(vm, as_num(fmod(a, b))); break; }
        case 0x40: {   // NewObject
            AsStr *n = as_tostr(vm, pop(vm));
            int argc = as_toint32(vm, pop(vm));
            if (argc < 0) argc = 0;
            if (argc > 64) argc = 64;
            AsVal args[64];
            for (int i = 0; i < argc; i++) args[i] = pop(vm);
            AsVal ctor = get_variable(vm, c, n);
            if (ctor.t != V_OBJ || ctor.o->type != OBJ_FUNC) {
                as_warn(vm, "new of unknown class %s", n->s);
                push(vm, as_undef());
                break;
            }
            AsVal proto = as_get(vm, ctor, A_prototype);
            AsObj *o = as_obj_new(vm, OBJ_PLAIN, proto.t == V_OBJ ? proto.o : vm->object_proto);
            AsVal r = as_call(vm, ctor, as_objv(o), argc, args);
            push(vm, r.t == V_OBJ ? r : as_objv(o));
            break;
        }
        case 0x42: {   // InitArray
            int n = as_toint32(vm, pop(vm));
            if (n < 0) n = 0;
            AsObj *a = as_array_new(vm, n);
            for (int i = 0; i < n; i++) a->items[i] = pop(vm);
            push(vm, as_objv(a));
            break;
        }
        case 0x43: {   // InitObject
            int n = as_toint32(vm, pop(vm));
            AsObj *o = as_obj_new(vm, OBJ_PLAIN, vm->object_proto);
            for (int i = 0; i < n; i++) {
                AsVal v = pop(vm);
                AsStr *k = as_tostr(vm, pop(vm));
                as_set(vm, as_objv(o), k, v);
            }
            push(vm, as_objv(o));
            break;
        }
        case 0x44: {   // TypeOf
            AsVal v = pop(vm);
            const char *t = v.t == V_UNDEF ? "undefined" : v.t == V_NULL ? "null" : v.t == V_BOOL ? "boolean"
                          : v.t == V_NUM ? "number" : v.t == V_STR ? "string"
                          : v.o->type == OBJ_FUNC ? "function" : v.o->type == OBJ_MC ? "movieclip" : "object";
            push(vm, as_strv(as_atom(vm, t)));
            break;
        }
        case 0x47: {   // Add2
            AsVal b = to_primitive(vm, pop(vm), 0), a = to_primitive(vm, pop(vm), 0);
            if (a.t == V_STR || b.t == V_STR) {
                AsStr *x = as_tostr(vm, a), *y = as_tostr(vm, b);
                AsStr *s = gc_alloc(vm, sizeof(AsStr) + x->len + y->len + 1, GC_STR);
                memcpy(s->s, x->s, x->len); memcpy(s->s + x->len, y->s, y->len + 1);
                s->len = x->len + y->len; s->hash = name_hash(s->s, s->len);
                push(vm, as_strv(s));
            } else {
                push(vm, as_num(as_tonum(vm, a) + as_tonum(vm, b)));
            }
            break;
        }
        case 0x48: { AsVal b = pop(vm), a = pop(vm); int r = less_than(vm, a, b); push(vm, r < 0 ? as_undef() : as_bool(r)); break; }
        case 0x67: { AsVal b = pop(vm), a = pop(vm); int r = less_than(vm, b, a); push(vm, r < 0 ? as_undef() : as_bool(r)); break; }
        case 0x49: { AsVal b = pop(vm), a = pop(vm); push(vm, as_bool(abstract_equals(vm, a, b))); break; }
        case 0x66: { AsVal b = pop(vm), a = pop(vm); push(vm, as_bool(strict_equals(a, b))); break; }
        case 0x4A: push(vm, as_num(as_tonum(vm, pop(vm)))); break;
        case 0x4B: push(vm, as_strv(as_tostr(vm, pop(vm)))); break;
        case 0x4C: { AsVal v = pop(vm); push(vm, v); push(vm, v); break; }
        case 0x4D: { AsVal a = pop(vm), b = pop(vm); push(vm, a); push(vm, b); break; }
        case 0x4E: {   // GetMember
            AsStr *k = as_tostr(vm, pop(vm));
            AsVal o = pop(vm);
            push(vm, as_get(vm, o, k));
            break;
        }
        case 0x4F: {   // SetMember
            AsVal v = pop(vm);
            AsStr *k = as_tostr(vm, pop(vm));
            AsVal o = pop(vm);
            static const char *watch;   // AS_WATCH=name: report undefined being stored there
            if (!watch) watch = AS_ENV("AS_WATCH") ? AS_ENV("AS_WATCH") : "";
            if (*watch && (v.t == V_UNDEF || AS_ENV("AS_WATCH_ALL")) && !strcasecmp(k->s, watch))
                as_warn(vm, "%s set to %s at @%u (in %s)", watch, v.t == V_UNDEF ? "undefined" : as_tostr(vm, v)->s,
                        (unsigned)c->pc, vm->cur_fn && vm->cur_fn->fname ? vm->cur_fn->fname->s : "timeline");
            as_set(vm, o, k, v);
            break;
        }
        case 0x50: push(vm, as_num(as_tonum(vm, pop(vm)) + 1)); break;
        case 0x51: push(vm, as_num(as_tonum(vm, pop(vm)) - 1)); break;
        case 0x52: {   // CallMethod
            AsVal name = pop(vm);
            AsVal obj = pop(vm);
            int argc = as_toint32(vm, pop(vm));
            if (argc < 0) argc = 0;
            if (argc > 64) argc = 64;
            AsVal args[64];
            for (int i = 0; i < argc; i++) args[i] = pop(vm);
            AsVal f, self = obj;
            if (name.t == V_UNDEF || (name.t == V_STR && !name.s->len)) { f = obj; self = as_undef(); }
            else f = as_get(vm, obj, as_tostr(vm, name));
            if (AS_ENV("AS_CALLS") && f.t == V_OBJ && f.o->type == OBJ_FUNC)
                as_warn(vm, "call %s()", f.o->fname ? f.o->fname->s : as_tostr(vm, name)->s);
            if (f.t != V_OBJ || f.o->type != OBJ_FUNC) {
                AsStr *ns = as_tostr(vm, name);
                as_warn(vm, "call to undefined method %s() on %s (in %s)", ns->s,
                        obj.t == V_OBJ ? (obj.o->type == OBJ_MC ? as_tostr(vm, obj)->s : "object")
                                       : obj.t == V_UNDEF ? "undefined" : as_tostr(vm, obj)->s,
                        vm->cur_fn && vm->cur_fn->fname ? vm->cur_fn->fname->s
                        : c->target ? as_mc_target_path(vm, c->target)->s : "?");
                push(vm, as_undef());
                break;
            }
            push(vm, as_call(vm, f, self, argc, args));
            break;
        }
        case 0x53: {   // NewMethod
            AsVal name = pop(vm), obj = pop(vm);
            int argc = as_toint32(vm, pop(vm));
            if (argc < 0) argc = 0;
            if (argc > 64) argc = 64;
            AsVal args[64];
            for (int i = 0; i < argc; i++) args[i] = pop(vm);
            AsVal ctor = (name.t == V_UNDEF || (name.t == V_STR && !name.s->len)) ? obj : as_get(vm, obj, as_tostr(vm, name));
            AsVal proto = as_get(vm, ctor, A_prototype);
            AsObj *o = as_obj_new(vm, OBJ_PLAIN, proto.t == V_OBJ ? proto.o : vm->object_proto);
            AsVal r = as_call(vm, ctor, as_objv(o), argc, args);
            push(vm, r.t == V_OBJ ? r : as_objv(o));
            break;
        }
        case 0x46: case 0x55: {   // Enumerate / Enumerate2
            AsVal o = pop(vm);
            if (op == 0x46) o = get_variable(vm, c, as_tostr(vm, o));
            push(vm, as_null());
            if (o.t == V_OBJ) {
                AsObj *ob = o.o;
                if (ob->type == OBJ_ARRAY)
                    for (int i = ob->nitems - 1; i >= 0; i--) { char b[16]; snprintf(b, sizeof b, "%d", i); push(vm, as_strv(as_str(vm, b))); }
                if (ob->type == OBJ_MC) {   // child clips are enumerable members (unnamed ones as "instanceN")
                    AsMc *mc = (AsMc *)ob;
                    for (int i = 0; i < mc->ndisp; i++) {
                        AsMc *k = mc->disp[i].mc;
                        if (!k || k->removed) continue;
                        if (!k->name) {
                            static unsigned next_instance;
                            char b[24];
                            snprintf(b, sizeof b, "instance%u", ++next_instance);
                            k->name = as_atom(vm, b);
                        }
                        if (!as_own(ob, k->name)) push(vm, as_strv(k->name));
                    }
                }
                for (AsObj *p = ob; p; p = p->proto)
                    for (int i = 0; i < p->props.cap; i++) {
                        AsProp *pr = &p->props.slots[i];
                        if (pr->key && pr->key != DELETED && !(pr->flags & PROP_DONTENUM)) push(vm, as_strv(pr->key));
                    }
            }
            break;
        }
        case 0x60: { int b = as_toint32(vm, pop(vm)), a = as_toint32(vm, pop(vm)); push(vm, as_num(a & b)); break; }
        case 0x61: { int b = as_toint32(vm, pop(vm)), a = as_toint32(vm, pop(vm)); push(vm, as_num(a | b)); break; }
        case 0x62: { int b = as_toint32(vm, pop(vm)), a = as_toint32(vm, pop(vm)); push(vm, as_num(a ^ b)); break; }
        case 0x63: { int b = as_toint32(vm, pop(vm)), a = as_toint32(vm, pop(vm)); push(vm, as_num((int32_t)((uint32_t)a << (b & 31)))); break; }
        case 0x64: { int b = as_toint32(vm, pop(vm)), a = as_toint32(vm, pop(vm)); push(vm, as_num(a >> (b & 31))); break; }
        case 0x65: { int b = as_toint32(vm, pop(vm)), a = as_toint32(vm, pop(vm)); push(vm, as_num((uint32_t)a >> (b & 31))); break; }
        case 0x81: if (c->target) as_mc_goto(vm, c->target, rd16(body), 0); break;   // GotoFrame (0-based)
        case 0x8C: if (c->target) {                                                      // GotoLabel
            int f = as_mc_frame_of(c->target, as_strv(as_str(vm, (const char *)body)));
            if (f >= 0) as_mc_goto(vm, c->target, f, 0);
        } break;
        case 0x9F: {   // GotoFrame2
            AsVal f = pop(vm);
            if (c->target) as_mc_goto_value(vm, c->target, f, body[0] & 1);
            break;
        }
        case 0x87: { AsVal v = pop(vm); push(vm, v); if (body[0] < c->nregs) c->regs[body[0]] = v; break; }
        case 0x88: c->pool = get_pool(vm, body, len); break;
        case 0x96: case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4:   // Push (A0-A4: the game's own)
            do_push(vm, c, body, len);
            break;
        case 0x99: next = next + (int16_t)rd16(body); break;                       // Jump
        case 0x9D: if (as_tobool(vm, pop(vm))) next = next + (int16_t)rd16(body); break;   // If
        case 0x9A: {   // GetURL2: loadMovie / unloadMovie
            AsVal target = pop(vm), url = pop(vm);
            as_mc_get_url(vm, c->target, url, target, body[0]);
            break;
        }
        case 0x83: break;   // GetURL (browser): ignored
        case 0x9B: case 0x8E: {   // DefineFunction / DefineFunction2
            AsStr *name;
            const uint8_t *p = body;
            // the code follows this action; find its size to skip it
            AsObj *f = define_function(vm, c, body, len, op == 0x8E, next, &name);
            (void)p;
            next += f->code_len;
            if (name) {
                AsObj *o = c->activation ? c->activation : &(c->target ? c->target : vm->root)->obj;
                as_set(vm, as_objv(o), name, as_objv(f));
            } else {
                push(vm, as_objv(f));
            }
            break;
        }
        default:
            as_warn(vm, "unhandled action 0x%02X", op);
            break;
        }
        c->pc = next;
    }
}

void as_run_block(AsVm *vm, const Movie *m, uint32_t off, uint32_t len, AsMc *target) {
    Ctx c;
    memset(&c, 0, sizeof c);
    c.code = m->data;
    c.movie = m;
    c.pc = off;
    c.end = off + len;
    c.regs = vm->global_regs;
    c.nregs = 4;
    c.target = target;
    c.this_ = target ? as_objv(&target->obj) : as_undef();
    c.scope = target ? scope_new(vm, &target->obj, NULL) : NULL;
    int sp0 = vm->sp;
    long long ops0 = vm->ops;
    run(vm, &c);
    if (AS_ENV("AS_DEBUG") && len < 64)
        as_warn(vm, "block @%u len %u: %lld ops, first op 0x%02x, sp %d", off, len, vm->ops - ops0, m->data[off], vm->sp);
    vm->sp = sp0;
}

// ---- GC ---------------------------------------------------------------------------

static void mark_gc(AsGc *g) { g->mark = 1; }

static void mark_scope(AsVm *vm, AsScope *s) {
    for (; s && !s->gc.mark; s = s->parent) {
        s->gc.mark = 1;
        if (s->obj) as_mark_obj(vm, s->obj);
    }
}

void as_mark_val(AsVm *vm, AsVal v) {
    if (v.t == V_STR) mark_gc(&v.s->gc);
    else if (v.t == V_OBJ) as_mark_obj(vm, v.o);
}

void as_mark_obj(AsVm *vm, AsObj *o) {
    if (!o || o->gc.mark) return;
    o->gc.mark = 1;
    if (o->proto) as_mark_obj(vm, o->proto);
    for (int i = 0; i < o->props.cap; i++) {
        AsProp *p = &o->props.slots[i];
        if (p->key && p->key != DELETED) as_mark_val(vm, p->v);
    }
    for (int i = 0; i < o->nitems; i++) as_mark_val(vm, o->items[i]);
    if (o->type == OBJ_FUNC) {
        mark_scope(vm, o->scope);
        if (o->target) as_mark_obj(vm, &o->target->obj);
    }
    if (o->type == OBJ_BOXED) as_mark_val(vm, o->boxed);
    if (o->bound_mc) as_mark_obj(vm, &o->bound_mc->obj);
    if (o->type == OBJ_MC) as_mc_mark(vm, (AsMc *)o);
}

static void free_gc(AsGc *g) {
    if (g->kind == GC_OBJ) {
        AsObj *o = (AsObj *)g;
        free(o->props.slots);
        free(o->items);
        free(o->param_names);
        free(o->param_regs);
        if (o->type == OBJ_MC) free(((AsMc *)o)->disp);
    }
    free(g);
}

// Sweeping is spread over frames: after a mark, the heap list is set aside and up to
// SWEEP_STEP of its objects are freed or kept each frame; new objects go on a fresh list.
#define SWEEP_STEP 1000
static AsGc *g_sweep;            // objects still to sweep (marked = live)
static size_t g_sweep_live;

static void sweep_some(AsVm *vm, size_t n) {
    while (g_sweep && n--) {
        AsGc *g = g_sweep;
        g_sweep = g->next;
        if (g->mark || g->permanent) {
            g->mark = 0;
            g->next = vm->heap;
            vm->heap = g;
            g_sweep_live++;
            vm->heap_count++;
        } else {
            free_gc(g);
        }
    }
    if (!g_sweep && g_sweep_live) {   // done: the next collection is due when the heap has doubled
        vm->gc_threshold = g_sweep_live * 2 > 20000 ? g_sweep_live * 2 : 20000;
        g_sweep_live = 0;
    }
}

void as_gc(AsVm *vm) {
    if (g_sweep) { sweep_some(vm, SWEEP_STEP); return; }
    if (vm->heap_count < vm->gc_threshold) return;
    extern double as_time_ms(void);
    double t0 = AS_ENV("AS_GCTIME") ? as_time_ms() : 0;
    as_mark_obj(vm, vm->global);
    if (vm->root) as_mark_obj(vm, &vm->root->obj);
    AsObj *protos[] = {vm->object_proto, vm->function_proto, vm->array_proto, vm->string_proto, vm->number_proto,
                       vm->boolean_proto, vm->mc_proto, vm->color_proto, vm->sound_proto};
    for (size_t i = 0; i < sizeof protos / sizeof *protos; i++) as_mark_obj(vm, protos[i]);
    for (int i = 0; i < 4; i++) as_mark_val(vm, vm->global_regs[i]);
    for (int i = 0; i < vm->sp; i++) as_mark_val(vm, vm->stack[i]);
    for (AsLoad *l = vm->loads; l; l = l->next) as_mark_obj(vm, &l->target->obj);
    for (int i = 0; i < vm->nqueue; i++) as_mark_obj(vm, &vm->queue[i]->obj);
    mark_gc(&vm->empty->gc);
    if (t0) as_warn(vm, "gc mark %.2f ms", as_time_ms() - t0);
    // survivors are counted back in as the sweep keeps them
    g_sweep = vm->heap;
    vm->heap = NULL;
    vm->heap_count = 0;
    vm->gc_threshold = (size_t)-1;   // no new collection until this sweep is done
    g_sweep_live = 0;
    sweep_some(vm, SWEEP_STEP);
}

// ---- setup ------------------------------------------------------------------------

AsVm *as_new(AsLoader loader) {
    AsVm *vm = calloc(1, sizeof(AsVm));
    vm->loader = loader;
    vm->gc_threshold = 20000;
    vm->rand_state = 12345;
    vm->empty = as_atom(vm, "");
    A_length = as_atom(vm, "length");
    A_prototype = as_atom(vm, "prototype");
    A_proto = as_atom(vm, "__proto__");
    A_constructor = as_atom(vm, "constructor");
    A_this = as_atom(vm, "this");
    A_root = as_atom(vm, "_root");
    A_global = as_atom(vm, "_global");
    A_parent = as_atom(vm, "_parent");
    A_level0 = as_atom(vm, "_level0");
    A_arguments = as_atom(vm, "arguments");
    A_callee = as_atom(vm, "callee");
    A_caller = as_atom(vm, "caller");
    vm->object_proto = as_obj_new(vm, OBJ_PLAIN, NULL);
    vm->function_proto = as_obj_new(vm, OBJ_PLAIN, vm->object_proto);
    vm->global = as_obj_new(vm, OBJ_PLAIN, vm->object_proto);
    as_lib_init(vm);
    return vm;
}

void as_free(AsVm *vm) {
    while (g_sweep) {
        AsGc *g = g_sweep;
        g_sweep = g->next;
        if (!g->permanent) free_gc(g);
    }
    while (vm->heap) {
        AsGc *g = vm->heap;
        vm->heap = g->next;
        if (!g->permanent) free_gc(g);
    }
    free(vm->queue);
    free(vm);
}
