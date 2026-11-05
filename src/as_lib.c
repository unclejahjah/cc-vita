// AVM1 built-ins: Object, Function, Array, String, Number, Boolean, Math, the
// global functions, MovieClip methods, Color and Sound.
#include "as.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARG(i) ((i) < argc ? argv[i] : as_undef())
#define THIS_OBJ (this_.t == V_OBJ ? this_.o : NULL)

static AsMc *this_mc(AsVal this_) {
    return this_.t == V_OBJ && this_.o->type == OBJ_MC ? (AsMc *)this_.o : NULL;
}

static AsStr *this_str(AsVm *vm, AsVal this_) {
    if (this_.t == V_OBJ && this_.o->type == OBJ_BOXED) return as_tostr(vm, this_.o->boxed);
    return as_tostr(vm, this_);
}

static AsObj *ctor(AsVm *vm, const char *name, AsNative fn, AsObj *proto) {
    AsObj *f = as_native(vm, fn);
    as_def(vm, f, "prototype", as_objv(proto));
    as_def(vm, proto, "constructor", as_objv(f));
    as_def(vm, vm->global, name, as_objv(f));
    return f;
}

// ---- Object / Function --------------------------------------------------------------

static AsVal obj_ctor(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (argc && argv[0].t == V_OBJ) return argv[0];
    if (this_.t == V_OBJ && this_.o->type == OBJ_PLAIN) return this_;
    return as_objv(as_obj_new(vm, OBJ_PLAIN, vm->object_proto));
}
static AsVal obj_tostring(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_; (void)argc; (void)argv;
    return as_strv(as_atom(vm, "[object Object]"));
}
static AsVal obj_valueof(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    if (this_.t == V_OBJ && this_.o->type == OBJ_BOXED) return this_.o->boxed;
    return this_;
}
static AsVal obj_hasown(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (!THIS_OBJ) return as_bool(0);
    return as_bool(as_own(THIS_OBJ, as_intern(vm, as_tostr(vm, ARG(0)))) != NULL);
}
static AsVal fn_call(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    return as_call(vm, this_, ARG(0), argc > 0 ? argc - 1 : 0, argv + 1);
}
static AsVal fn_apply(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsVal a = ARG(1);
    if (a.t == V_OBJ && a.o->type == OBJ_ARRAY) return as_call(vm, this_, ARG(0), a.o->nitems, a.o->items);
    return as_call(vm, this_, ARG(0), 0, NULL);
}

// ---- Array ------------------------------------------------------------------------

