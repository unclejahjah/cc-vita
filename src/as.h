// A small ActionScript 1 (AVM1) engine for running Castle Crashers' own scripts.
//
// The game's SWFs are version 6, so this follows SWF6 rules: identifiers are
// case-insensitive, undefined converts to 0 and "", and so on. Besides the
// standard actions the game uses 0x70 (logical not) and 0xA0-0xA4 (Push with the
// standard encoding).
//
// Memory: every object and dynamic string is on one heap list and collected by
// mark and sweep. Collection only runs between frames (as_gc), when no script is
// running, so nothing on the C stack needs to be tracked. Names (property keys,
// constant pool entries) are interned atoms that live for the whole session.
#ifndef CC_AS_H
#define CC_AS_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "swf.h"

// A debug switch from the environment (PC test runs), looked up once per place it's used:
// these sit in hot paths, and the answer never changes during a run.
#define AS_ENV(name) ({ static const char *as_env_v_; static int as_env_got_; \
    if (!as_env_got_) { as_env_v_ = getenv(name); as_env_got_ = 1; } as_env_v_; })

typedef struct AsGc AsGc;
typedef struct AsStr AsStr;
typedef struct AsObj AsObj;
typedef struct AsVm AsVm;
typedef struct AsMc AsMc;

enum { V_UNDEF, V_NULL, V_BOOL, V_NUM, V_STR, V_OBJ };

typedef struct {
    uint8_t t;
    union {
        double n;
        int b;
        AsStr *s;
        AsObj *o;
    };
} AsVal;

struct AsGc {
    AsGc *next;
    uint8_t kind;              // GC_STR, GC_OBJ, GC_SCOPE
    uint8_t mark;
    uint8_t permanent;         // atoms and pools: never collected
};

struct AsStr {
    AsGc gc;
    uint32_t len;
    uint32_t hash;             // of the lowercased text (names are case-insensitive)
    AsStr *atom;               // the interned key for this text, once looked up
    char s[];                  // NUL-terminated
};

// ---- properties ----

enum { PROP_DONTENUM = 1, PROP_READONLY = 2, PROP_DONTDELETE = 4 };

typedef struct {
    AsStr *key;                // atom; NULL = empty slot, (AsStr *)1 = deleted
    AsVal v;
    uint8_t flags;
} AsProp;

typedef struct {
    AsProp *slots;
    int cap, count, used;      // used includes deleted slots
} AsProps;

// ---- objects ----

enum {
    OBJ_PLAIN, OBJ_ARRAY, OBJ_FUNC, OBJ_MC, OBJ_BOXED,   // Boolean/Number/String wrapper
    OBJ_COLOR, OBJ_SOUND, OBJ_ACTIVATION,
};

typedef struct AsScope AsScope;

typedef AsVal (*AsNative)(AsVm *vm, AsVal this_, int argc, AsVal *argv);

typedef struct {
    int n;
    AsStr **s;                 // atoms
} AsPool;

struct AsObj {
    AsGc gc;
    uint8_t type;
    AsObj *proto;              // __proto__
    AsProps props;
    // OBJ_ARRAY
    AsVal *items;
    int nitems, capitems;
    // OBJ_FUNC
    AsNative native;
    const Movie *code_movie;   // bytecode lives in code_movie->data
    uint32_t code_off, code_len;
    uint8_t v2;                // DefineFunction2
    uint8_t nregs;
    uint16_t flags2;           // DefineFunction2 preload/suppress flags
    int nparams;
    AsStr **param_names;       // atoms
    uint8_t *param_regs;       // DefineFunction2: register for each parameter (0 = by name)
    const AsPool *pool;        // constant pool when defined
    AsScope *scope;            // scope chain when defined
    AsMc *target;              // timeline the function was defined on
    AsStr *fname;              // name it was defined with (for messages)
    // OBJ_BOXED
    AsVal boxed;
    // OBJ_COLOR / OBJ_SOUND
    AsMc *bound_mc;
};

struct AsScope {
    AsGc gc;
    AsObj *obj;
    AsScope *parent;
};

// ---- movie clips ----

