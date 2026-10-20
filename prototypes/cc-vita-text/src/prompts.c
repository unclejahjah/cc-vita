// PlayStation button prompts: see prompts.h.
//
// Glyphs are drawn with signed distance functions (circles, capsules,
// rounded boxes, triangles), which gives smooth anti-aliased edges at any
// cell size with no image files.
#include "prompts.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "strtab.h"

static PromptGlyph g_action[PA_COUNT];

void prompts_bind(PromptAction a, PromptGlyph g) {
    if ((unsigned)a < PA_COUNT) g_action[a] = g;
}

void prompts_init(void) {
    prompts_bind(PA_JUMP, PG_CROSS);
    prompts_bind(PA_ACCEPT, PG_CROSS);
    prompts_bind(PA_LIGHT, PG_SQUARE);
    prompts_bind(PA_HEAVY, PG_TRIANGLE);
    prompts_bind(PA_BLOCK, PG_R);
    prompts_bind(PA_MAGIC, PG_L);
    prompts_bind(PA_BACK, PG_CIRCLE);
    prompts_bind(PA_PROJECTILE, PG_CIRCLE);
    prompts_bind(PA_PAUSE, PG_START);
}

// ---- Token names ----

typedef struct { const char *name; int glyph; int action; } Token;

static const Token k_tokens[] = {
    {"cross", PG_CROSS, -1},       {"circle", PG_CIRCLE, -1},
    {"square", PG_SQUARE, -1},     {"triangle", PG_TRIANGLE, -1},
    {"l", PG_L, -1},               {"r", PG_R, -1},
    {"start", PG_START, -1},       {"select", PG_SELECT, -1},
    {"dpad", PG_DPAD, -1},         {"up", PG_DPAD_UP, -1},
    {"down", PG_DPAD_DOWN, -1},    {"left", PG_DPAD_LEFT, -1},
    {"right", PG_DPAD_RIGHT, -1},  {"updown", PG_DPAD_UPDOWN, -1},
    {"leftright", PG_DPAD_LEFTRIGHT, -1},
    {"lstick", PG_LSTICK, -1},     {"rstick", PG_RSTICK, -1},
    {"stick", PG_LSTICK, -1},
    // actions
    {"jump", -1, PA_JUMP},         {"accept", -1, PA_ACCEPT},
    {"light", -1, PA_LIGHT},       {"heavy", -1, PA_HEAVY},
    {"block", -1, PA_BLOCK},       {"magic", -1, PA_MAGIC},
    {"back", -1, PA_BACK},         {"projectile", -1, PA_PROJECTILE},
    {"pause", -1, PA_PAUSE},
    // Xbox names, for text from the PC/Xbox versions
    {"a", PG_CROSS, -1},           {"b", PG_CIRCLE, -1},
    {"x", PG_SQUARE, -1},          {"y", PG_TRIANGLE, -1},
    {"lb", PG_L, -1},              {"rb", PG_R, -1},
    {"lt", PG_L, -1},              {"rt", PG_R, -1},
    {"back_xbox", PG_SELECT, -1},  {"start_xbox", PG_START, -1},
};