static AsVal arr_ctor(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    if (argc == 1 && argv[0].t == V_NUM) return as_objv(as_array_new(vm, (int)argv[0].n));
    AsObj *a = as_array_new(vm, argc);
    for (int i = 0; i < argc; i++) a->items[i] = argv[i];
    return as_objv(a);
}
static AsObj *this_arr(AsVal this_) { return this_.t == V_OBJ && this_.o->type == OBJ_ARRAY ? this_.o : NULL; }
static AsVal arr_push(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsObj *a = this_arr(this_);
    if (!a) return as_undef();
    for (int i = 0; i < argc; i++) as_array_push(vm, a, argv[i]);
    return as_num(a->nitems);
}
static AsVal arr_pop(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    AsObj *a = this_arr(this_);
    if (!a || !a->nitems) return as_undef();
    return a->items[--a->nitems];
}
static AsVal arr_shift(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    AsObj *a = this_arr(this_);
    if (!a || !a->nitems) return as_undef();
    AsVal v = a->items[0];
    memmove(a->items, a->items + 1, sizeof(AsVal) * --a->nitems);
    return v;
}
static AsVal arr_unshift(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsObj *a = this_arr(this_);
    if (!a) return as_undef();
    for (int i = 0; i < argc; i++) as_array_push(vm, a, as_undef());
    memmove(a->items + argc, a->items, sizeof(AsVal) * (a->nitems - argc));
    for (int i = 0; i < argc; i++) a->items[i] = argv[i];
    return as_num(a->nitems);
}
static AsVal arr_splice(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsObj *a = this_arr(this_);
    if (!a) return as_undef();
    int start = as_toint32(vm, ARG(0)), n = a->nitems;
    if (start < 0) start += n;
    if (start < 0) start = 0;
    if (start > n) start = n;
    int del = argc > 1 ? as_toint32(vm, argv[1]) : n - start;
    if (del < 0) del = 0;
    if (start + del > n) del = n - start;
    AsObj *removed = as_array_new(vm, del);
    for (int i = 0; i < del; i++) removed->items[i] = a->items[start + i];
    int ins = argc > 2 ? argc - 2 : 0, nn = n - del + ins;
    AsVal *items = malloc(sizeof(AsVal) * (nn ? nn : 1));
    memcpy(items, a->items, sizeof(AsVal) * start);
    for (int i = 0; i < ins; i++) items[start + i] = argv[2 + i];
    memcpy(items + start + ins, a->items + start + del, sizeof(AsVal) * (n - start - del));
    free(a->items);
    a->items = items;
    a->nitems = a->capitems = nn;
    return as_objv(removed);
}
static AsVal arr_slice(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsObj *a = this_arr(this_);
    if (!a) return as_undef();
    int n = a->nitems, s = argc > 0 ? as_toint32(vm, argv[0]) : 0, e = argc > 1 ? as_toint32(vm, argv[1]) : n;
    if (s < 0) s += n;
    if (e < 0) e += n;
    if (s < 0) s = 0;
    if (e > n) e = n;
    AsObj *r = as_array_new(vm, e > s ? e - s : 0);
    for (int i = s; i < e; i++) r->items[i - s] = a->items[i];
    return as_objv(r);
}
static AsVal arr_join(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsObj *a = this_arr(this_);
    if (!a) return as_strv(vm->empty);
    const char *sep = argc && argv[0].t != V_UNDEF ? as_tostr(vm, argv[0])->s : ",";
    size_t cap = 64, len = 0;
    char *buf = malloc(cap);
    buf[0] = 0;
    for (int i = 0; i < a->nitems; i++) {
        const char *s = as_tostr(vm, a->items[i])->s;
        size_t need = len + strlen(s) + strlen(sep) + 1;
        if (need > cap) { while (cap < need) cap *= 2; buf = realloc(buf, cap); }
        if (i) { strcpy(buf + len, sep); len += strlen(sep); }
        strcpy(buf + len, s);
        len += strlen(s);
    }
    AsStr *r = as_strn(vm, buf, len);
    free(buf);
    return as_strv(r);
}
static AsVal arr_tostring(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)argc; (void)argv;
    return arr_join(vm, this_, 0, NULL);
}
static AsVal arr_concat(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsObj *a = this_arr(this_);
    AsObj *r = as_array_new(vm, 0);
    if (a) for (int i = 0; i < a->nitems; i++) as_array_push(vm, r, a->items[i]);
    for (int k = 0; k < argc; k++) {
        if (argv[k].t == V_OBJ && argv[k].o->type == OBJ_ARRAY)
            for (int i = 0; i < argv[k].o->nitems; i++) as_array_push(vm, r, argv[k].o->items[i]);
        else as_array_push(vm, r, argv[k]);
    }
    return as_objv(r);
}
static AsVal arr_reverse(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    AsObj *a = this_arr(this_);
    if (!a) return as_undef();
    for (int i = 0, j = a->nitems - 1; i < j; i++, j--) { AsVal t = a->items[i]; a->items[i] = a->items[j]; a->items[j] = t; }
    return this_;
}
static AsVm *sort_vm;
static AsVal sort_fn;
static int sort_flags;
static int cmp_vals(const void *x, const void *y) {
    AsVal a = *(const AsVal *)x, b = *(const AsVal *)y;
    if (sort_fn.t == V_OBJ) {
        AsVal args[2] = {a, b};
        double r = as_tonum(sort_vm, as_call(sort_vm, sort_fn, as_undef(), 2, args));
        return r < 0 ? -1 : r > 0;
    }
    int r;
    if (sort_flags & 16) {   // NUMERIC
        double p = as_tonum(sort_vm, a), q2 = as_tonum(sort_vm, b);
        r = p < q2 ? -1 : p > q2;
    } else {
        r = (sort_flags & 1) ? strcasecmp(as_tostr(sort_vm, a)->s, as_tostr(sort_vm, b)->s)
                             : strcmp(as_tostr(sort_vm, a)->s, as_tostr(sort_vm, b)->s);
    }
    return (sort_flags & 2) ? -r : r;   // DESCENDING
}
static AsVal arr_sort(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsObj *a = this_arr(this_);
    if (!a) return as_undef();
    sort_vm = vm;
    sort_fn = argc && argv[0].t == V_OBJ ? argv[0] : as_undef();
    sort_flags = argc && argv[0].t == V_NUM ? (int)argv[0].n : argc > 1 ? as_toint32(vm, argv[1]) : 0;
    qsort(a->items, a->nitems, sizeof(AsVal), cmp_vals);
    return this_;
}

// ---- String -----------------------------------------------------------------------