// What sits at one depth of a clip's display list.
typedef struct {
    int depth;
    uint8_t kind;              // DISP_SHAPE, DISP_MC, DISP_TEXT
    uint16_t cid;
    uint16_t clip;             // mask: clips depths up to this one
    uint32_t uid;              // timeline instance id (0 = created by script)
    Mat m;                     // for shapes (clips keep their own)
    CXform cx;
    AsMc *mc;                  // DISP_MC
} AsDisp;

enum { DISP_SHAPE, DISP_MC, DISP_TEXT };

struct AsMc {
    AsObj obj;                 // variables live in obj.props
    const Movie *movie;
    const Sprite *def;         // its timeline (a sprite, or a movie root)
    AsMc *parent;
    AsStr *name;               // atom; NULL for unnamed
    int depth;
    int frame;                 // 0-based current frame
    uint8_t playing;
    uint8_t removed;
    uint8_t scripted;          // a script set its transform: the timeline stops moving it
    uint8_t visible;
    uint8_t loaded;            // onClipEvent(load) done
    Mat m;                     // placement matrix
    CXform cx;                 // placement colour transform (and Color objects)
    float alpha;               // _alpha, 0..100
    float rotation, xscale, yscale;   // decomposed, kept in step with m
    uint32_t uid;
    uint32_t events_off, events_len;  // onClipEvent handlers
    uint16_t clip;             // mask depth for its own placement
    AsDisp *disp;              // sorted by depth
    int ndisp, capdisp;
    int pending_frame;         // frame whose scripts are queued, -1 none
    uint32_t born;             // vm->tick it was created on: it doesn't advance until the next one
    const EditText *text;      // a text field (its contents are the "text" variable)
};

// ---- the VM ----

#define AS_MAX_STACK 4096

typedef struct AsLoad {
    struct AsLoad *next;
    AsMc *target;
    char url[256];
} AsLoad;

// File access for loadMovie: returns a parsed movie, or NULL.
typedef Movie *(*AsLoader)(AsVm *vm, const char *url);

struct AsVm {
    AsGc *heap;
    size_t heap_count, gc_threshold;
    AsVal stack[AS_MAX_STACK];
    int sp;
    AsVal global_regs[4];
    AsObj *global;              // _global
    AsMc *root;                 // _level0
    AsObj *object_proto, *function_proto, *array_proto, *string_proto, *number_proto,
          *boolean_proto, *mc_proto, *color_proto, *sound_proto;
    AsStr *empty;               // ""
    AsLoad *loads;              // loadMovie requests, done at the end of the frame
    AsLoader loader;
    void *host;                 // for the host's own use
    int depth;                  // call depth (runaway recursion guard)
    AsObj *cur_fn;              // function running now (for messages), NULL on a timeline
    long long ops;              // actions executed (profiling)
    uint32_t tick;              // frames run so far
    uint32_t rand_state;
    void (*trace)(AsVm *vm, const char *msg);
    void (*loaded)(AsVm *vm, AsMc *target);    // after a loadMovie finished (its first frame has run)
    void (*warn)(AsVm *vm, const char *msg);   // missing functions and other surprises
    // Sound objects: play an attached sound (linkage name), stop what one object started
    void (*sound_start)(AsVm *vm, const char *linkage, float volume, float pan, int loops, const void *owner);
    void (*sound_stop)(AsVm *vm, const void *owner);
    // each tick, after the frame scripts ran and before onEnterFrame (host work that needs
    // freshly placed clips to have run their first frame)
    void (*after_frame_scripts)(AsVm *vm);
    // native versions of hot script functions, by name (as_override)
    struct { AsStr *name; AsNative fn; } overrides[32];
    int noverrides;
    // variables whose value the host fixes (as_pin): scripts read it, their writes are ignored
    struct { AsStr *name, *only_in; AsVal v; } pinned[16];
    int npinned;
    // queued frame scripts for this tick: (clip, frame) in order
    AsMc **queue;
    int nqueue, capqueue;
};

// lifecycle
AsVm *as_new(AsLoader loader);
void as_free(AsVm *vm);
void as_gc(AsVm *vm);           // only between frames