int prompts_lookup(const char *name, int len) {
    char low[16];
    if (len <= 0 || len >= (int)sizeof low) return -1;
    for (int i = 0; i < len; i++) {
        char c = name[i];
        low[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
    }
    low[len] = 0;
    for (unsigned i = 0; i < sizeof k_tokens / sizeof *k_tokens; i++) {
        if (strcmp(k_tokens[i].name, low)) continue;
        return k_tokens[i].action >= 0 ? (int)g_action[k_tokens[i].action] : k_tokens[i].glyph;
    }
    return -1;
}

float prompts_aspect(PromptGlyph g) {
    return (g == PG_START || g == PG_SELECT) ? 2.0f : 1.0f;
}

// ---- Atlas layout ----
// Row 0: cross circle square triangle L R lstick rstick
// Row 1: dpad, up, down, left, right, updown, leftright
// Row 2: start (2 cells), select (2 cells)

PromptRect prompts_rect(PromptGlyph g) {
    static const signed char cell[PG_COUNT][2] = {
        [PG_CROSS] = {0, 0}, [PG_CIRCLE] = {1, 0}, [PG_SQUARE] = {2, 0},
        [PG_TRIANGLE] = {3, 0}, [PG_L] = {4, 0}, [PG_R] = {5, 0},
        [PG_LSTICK] = {6, 0}, [PG_RSTICK] = {7, 0},
        [PG_DPAD] = {0, 1}, [PG_DPAD_UP] = {1, 1}, [PG_DPAD_DOWN] = {2, 1},
        [PG_DPAD_LEFT] = {3, 1}, [PG_DPAD_RIGHT] = {4, 1},
        [PG_DPAD_UPDOWN] = {5, 1}, [PG_DPAD_LEFTRIGHT] = {6, 1},
        [PG_START] = {0, 2}, [PG_SELECT] = {2, 2},
    };
    PromptRect r = {0, 0, 0, 0};
    if ((unsigned)g >= PG_COUNT) return r;
    r.x = cell[g][0] * PROMPT_CELL;
    r.y = cell[g][1] * PROMPT_CELL;
    r.w = (int)(PROMPT_CELL * prompts_aspect(g));
    r.h = PROMPT_CELL;
    return r;
}

// ---- Shapes ----

enum { SH_CIRCLE, SH_SEG, SH_BOX, SH_TRI };

typedef struct { int type; float p[6]; } Shape;

#define MAX_SHAPES 96

typedef struct {
    Shape s[MAX_SHAPES];
    int n;
    float outline;   // > 0: draw only a band this thick around the edge
    uint8_t rgba[4];
} Layer;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static float sd_seg(float px, float py, float ax, float ay, float bx, float by, float r) {
    float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    float h = clampf((pax * bax + pay * bay) / (bax * bax + bay * bay + 1e-9f), 0, 1);
    float dx = pax - bax * h, dy = pay - bay * h;
    return sqrtf(dx * dx + dy * dy) - r;
}

static float sd_box(float px, float py, float cx, float cy, float hw, float hh, float rad) {
    float qx = fabsf(px - cx) - hw + rad, qy = fabsf(py - cy) - hh + rad;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - rad;
}

static float sd_tri(float px, float py, const float *v) {
    // Exact distance to a filled triangle (Inigo Quilez's formulation).
    float e0x = v[2] - v[0], e0y = v[3] - v[1];
    float e1x = v[4] - v[2], e1y = v[5] - v[3];
    float e2x = v[0] - v[4], e2y = v[1] - v[5];
    float v0x = px - v[0], v0y = py - v[1];
    float v1x = px - v[2], v1y = py - v[3];
    float v2x = px - v[4], v2y = py - v[5];
    float h0 = clampf((v0x * e0x + v0y * e0y) / (e0x * e0x + e0y * e0y), 0, 1);
    float h1 = clampf((v1x * e1x + v1y * e1y) / (e1x * e1x + e1y * e1y), 0, 1);
    float h2 = clampf((v2x * e2x + v2y * e2y) / (e2x * e2x + e2y * e2y), 0, 1);
    float p0x = v0x - e0x * h0, p0y = v0y - e0y * h0;
    float p1x = v1x - e1x * h1, p1y = v1y - e1y * h1;
    float p2x = v2x - e2x * h2, p2y = v2y - e2y * h2;
    float s = e0x * e2y - e0y * e2x > 0 ? 1.0f : -1.0f;
    float d0 = p0x * p0x + p0y * p0y, s0 = s * (v0x * e0y - v0y * e0x);
    float d1 = p1x * p1x + p1y * p1y, s1 = s * (v1x * e1y - v1y * e1x);
    float d2 = p2x * p2x + p2y * p2y, s2 = s * (v2x * e2y - v2y * e2x);
    float d = d0 < d1 ? d0 : d1; d = d < d2 ? d : d2;
    float sg = s0 < s1 ? s0 : s1; sg = sg < s2 ? sg : s2;
    return -sqrtf(d) * (sg < 0 ? -1.0f : 1.0f);
}

static float layer_dist(const Layer *l, float x, float y) {
    float d = 1e9f;
    for (int i = 0; i < l->n; i++) {
        const Shape *s = &l->s[i];
        const float *p = s->p;
        float e;
        switch (s->type) {
        case SH_CIRCLE: e = sqrtf((x - p[0]) * (x - p[0]) + (y - p[1]) * (y - p[1])) - p[2]; break;
        case SH_SEG:    e = sd_seg(x, y, p[0], p[1], p[2], p[3], p[4]); break;
        case SH_BOX:    e = sd_box(x, y, p[0], p[1], p[2], p[3], p[4]); break;
        default:        e = sd_tri(x, y, p); break;
        }
        if (e < d) d = e;
    }
    if (l->outline > 0) d = fabsf(d) - l->outline;
    return d;
}

static void layer_reset(Layer *l, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float outline) {
    l->n = 0;
    l->outline = outline;
    l->rgba[0] = r; l->rgba[1] = g; l->rgba[2] = b; l->rgba[3] = a;
}

static void add(Layer *l, int type, float a, float b, float c, float d, float e, float f) {
    if (l->n >= MAX_SHAPES) return;
    Shape *s = &l->s[l->n++];
    s->type = type;
    s->p[0] = a; s->p[1] = b; s->p[2] = c; s->p[3] = d; s->p[4] = e; s->p[5] = f;
}

static void add_poly(Layer *l, const float *pts, int n, float ox, float oy, float sc, float r) {
    for (int i = 0; i + 1 < n; i++)
        add(l, SH_SEG, ox + pts[2 * i] * sc, oy + pts[2 * i + 1] * sc,
            ox + pts[2 * i + 2] * sc, oy + pts[2 * i + 3] * sc, r, 0);
}

// Stroke letters on a 4 x 6 grid (y down), only the ones the glyphs need.
// Each letter is a list of polylines; a negative count ends the letter.
static void add_letter(Layer *l, char ch, float ox, float oy, float sc, float r) {
    static const float L1[] = {0, 0, 0, 6, 4, 6};
    static const float T1[] = {0, 0, 4, 0}, T2[] = {2, 0, 2, 6};
    static const float E1[] = {4, 0, 0, 0, 0, 6, 4, 6}, E2[] = {0, 3, 3, 3};
    static const float A1[] = {0, 6, 2, 0, 4, 6}, A2[] = {0.7f, 4, 3.3f, 4};
    static const float R1[] = {0, 6, 0, 0, 2.8f, 0, 3.7f, 0.4f, 4, 1.5f, 3.7f, 2.6f, 2.8f, 3, 0, 3};
    static const float R2[] = {2, 3, 4, 6};
    static const float C1[] = {4, 0.9f, 3.1f, 0, 0.9f, 0, 0, 1.2f, 0, 4.8f, 0.9f, 6, 3.1f, 6, 4, 5.1f};
    static const float S1[] = {4, 0.9f, 3.1f, 0, 0.9f, 0, 0, 0.9f, 0, 2.1f, 0.9f, 3, 3.1f, 3,
                               4, 3.9f, 4, 5.1f, 3.1f, 6, 0.9f, 6, 0, 5.1f};
#define P(a) add_poly(l, a, (int)(sizeof a / sizeof *a / 2), ox, oy, sc, r)
    switch (ch) {
    case 'L': P(L1); break;
    case 'T': P(T1); P(T2); break;
    case 'E': P(E1); P(E2); break;
    case 'A': P(A1); P(A2); break;
    case 'R': P(R1); P(R2); break;
    case 'C': P(C1); break;
    case 'S': P(S1); break;
    }
#undef P
}

// Centre a word of stroke letters on (cx, cy) with the given cap height.
static void add_word(Layer *l, const char *w, float cx, float cy, float h, float r) {
    float sc = h / 6.0f, adv = 4 * sc, gap = 1.6f * sc;
    int n = (int)strlen(w);
    float width = n * adv + (n - 1) * gap;
    float x = cx - width / 2, y = cy - h / 2;
    for (int i = 0; i < n; i++, x += adv + gap) add_letter(l, w[i], x, y, sc, r);
}

// Composite a layer "over" the cell (straight alpha).
static void paint(uint8_t *rgba, PromptRect rc, const Layer *l) {
    for (int y = 0; y < rc.h; y++) {
        for (int x = 0; x < rc.w; x++) {
            float d = layer_dist(l, x + 0.5f, y + 0.5f);
            float cov = clampf(0.5f - d, 0, 1);
            if (cov <= 0) continue;
            uint8_t *px = rgba + ((rc.y + y) * PROMPT_ATLAS_W + rc.x + x) * 4;
            float sa = cov * l->rgba[3] / 255.0f, da = px[3] / 255.0f;
            float oa = sa + da * (1 - sa);
            for (int c = 0; c < 3; c++) {
                float v = (l->rgba[c] * sa + px[c] * da * (1 - sa)) / (oa > 0 ? oa : 1);
                px[c] = (uint8_t)(v + 0.5f);
            }
            px[3] = (uint8_t)(oa * 255 + 0.5f);
        }
    }
}

// Colours: dark button body, light rim, and the classic face-button colours.
#define BODY 30, 30, 36, 255
#define RIM 236, 236, 236, 255
#define INK 250, 250, 250, 255

static Layer g_l;   // big enough to keep off the stack

static void face_button(uint8_t *rgba, PromptRect rc, PromptGlyph g) {
    Layer *l = &g_l;
    layer_reset(l, BODY, 0); add(l, SH_CIRCLE, 32, 32, 30, 0, 0, 0); paint(rgba, rc, l);
    layer_reset(l, RIM, 1.4f); add(l, SH_CIRCLE, 32, 32, 29.4f, 0, 0, 0); paint(rgba, rc, l);
    switch (g) {
    case PG_CROSS:
        layer_reset(l, 124, 178, 232, 255, 0);
        add(l, SH_SEG, 21, 21, 43, 43, 3.4f, 0);
        add(l, SH_SEG, 43, 21, 21, 43, 3.4f, 0);
        break;
    case PG_CIRCLE:
        layer_reset(l, 255, 102, 102, 255, 3.4f);
        add(l, SH_CIRCLE, 32, 32, 12.5f, 0, 0, 0);
        break;
    case PG_SQUARE:
        layer_reset(l, 217, 154, 203, 255, 3.2f);
        add(l, SH_BOX, 32, 32, 11.5f, 11.5f, 0.5f, 0);
        break;
    default: {   // triangle
        static const float tri[] = {32, 17.5f, 45.5f, 40.5f, 18.5f, 40.5f, 32, 17.5f};
        layer_reset(l, 64, 224, 176, 255, 0);
        add_poly(l, tri, 4, 0, 0, 1, 3.2f);
        break;
    }
    }
    paint(rgba, rc, l);
}

static void shoulder(uint8_t *rgba, PromptRect rc, char letter) {
    Layer *l = &g_l;
    layer_reset(l, BODY, 0); add(l, SH_BOX, 32, 32, 29, 19, 9, 0); paint(rgba, rc, l);
    layer_reset(l, RIM, 1.4f); add(l, SH_BOX, 32, 32, 28.4f, 18.4f, 8.4f, 0); paint(rgba, rc, l);
    layer_reset(l, INK, 0);
    char w[2] = {letter, 0};
    add_word(l, w, 32, 32, 22, 2.4f);
    paint(rgba, rc, l);
}

static void pill(uint8_t *rgba, PromptRect rc, const char *word) {
    Layer *l = &g_l;
    layer_reset(l, BODY, 0); add(l, SH_BOX, 64, 32, 61, 20, 20, 0); paint(rgba, rc, l);
    layer_reset(l, RIM, 1.4f); add(l, SH_BOX, 64, 32, 60.4f, 19.4f, 19.4f, 0); paint(rgba, rc, l);
    layer_reset(l, INK, 0);
    add_word(l, word, 64, 32, 17, 1.7f);
    paint(rgba, rc, l);
}

static void stick(uint8_t *rgba, PromptRect rc, char letter) {
    Layer *l = &g_l;
    layer_reset(l, BODY, 0); add(l, SH_CIRCLE, 32, 32, 30, 0, 0, 0); paint(rgba, rc, l);
    layer_reset(l, RIM, 1.4f); add(l, SH_CIRCLE, 32, 32, 29.4f, 0, 0, 0); paint(rgba, rc, l);
    layer_reset(l, 78, 78, 86, 255, 0); add(l, SH_CIRCLE, 32, 32, 18, 0, 0, 0); paint(rgba, rc, l);
    layer_reset(l, 170, 170, 176, 255, 1.0f); add(l, SH_CIRCLE, 32, 32, 17.5f, 0, 0, 0); paint(rgba, rc, l);
    layer_reset(l, INK, 0);
    char w[2] = {letter, 0};
    add_word(l, w, 32, 32, 16, 2.0f);
    // little arrows around the stick: it moves in every direction
    add(l, SH_TRI, 32, 5.5f, 28, 10, 36, 10);
    add(l, SH_TRI, 32, 58.5f, 36, 54, 28, 54);
    add(l, SH_TRI, 5.5f, 32, 10, 36, 10, 28);
    add(l, SH_TRI, 58.5f, 32, 54, 28, 54, 36);
    paint(rgba, rc, l);
}

// mask bits: 1 up, 2 down, 4 left, 8 right (0 = plain d-pad)
static void dpad(uint8_t *rgba, PromptRect rc, int mask) {
    Layer *l = &g_l;
    layer_reset(l, BODY, 0);
    add(l, SH_BOX, 32, 32, 10.5f, 29, 3.5f, 0);
    add(l, SH_BOX, 32, 32, 29, 10.5f, 3.5f, 0);
    paint(rgba, rc, l);
    layer_reset(l, RIM, 1.4f);
    add(l, SH_BOX, 32, 32, 9.9f, 28.4f, 2.9f, 0);
    add(l, SH_BOX, 32, 32, 28.4f, 9.9f, 2.9f, 0);
    paint(rgba, rc, l);

    // Arrow per arm: (tip, base left, base right)
    static const float arrows[4][6] = {
        {32, 8, 26.5f, 16, 37.5f, 16},    // up
        {32, 56, 37.5f, 48, 26.5f, 48},   // down
        {8, 32, 16, 37.5f, 16, 26.5f},    // left
        {56, 32, 48, 26.5f, 48, 37.5f},   // right
    };
    for (int pass = 0; pass < 2; pass++) {   // 0: dim arrows, 1: lit ones
        if (pass == 0) layer_reset(l, 120, 120, 128, 255, 0);
        else           layer_reset(l, INK, 0);
        for (int i = 0; i < 4; i++) {
            int lit = mask == 0 || (mask >> i & 1);
            if (lit != pass) continue;
            const float *a = arrows[i];
            add(l, SH_TRI, a[0], a[1], a[2], a[3], a[4], a[5]);
        }
        paint(rgba, rc, l);
    }
}

void prompts_build_atlas(uint8_t *rgba) {
    memset(rgba, 0, PROMPT_ATLAS_W * PROMPT_ATLAS_H * 4);
    for (int g = 0; g < PG_COUNT; g++) {
        PromptRect rc = prompts_rect((PromptGlyph)g);
        switch (g) {
        case PG_CROSS: case PG_CIRCLE: case PG_SQUARE: case PG_TRIANGLE:
            face_button(rgba, rc, (PromptGlyph)g); break;
        case PG_L: shoulder(rgba, rc, 'L'); break;
        case PG_R: shoulder(rgba, rc, 'R'); break;
        case PG_START: pill(rgba, rc, "START"); break;
        case PG_SELECT: pill(rgba, rc, "SELECT"); break;
        case PG_LSTICK: stick(rgba, rc, 'L'); break;
        case PG_RSTICK: stick(rgba, rc, 'R'); break;
        case PG_DPAD: dpad(rgba, rc, 0); break;
        case PG_DPAD_UP: dpad(rgba, rc, 1); break;
        case PG_DPAD_DOWN: dpad(rgba, rc, 2); break;
        case PG_DPAD_LEFT: dpad(rgba, rc, 4); break;
        case PG_DPAD_RIGHT: dpad(rgba, rc, 8); break;
        case PG_DPAD_UPDOWN: dpad(rgba, rc, 3); break;
        case PG_DPAD_LEFTRIGHT: dpad(rgba, rc, 12); break;
        }
    }
}

// ---- Characters drawn as glyphs ----

#define MAX_CP_MAP 32
static uint32_t g_cp[MAX_CP_MAP];
static uint8_t g_cp_glyph[MAX_CP_MAP];
static int g_ncp;

void prompts_map_codepoint(uint32_t cp, PromptGlyph g) {
    for (int i = 0; i < g_ncp; i++)
        if (g_cp[i] == cp) { g_cp_glyph[i] = (uint8_t)g; return; }
    if (g_ncp == MAX_CP_MAP) return;
    g_cp[g_ncp] = cp;
    g_cp_glyph[g_ncp++] = (uint8_t)g;
}

int prompts_lookup_codepoint(uint32_t cp) {
    for (int i = 0; i < g_ncp; i++)
        if (g_cp[i] == cp) return g_cp_glyph[i];
    return -1;
}

int prompts_map_from_strtab(void) {
    int count = 0;
    char key[32];
    // Private-use area, where fonts usually keep icon characters.
    for (uint32_t cp = 0xE000; cp < 0xE100; cp++) {
        snprintf(key, sizeof key, "glyph.U+%04X", (unsigned)cp);
        const char *v = strtab_get(key);
        if (!v) continue;
        int g = prompts_lookup(v, (int)strlen(v));
        if (g >= 0) { prompts_map_codepoint(cp, (PromptGlyph)g); count++; }
    }
    return count;
}

void prompts_glyph_quad(PromptGlyph g, float x, float y_center, float h,
                        const float m[6], float out[4][4]) {
    PromptRect r = prompts_rect(g);
    float w = h * prompts_aspect(g);
    float xs[4] = {x, x + w, x + w, x};
    float ys[4] = {y_center - h / 2, y_center - h / 2, y_center + h / 2, y_center + h / 2};
    float u0 = (float)r.x / PROMPT_ATLAS_W, u1 = (float)(r.x + r.w) / PROMPT_ATLAS_W;
    float v0 = (float)r.y / PROMPT_ATLAS_H, v1 = (float)(r.y + r.h) / PROMPT_ATLAS_H;
    float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
    for (int i = 0; i < 4; i++) {
        out[i][0] = m[0] * xs[i] + m[2] * ys[i] + m[4];
        out[i][1] = m[1] * xs[i] + m[3] * ys[i] + m[5];
        out[i][2] = us[i];
        out[i][3] = vs[i];
    }
}