static AsVal str_ctor(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsStr *s = argc ? as_tostr(vm, argv[0]) : vm->empty;
    if (this_.t == V_OBJ && this_.o->type == OBJ_PLAIN && this_.o->proto == vm->string_proto) {
        this_.o->type = OBJ_BOXED;
        this_.o->boxed = as_strv(s);
        return this_;
    }
    return as_strv(s);
}
static AsVal str_charat(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsStr *s = this_str(vm, this_);
    int i = as_toint32(vm, ARG(0));
    if (i < 0 || i >= (int)s->len) return as_strv(vm->empty);
    return as_strv(as_strn(vm, s->s + i, 1));
}
static AsVal str_charcodeat(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsStr *s = this_str(vm, this_);
    int i = as_toint32(vm, ARG(0));
    if (i < 0 || i >= (int)s->len) return as_num(NAN);
    return as_num((unsigned char)s->s[i]);
}
static AsVal str_fromcharcode(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    char buf[256];
    int n = 0;
    for (int i = 0; i < argc && n < 255; i++) buf[n++] = (char)as_toint32(vm, argv[i]);
    return as_strv(as_strn(vm, buf, n));
}
static AsVal str_indexof(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsStr *s = this_str(vm, this_), *f = as_tostr(vm, ARG(0));
    int from = argc > 1 ? as_toint32(vm, argv[1]) : 0;
    if (from < 0) from = 0;
    if (from > (int)s->len) return as_num(-1);
    const char *p = strstr(s->s + from, f->s);
    return as_num(p ? p - s->s : -1);
}
static AsVal str_lastindexof(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsStr *s = this_str(vm, this_), *f = as_tostr(vm, ARG(0));
    int last = -1;
    for (const char *p = s->s; (p = strstr(p, f->s)); p++) last = (int)(p - s->s);
    return as_num(last);
}
static AsVal str_substr(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsStr *s = this_str(vm, this_);
    int n = s->len, st = as_toint32(vm, ARG(0));
    if (st < 0) st += n;
    if (st < 0) st = 0;
    if (st > n) st = n;
    int len = argc > 1 && argv[1].t != V_UNDEF ? as_toint32(vm, argv[1]) : n - st;
    if (len < 0) len = 0;
    if (st + len > n) len = n - st;
    return as_strv(as_strn(vm, s->s + st, len));
}
static AsVal str_substring(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsStr *s = this_str(vm, this_);
    int n = s->len, a = as_toint32(vm, ARG(0)), b = argc > 1 && argv[1].t != V_UNDEF ? as_toint32(vm, argv[1]) : n;
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (a > n) a = n;
    if (b > n) b = n;
    if (a > b) { int t = a; a = b; b = t; }
    return as_strv(as_strn(vm, s->s + a, b - a));
}
static AsVal str_slice(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsStr *s = this_str(vm, this_);
    int n = s->len, a = as_toint32(vm, ARG(0)), b = argc > 1 && argv[1].t != V_UNDEF ? as_toint32(vm, argv[1]) : n;
    if (a < 0) a += n;
    if (b < 0) b += n;
    if (a < 0) a = 0;
    if (b > n) b = n;
    return as_strv(as_strn(vm, s->s + a, b > a ? b - a : 0));
}
static AsVal str_split(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsStr *s = this_str(vm, this_);
    AsObj *a = as_array_new(vm, 0);
    if (!argc || argv[0].t == V_UNDEF) { as_array_push(vm, a, as_strv(s)); return as_objv(a); }
    AsStr *sep = as_tostr(vm, argv[0]);
    if (!sep->len) {
        for (uint32_t i = 0; i < s->len; i++) as_array_push(vm, a, as_strv(as_strn(vm, s->s + i, 1)));
        return as_objv(a);
    }
    const char *p = s->s;
    for (;;) {
        const char *q = strstr(p, sep->s);
        if (!q) { as_array_push(vm, a, as_strv(as_str(vm, p))); break; }
        as_array_push(vm, a, as_strv(as_strn(vm, p, q - p)));
        p = q + sep->len;
    }
    return as_objv(a);
}
static AsVal str_case(AsVm *vm, AsVal this_, int up) {
    AsStr *s = this_str(vm, this_);
    AsStr *r = as_strn(vm, s->s, s->len);
    for (uint32_t i = 0; i < r->len; i++) r->s[i] = up ? toupper((unsigned char)r->s[i]) : tolower((unsigned char)r->s[i]);
    return as_strv(r);
}
static AsVal str_lower(AsVm *vm, AsVal this_, int argc, AsVal *argv) { (void)argc; (void)argv; return str_case(vm, this_, 0); }
static AsVal str_upper(AsVm *vm, AsVal this_, int argc, AsVal *argv) { (void)argc; (void)argv; return str_case(vm, this_, 1); }
static AsVal str_tostring(AsVm *vm, AsVal this_, int argc, AsVal *argv) { (void)argc; (void)argv; return as_strv(this_str(vm, this_)); }

// ---- Number / Boolean -------------------------------------------------------------

static AsVal num_ctor(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    double n = argc ? as_tonum(vm, argv[0]) : 0;
    if (this_.t == V_OBJ && this_.o->type == OBJ_PLAIN && this_.o->proto == vm->number_proto) {
        this_.o->type = OBJ_BOXED;
        this_.o->boxed = as_num(n);
        return this_;
    }
    return as_num(n);
}
static AsVal num_tostring(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    double n = as_tonum(vm, this_.t == V_OBJ && this_.o->type == OBJ_BOXED ? this_.o->boxed : this_);
    int radix = argc ? as_toint32(vm, argv[0]) : 10;
    if (radix == 10 || radix < 2 || radix > 36 || isnan(n)) return as_strv(as_tostr(vm, as_num(n)));
    char buf[72], *p = buf + 71;
    *p = 0;
    long long v = (long long)n;
    int neg = v < 0;
    if (neg) v = -v;
    do { int dgt = (int)(v % radix); *--p = (char)(dgt < 10 ? '0' + dgt : 'a' + dgt - 10); v /= radix; } while (v);
    if (neg) *--p = '-';
    return as_strv(as_str(vm, p));
}
static AsVal bool_ctor(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    return as_bool(argc && as_tobool(vm, argv[0]));
}

// ---- Math and globals -------------------------------------------------------------

#define MATH1(name, expr) \
    static AsVal name(AsVm *vm, AsVal this_, int argc, AsVal *argv) { \
        (void)this_; double x = as_tonum(vm, ARG(0)); return as_num(expr); }