// values
static inline AsVal as_undef(void) { AsVal v; v.t = V_UNDEF; v.n = 0; return v; }
static inline AsVal as_null(void) { AsVal v; v.t = V_NULL; v.n = 0; return v; }
static inline AsVal as_bool(int b) { AsVal v; v.t = V_BOOL; v.n = 0; v.b = b != 0; return v; }
static inline AsVal as_num(double n) { AsVal v; v.t = V_NUM; v.n = n; return v; }
static inline AsVal as_strv(AsStr *s) { AsVal v; v.t = V_STR; v.n = 0; v.s = s; return v; }
static inline AsVal as_objv(AsObj *o) { AsVal v; v.n = 0; if (o) { v.t = V_OBJ; v.o = o; } else v.t = V_NULL; return v; }

AsStr *as_str(AsVm *vm, const char *s);              // a GC'd string
AsStr *as_strn(AsVm *vm, const char *s, size_t n);
AsStr *as_atom(AsVm *vm, const char *s);             // interned, permanent
AsStr *as_intern(AsVm *vm, AsStr *s);                // atom for any string
double as_tonum(AsVm *vm, AsVal v);
AsStr *as_tostr(AsVm *vm, AsVal v);
int as_tobool(AsVm *vm, AsVal v);
AsObj *as_toobj(AsVm *vm, AsVal v);
int as_toint32(AsVm *vm, AsVal v);

// objects
AsObj *as_obj_new(AsVm *vm, int type, AsObj *proto);
AsObj *as_array_new(AsVm *vm, int n);
void as_array_push(AsVm *vm, AsObj *a, AsVal v);
AsObj *as_native(AsVm *vm, AsNative fn);
AsVal as_get(AsVm *vm, AsVal obj, AsStr *key);       // key: any string
void as_set(AsVm *vm, AsVal obj, AsStr *key, AsVal v);
AsProp *as_own(AsObj *o, AsStr *atom);
void as_def(AsVm *vm, AsObj *o, const char *name, AsVal v);   // define with DONTENUM
void as_def_fn(AsVm *vm, AsObj *o, const char *name, AsNative fn);
AsVal as_call(AsVm *vm, AsVal fn, AsVal this_, int argc, AsVal *argv);
AsVal as_call_method(AsVm *vm, AsVal obj, const char *name, int argc, AsVal *argv);

// running scripts
void as_run_block(AsVm *vm, const Movie *m, uint32_t off, uint32_t len, AsMc *target);

// From now on, script functions defined with this name run fn instead of their bytecode.
void as_override(AsVm *vm, const char *name, AsNative fn);
// A variable that always reads v by name (only primitive values: they aren't GC roots).
// With only_in, just inside the script function of that name (and writes still happen).
void as_pin(AsVm *vm, const char *name, AsVal v, const char *only_in);

// profiling: time and actions per function, worst first (exclusive of callees)
void as_prof_enable(int on);
void as_prof_report(int top, void (*out)(const char *line));

// movie clips (as_mc.c)
AsMc *as_mc_new(AsVm *vm, const Movie *m, const Sprite *def, AsMc *parent, int depth);
void as_mc_load_movie(AsVm *vm, AsMc *mc, Movie *m);  // replace its contents with a whole SWF
void as_mc_goto(AsVm *vm, AsMc *mc, int frame, int play);
int as_mc_frame_of(AsMc *mc, AsVal label);            // 0-based, -1 if none
void as_mc_remove(AsVm *vm, AsMc *mc);
AsMc *as_mc_child(AsMc *mc, AsStr *name);
AsMc *as_mc_attach(AsVm *vm, AsMc *mc, AsStr *linkage, AsStr *name, int depth);
void as_tick(AsVm *vm);                               // one frame of the whole tree
AsVal as_mc_get_prop(AsVm *vm, AsMc *mc, int index);
void as_mc_set_prop(AsVm *vm, AsMc *mc, int index, AsVal v);
int as_mc_prop_index(const char *name);               // "_x" -> 0, ... ; -1 if not a clip property
void as_mc_mark(AsVm *vm, AsMc *mc);
AsVal as_resolve_path(AsVm *vm, AsMc *from, const char *path, int *found);
AsMc *as_mc_resolve_target(AsVm *vm, AsMc *base, AsVal target);   // clip, path string, or "" for base

// library (as_lib.c)
void as_lib_init(AsVm *vm);

// internal helpers shared between files
void as_mark_val(AsVm *vm, AsVal v);
void as_mark_obj(AsVm *vm, AsObj *o);
void as_queue_frame(AsVm *vm, AsMc *mc);
void as_warn(AsVm *vm, const char *fmt, ...);

#endif