MATH1(m_abs, fabs(x))
MATH1(m_sin, sin(x))
MATH1(m_cos, cos(x))
MATH1(m_tan, tan(x))
MATH1(m_asin, asin(x))
MATH1(m_acos, acos(x))
MATH1(m_atan, atan(x))
MATH1(m_sqrt, sqrt(x))
MATH1(m_floor, floor(x))
MATH1(m_ceil, ceil(x))
MATH1(m_round, floor(x + 0.5))
MATH1(m_exp, exp(x))
MATH1(m_log, log(x))
static AsVal m_atan2(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    return as_num(atan2(as_tonum(vm, ARG(0)), as_tonum(vm, ARG(1))));
}
static AsVal m_pow(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    return as_num(pow(as_tonum(vm, ARG(0)), as_tonum(vm, ARG(1))));
}
static AsVal m_min(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    double r = INFINITY;
    for (int i = 0; i < argc; i++) { double x = as_tonum(vm, argv[i]); if (x < r || isnan(x)) r = x; }
    return as_num(r);
}
static AsVal m_max(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    double r = -INFINITY;
    for (int i = 0; i < argc; i++) { double x = as_tonum(vm, argv[i]); if (x > r || isnan(x)) r = x; }
    return as_num(r);
}
static AsVal m_random(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_; (void)argc; (void)argv;
    vm->rand_state = vm->rand_state * 1103515245u + 12345u;
    return as_num(((vm->rand_state >> 8) & 0xFFFFFF) / 16777216.0);
}
static AsVal g_parseint(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    const char *s = as_tostr(vm, ARG(0))->s;
    int radix = argc > 1 ? as_toint32(vm, argv[1]) : 0;
    while (isspace((unsigned char)*s)) s++;
    char *end;
    long v = strtol(s, &end, radix);
    return end == s ? as_num(NAN) : as_num((double)v);
}
static AsVal g_parsefloat(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    const char *s = as_tostr(vm, ARG(0))->s;
    char *end;
    double v = strtod(s, &end);
    return end == s ? as_num(NAN) : as_num(v);
}
static AsVal g_isnan(AsVm *vm, AsVal this_, int argc, AsVal *argv) { (void)this_; return as_bool(isnan(as_tonum(vm, ARG(0)))); }
static AsVal g_isfinite(AsVm *vm, AsVal this_, int argc, AsVal *argv) { (void)this_; return as_bool(isfinite(as_tonum(vm, ARG(0)))); }
static AsVal g_string(AsVm *vm, AsVal this_, int argc, AsVal *argv) { (void)this_; return as_strv(as_tostr(vm, ARG(0))); }
static AsVal g_gettimer(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_; (void)argc; (void)argv;
    extern double as_time_ms(void);
    return as_num(floor(vm->time_ms ? vm->time_ms(vm) : as_time_ms()));
}

// ---- MovieClip --------------------------------------------------------------------

static AsVal mc_goto(AsVm *vm, AsVal this_, int argc, AsVal *argv, int play) {
    AsMc *mc = this_mc(this_);
    if (!mc) return as_undef();
    extern void as_mc_goto_value(AsVm *vm, AsMc *mc, AsVal frame, int play);
    if (argc) as_mc_goto_value(vm, mc, argv[0], play);
    return as_undef();
}
static AsVal mc_gotoandstop(AsVm *vm, AsVal this_, int argc, AsVal *argv) { return mc_goto(vm, this_, argc, argv, 0); }
static AsVal mc_gotoandplay(AsVm *vm, AsVal this_, int argc, AsVal *argv) { return mc_goto(vm, this_, argc, argv, 1); }
static AsVal mc_play(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    if (this_mc(this_)) this_mc(this_)->playing = 1;
    return as_undef();
}
static AsVal mc_stop(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    if (this_mc(this_)) this_mc(this_)->playing = 0;
    return as_undef();
}
static AsVal mc_nextframe(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)argc; (void)argv;
    AsMc *mc = this_mc(this_);
    if (mc) as_mc_goto(vm, mc, mc->frame + 1, 0);
    return as_undef();
}
static AsVal mc_prevframe(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)argc; (void)argv;
    AsMc *mc = this_mc(this_);
    if (mc && mc->frame > 0) as_mc_goto(vm, mc, mc->frame - 1, 0);
    return as_undef();
}
static void copy_init(AsVm *vm, AsMc *mc, AsVal init) {
    if (!mc || init.t != V_OBJ) return;
    AsObj *o = init.o;
    for (int i = 0; i < o->props.cap; i++) {
        AsProp *p = &o->props.slots[i];
        if (p->key && p->key != (AsStr *)1) as_set(vm, as_objv(&mc->obj), p->key, p->v);
    }
}
static AsVal mc_attach(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsMc *mc = this_mc(this_);
    if (!mc || argc < 3) return as_undef();
    AsMc *c = as_mc_attach(vm, mc, as_tostr(vm, argv[0]), as_tostr(vm, argv[1]), as_toint32(vm, argv[2]));
    copy_init(vm, c, ARG(3));
    return c ? as_objv(&c->obj) : as_undef();
}
static AsVal mc_create_empty(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsMc *mc = this_mc(this_);
    if (!mc || argc < 2) return as_undef();
    extern AsMc *as_mc_create_empty(AsVm *vm, AsMc *parent, AsStr *name, int depth);
    AsMc *c = as_mc_create_empty(vm, mc, as_tostr(vm, argv[0]), as_toint32(vm, argv[1]));
    return c ? as_objv(&c->obj) : as_undef();
}
static AsVal mc_duplicate(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsMc *mc = this_mc(this_);
    if (!mc || argc < 2) return as_undef();
    extern AsMc *as_mc_duplicate(AsVm *vm, AsMc *mc, AsStr *name, int depth);
    AsMc *c = as_mc_duplicate(vm, mc, as_tostr(vm, argv[0]), as_toint32(vm, argv[1]));
    copy_init(vm, c, ARG(2));
    return c ? as_objv(&c->obj) : as_undef();
}
static AsVal mc_remove(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)argc; (void)argv;
    AsMc *mc = this_mc(this_);
    if (mc && mc->depth >= 0) as_mc_remove(vm, mc);   // timeline clips can't be removed by script
    return as_undef();
}
static AsVal mc_swapdepths(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsMc *mc = this_mc(this_);
    if (!mc || !argc) return as_undef();
    extern void as_mc_swap_depths(AsVm *vm, AsMc *mc, AsVal target);
    as_mc_swap_depths(vm, mc, argv[0]);
    return as_undef();
}
static AsVal mc_getdepth(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    AsMc *mc = this_mc(this_);
    return mc ? as_num(mc->depth) : as_undef();
}
extern void render_mc_bounds(AsMc *mc, Mat m, float out[4]);
extern Mat as_mc_global_matrix(AsMc *mc);
static AsVal mc_getbounds(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsMc *mc = this_mc(this_);
    if (!mc) return as_undef();
    Mat m = MAT_IDENTITY;
    AsMc *space = argc ? as_mc_resolve_target(vm, mc, argv[0]) : mc;
    if (space && space != mc) {
        // bounds in the other clip's space: mc -> global -> space
        Mat g = as_mc_global_matrix(mc), s = as_mc_global_matrix(space);
        float det = s.a * s.d - s.b * s.c;
        if (det) {
            Mat inv = {s.d / det, -s.b / det, -s.c / det, s.a / det, (s.c * s.ty - s.d * s.tx) / det,
                       (s.b * s.tx - s.a * s.ty) / det};
            m = mat_mul(inv, g);
        }
    }
    float b[4];
    render_mc_bounds(mc, m, b);
    AsObj *o = as_obj_new(vm, OBJ_PLAIN, vm->object_proto);
    int ok = b[2] >= b[0];
    as_set(vm, as_objv(o), as_atom(vm, "xMin"), as_num(ok ? b[0] / 20 : 6710886.35));
    as_set(vm, as_objv(o), as_atom(vm, "yMin"), as_num(ok ? b[1] / 20 : 6710886.35));
    as_set(vm, as_objv(o), as_atom(vm, "xMax"), as_num(ok ? b[2] / 20 : 6710886.35));
    as_set(vm, as_objv(o), as_atom(vm, "yMax"), as_num(ok ? b[3] / 20 : 6710886.35));
    return as_objv(o);
}
// mc.setMask(clip): clip stops drawing and mc shows only inside it; setMask(null) undoes it.
// The players' waterbox does this in Flooded Temple: what is under the water line is cut off.
static AsVal mc_setmask(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsMc *mc = this_mc(this_);
    if (!mc) return as_bool(0);
    AsMc *mask = argc && argv[0].t == V_OBJ ? as_mc_resolve_target(vm, mc, argv[0]) : NULL;
    if (mask == mc) mask = NULL;
    if (mc->mask) mc->mask->is_mask = 0;
    mc->mask = mask;
    if (mask) mask->is_mask = 1;
    return as_bool(1);
}
static AsVal mc_hittest(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsMc *mc = this_mc(this_);
    if (!mc) return as_bool(0);
    Mat g = as_mc_global_matrix(mc);
    float b[4];
    render_mc_bounds(mc, g, b);
    if (argc >= 2 && argv[0].t != V_OBJ) {   // point in global pixels
        float x = (float)as_tonum(vm, argv[0]) * 20, y = (float)as_tonum(vm, argv[1]) * 20;
        return as_bool(x >= b[0] && x <= b[2] && y >= b[1] && y <= b[3]);
    }
    AsMc *o = argc ? as_mc_resolve_target(vm, mc, argv[0]) : NULL;
    if (!o) return as_bool(0);
    float c[4];
    render_mc_bounds(o, as_mc_global_matrix(o), c);
    return as_bool(b[0] <= c[2] && c[0] <= b[2] && b[1] <= c[3] && c[1] <= b[3]);
}
static AsVal point_xform(AsVm *vm, AsVal this_, AsVal pt, int to_global) {
    AsMc *mc = this_mc(this_);
    if (!mc || pt.t != V_OBJ) return as_undef();
    AsStr *kx = as_atom(vm, "x"), *ky = as_atom(vm, "y");
    float x = (float)as_tonum(vm, as_get(vm, pt, kx)) * 20, y = (float)as_tonum(vm, as_get(vm, pt, ky)) * 20;
    Mat g = as_mc_global_matrix(mc);
    float nx, ny;
    if (to_global) {
        nx = g.a * x + g.c * y + g.tx;
        ny = g.b * x + g.d * y + g.ty;
    } else {
        float det = g.a * g.d - g.b * g.c;
        if (!det) return as_undef();
        float px = x - g.tx, py = y - g.ty;
        nx = (g.d * px - g.c * py) / det;
        ny = (-g.b * px + g.a * py) / det;
    }
    as_set(vm, pt, kx, as_num(nx / 20));
    as_set(vm, pt, ky, as_num(ny / 20));
    return as_undef();
}
static AsVal mc_localtoglobal(AsVm *vm, AsVal this_, int argc, AsVal *argv) { return point_xform(vm, this_, ARG(0), 1); }
static AsVal mc_globaltolocal(AsVm *vm, AsVal this_, int argc, AsVal *argv) { return point_xform(vm, this_, ARG(0), 0); }
static AsVal mc_loadmovie(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    extern void as_mc_get_url(AsVm *vm, AsMc *base, AsVal url, AsVal target, int flags);
    AsMc *mc = this_mc(this_);
    if (mc) as_mc_get_url(vm, mc, ARG(0), this_, 0x40);
    return as_undef();
}
static AsVal mc_unloadmovie(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)argc; (void)argv;
    extern void as_mc_get_url(AsVm *vm, AsMc *base, AsVal url, AsVal target, int flags);
    AsMc *mc = this_mc(this_);
    if (mc) as_mc_get_url(vm, mc, as_strv(vm->empty), this_, 0x40);
    return as_undef();
}
static AsVal mc_bytes(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    AsMc *mc = this_mc(this_);
    return as_num(mc && mc->movie ? (double)mc->movie->len : 0);
}
static AsVal noop(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    return as_undef();
}

// ---- Color ------------------------------------------------------------------------

static AsVal color_ctor(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (this_.t != V_OBJ) return as_undef();
    this_.o->type = OBJ_COLOR;
    this_.o->bound_mc = argc ? as_mc_resolve_target(vm, NULL, argv[0]) : NULL;
    return this_;
}
static AsMc *color_mc(AsVal this_) { return this_.t == V_OBJ && this_.o->type == OBJ_COLOR ? this_.o->bound_mc : NULL; }
static AsVal color_setrgb(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsMc *mc = color_mc(this_);
    if (!mc) return as_undef();
    int rgb = as_toint32(vm, ARG(0));
    mc->cx.mul[0] = mc->cx.mul[1] = mc->cx.mul[2] = 0;
    mc->cx.add[0] = (rgb >> 16) & 255;
    mc->cx.add[1] = (rgb >> 8) & 255;
    mc->cx.add[2] = rgb & 255;
    return as_undef();
}
static AsVal color_getrgb(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    AsMc *mc = color_mc(this_);
    if (!mc) return as_num(0);
    return as_num(((int)mc->cx.add[0] << 16) | ((int)mc->cx.add[1] << 8) | (int)mc->cx.add[2]);
}
static AsVal color_settransform(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsMc *mc = color_mc(this_);
    AsVal t = ARG(0);
    if (!mc || t.t != V_OBJ) return as_undef();
    static const char *mul[4] = {"ra", "ga", "ba", "aa"}, *add[4] = {"rb", "gb", "bb", "ab"};
    for (int i = 0; i < 4; i++) {
        AsVal m = as_get(vm, t, as_atom(vm, mul[i])), a = as_get(vm, t, as_atom(vm, add[i]));
        if (m.t != V_UNDEF) mc->cx.mul[i] = (float)(as_tonum(vm, m) / 100);
        if (a.t != V_UNDEF) mc->cx.add[i] = (float)as_tonum(vm, a);
    }
    return as_undef();
}
static AsVal color_gettransform(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)argc; (void)argv;
    AsMc *mc = color_mc(this_);
    AsObj *o = as_obj_new(vm, OBJ_PLAIN, vm->object_proto);
    if (!mc) return as_objv(o);
    static const char *mul[4] = {"ra", "ga", "ba", "aa"}, *add[4] = {"rb", "gb", "bb", "ab"};
    for (int i = 0; i < 4; i++) {
        as_set(vm, as_objv(o), as_atom(vm, mul[i]), as_num(mc->cx.mul[i] * 100));
        as_set(vm, as_objv(o), as_atom(vm, add[i]), as_num(mc->cx.add[i]));
    }
    return as_objv(o);
}

// ---- Sound: attached sounds played by the host (vm->sound_start) ----------------------
// Each Sound keeps its linkage name, volume and pan in hidden properties.

static AsStr *A_link, *A_vol, *A_pan;

static AsVal sound_ctor(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)argc; (void)argv;
    if (this_.t == V_OBJ) {
        this_.o->type = OBJ_SOUND;
        this_.o->bound_mc = argc ? as_mc_resolve_target(vm, NULL, argv[0]) : NULL;
    }
    return this_;
}

static double sound_num(AsObj *o, AsStr *key, double dflt) {
    AsProp *p = as_own(o, key);
    return p && p->v.t == V_NUM ? p->v.n : dflt;
}

static void sound_set(AsVm *vm, AsObj *o, AsStr *key, AsVal v) {
    AsProp *p = as_own(o, key);
    if (p) { p->v = v; return; }
    as_set(vm, as_objv(o), key, v);
    p = as_own(o, key);
    if (p) p->flags |= PROP_DONTENUM;
}

static AsVal sound_attach(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (this_.t == V_OBJ && argc) sound_set(vm, this_.o, A_link, as_strv(as_tostr(vm, argv[0])));
    return as_undef();
}

// start(secondOffset, loops)
static AsVal sound_start(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (this_.t != V_OBJ || !vm->sound_start) return as_undef();
    AsProp *link = as_own(this_.o, A_link);
    if (!link || link->v.t != V_STR) return as_undef();
    int loops = argc > 1 ? as_toint32(vm, argv[1]) : 0;
    vm->sound_start(vm, link->v.s->s, (float)sound_num(this_.o, A_vol, 100) / 100, (float)sound_num(this_.o, A_pan, 0) / 100,
                    loops, this_.o);
    return as_undef();
}

static AsVal sound_stop(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)argc; (void)argv;
    if (this_.t == V_OBJ && vm->sound_stop) vm->sound_stop(vm, this_.o);
    return as_undef();
}

static AsVal sound_setvolume(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (this_.t == V_OBJ && argc) sound_set(vm, this_.o, A_vol, as_num(as_tonum(vm, argv[0])));
    return as_undef();
}

static AsVal sound_setpan(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (this_.t == V_OBJ && argc) sound_set(vm, this_.o, A_pan, as_num(as_tonum(vm, argv[0])));
    return as_undef();
}

static AsVal sound_getvolume(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    return as_num(this_.t == V_OBJ ? sound_num(this_.o, A_vol, 100) : 100);
}

static AsVal sound_getpan(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)argc; (void)argv;
    return as_num(this_.t == V_OBJ ? sound_num(this_.o, A_pan, 0) : 0);
}

// ---- setup ------------------------------------------------------------------------

void as_lib_init(AsVm *vm) {
    AsObj *g = vm->global;
    // Object and Function
    AsObj *op = vm->object_proto;
    ctor(vm, "Object", obj_ctor, op);
    as_def_fn(vm, op, "toString", obj_tostring);
    as_def_fn(vm, op, "valueOf", obj_valueof);
    as_def_fn(vm, op, "hasOwnProperty", obj_hasown);
    AsObj *fp = vm->function_proto;
    ctor(vm, "Function", obj_ctor, fp);
    as_def_fn(vm, fp, "call", fn_call);
    as_def_fn(vm, fp, "apply", fn_apply);
    // Array
    vm->array_proto = as_obj_new(vm, OBJ_PLAIN, op);
    AsObj *ap = vm->array_proto;
    ctor(vm, "Array", arr_ctor, ap);
    as_def_fn(vm, ap, "push", arr_push);
    as_def_fn(vm, ap, "pop", arr_pop);
    as_def_fn(vm, ap, "shift", arr_shift);
    as_def_fn(vm, ap, "unshift", arr_unshift);
    as_def_fn(vm, ap, "splice", arr_splice);
    as_def_fn(vm, ap, "slice", arr_slice);
    as_def_fn(vm, ap, "join", arr_join);
    as_def_fn(vm, ap, "toString", arr_tostring);
    as_def_fn(vm, ap, "concat", arr_concat);
    as_def_fn(vm, ap, "reverse", arr_reverse);
    as_def_fn(vm, ap, "sort", arr_sort);
    // String
    vm->string_proto = as_obj_new(vm, OBJ_PLAIN, op);
    AsObj *sp = vm->string_proto;
    AsObj *sc = ctor(vm, "String", str_ctor, sp);
    as_def_fn(vm, sc, "fromCharCode", str_fromcharcode);
    as_def_fn(vm, sp, "charAt", str_charat);
    as_def_fn(vm, sp, "charCodeAt", str_charcodeat);
    as_def_fn(vm, sp, "indexOf", str_indexof);
    as_def_fn(vm, sp, "lastIndexOf", str_lastindexof);
    as_def_fn(vm, sp, "substr", str_substr);
    as_def_fn(vm, sp, "substring", str_substring);
    as_def_fn(vm, sp, "slice", str_slice);
    as_def_fn(vm, sp, "split", str_split);
    as_def_fn(vm, sp, "toLowerCase", str_lower);
    as_def_fn(vm, sp, "toUpperCase", str_upper);
    as_def_fn(vm, sp, "toString", str_tostring);
    as_def_fn(vm, sp, "valueOf", str_tostring);
    // Number, Boolean
    vm->number_proto = as_obj_new(vm, OBJ_PLAIN, op);
    AsObj *nc = ctor(vm, "Number", num_ctor, vm->number_proto);
    as_def_fn(vm, vm->number_proto, "toString", num_tostring);
    as_def_fn(vm, vm->number_proto, "valueOf", obj_valueof);
    as_def(vm, nc, "MAX_VALUE", as_num(1.79769313486231e308));
    as_def(vm, nc, "MIN_VALUE", as_num(4.94065645841247e-324));
    as_def(vm, nc, "NaN", as_num(NAN));
    vm->boolean_proto = as_obj_new(vm, OBJ_PLAIN, op);
    ctor(vm, "Boolean", bool_ctor, vm->boolean_proto);
    // Math
    AsObj *math = as_obj_new(vm, OBJ_PLAIN, op);
    as_def(vm, g, "Math", as_objv(math));
    as_def_fn(vm, math, "abs", m_abs);
    as_def_fn(vm, math, "sin", m_sin);
    as_def_fn(vm, math, "cos", m_cos);
    as_def_fn(vm, math, "tan", m_tan);
    as_def_fn(vm, math, "asin", m_asin);
    as_def_fn(vm, math, "acos", m_acos);
    as_def_fn(vm, math, "atan", m_atan);
    as_def_fn(vm, math, "atan2", m_atan2);
    as_def_fn(vm, math, "sqrt", m_sqrt);
    as_def_fn(vm, math, "floor", m_floor);
    as_def_fn(vm, math, "ceil", m_ceil);
    as_def_fn(vm, math, "round", m_round);
    as_def_fn(vm, math, "exp", m_exp);
    as_def_fn(vm, math, "log", m_log);
    as_def_fn(vm, math, "pow", m_pow);
    as_def_fn(vm, math, "min", m_min);
    as_def_fn(vm, math, "max", m_max);
    as_def_fn(vm, math, "random", m_random);
    as_def(vm, math, "PI", as_num(3.141592653589793));
    as_def(vm, math, "E", as_num(2.718281828459045));
    as_def(vm, math, "SQRT2", as_num(1.4142135623730951));
    // globals
    as_def_fn(vm, g, "parseInt", g_parseint);
    as_def_fn(vm, g, "parseFloat", g_parsefloat);
    as_def_fn(vm, g, "isNaN", g_isnan);
    as_def_fn(vm, g, "isFinite", g_isfinite);
    as_def_fn(vm, g, "String", g_string);
    as_def_fn(vm, g, "getTimer", g_gettimer);
    as_def(vm, g, "NaN", as_num(NAN));
    as_def(vm, g, "Infinity", as_num(INFINITY));
    // MovieClip
    vm->mc_proto = as_obj_new(vm, OBJ_PLAIN, op);
    AsObj *mp = vm->mc_proto;
    ctor(vm, "MovieClip", noop, mp);
    as_def_fn(vm, mp, "gotoAndStop", mc_gotoandstop);
    as_def_fn(vm, mp, "gotoAndPlay", mc_gotoandplay);
    as_def_fn(vm, mp, "play", mc_play);
    as_def_fn(vm, mp, "stop", mc_stop);
    as_def_fn(vm, mp, "nextFrame", mc_nextframe);
    as_def_fn(vm, mp, "prevFrame", mc_prevframe);
    as_def_fn(vm, mp, "attachMovie", mc_attach);
    as_def_fn(vm, mp, "createEmptyMovieClip", mc_create_empty);
    as_def_fn(vm, mp, "duplicateMovieClip", mc_duplicate);
    as_def_fn(vm, mp, "removeMovieClip", mc_remove);
    as_def_fn(vm, mp, "swapDepths", mc_swapdepths);
    as_def_fn(vm, mp, "getDepth", mc_getdepth);
    as_def_fn(vm, mp, "getBounds", mc_getbounds);
    as_def_fn(vm, mp, "hitTest", mc_hittest);
    as_def_fn(vm, mp, "localToGlobal", mc_localtoglobal);
    as_def_fn(vm, mp, "globalToLocal", mc_globaltolocal);
    as_def_fn(vm, mp, "loadMovie", mc_loadmovie);
    as_def_fn(vm, mp, "unloadMovie", mc_unloadmovie);
    as_def_fn(vm, mp, "getBytesLoaded", mc_bytes);
    as_def_fn(vm, mp, "getBytesTotal", mc_bytes);
    as_def_fn(vm, mp, "setMask", mc_setmask);
    as_def_fn(vm, mp, "startDrag", noop);
    as_def_fn(vm, mp, "stopDrag", noop);
    // Color
    vm->color_proto = as_obj_new(vm, OBJ_PLAIN, op);
    ctor(vm, "Color", color_ctor, vm->color_proto);
    as_def_fn(vm, vm->color_proto, "setRGB", color_setrgb);
    as_def_fn(vm, vm->color_proto, "getRGB", color_getrgb);
    as_def_fn(vm, vm->color_proto, "setTransform", color_settransform);
    as_def_fn(vm, vm->color_proto, "getTransform", color_gettransform);
    // Sound
    vm->sound_proto = as_obj_new(vm, OBJ_PLAIN, op);
    ctor(vm, "Sound", sound_ctor, vm->sound_proto);
    A_link = as_atom(vm, "__linkage");
    A_vol = as_atom(vm, "__volume");
    A_pan = as_atom(vm, "__pan");
    as_def_fn(vm, vm->sound_proto, "attachSound", sound_attach);
    as_def_fn(vm, vm->sound_proto, "start", sound_start);
    as_def_fn(vm, vm->sound_proto, "stop", sound_stop);
    as_def_fn(vm, vm->sound_proto, "setVolume", sound_setvolume);
    as_def_fn(vm, vm->sound_proto, "setPan", sound_setpan);
    as_def_fn(vm, vm->sound_proto, "getVolume", sound_getvolume);
    as_def_fn(vm, vm->sound_proto, "getPan", sound_getpan);
    as_def_fn(vm, vm->sound_proto, "setTransform", noop);
    as_def_fn(vm, vm->sound_proto, "loadSound", noop);
}
