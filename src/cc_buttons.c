// PlayStation button prompts drawn from the game's own button art. Every SWF exports
// Abutton, Bbutton, Xbutton, Ybutton (a glossy ball: black outline, base colour,
// highlight, shade, and the letter as a white fill) and Ltrigger, Rtrigger (grey
// trigger shapes with dark "LT"/"RT"). Here the letter fill is swapped for a
// PlayStation symbol and the ball recoloured, so prompts keep the game's look.
#include "cc_buttons.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float x0, y0, x1, y1; } Box;

static Box fill_box(const ShapeFill *f) {
    Box b = {1e30f, 1e30f, -1e30f, -1e30f};
    for (int i = 0; i < f->npoints; i++) {
        float x = f->pts[i * 2], y = f->pts[i * 2 + 1];
        if (x < b.x0) b.x0 = x;
        if (y < b.y0) b.y0 = y;
        if (x > b.x1) b.x1 = x;
        if (y > b.y1) b.y1 = y;
    }
    return b;
}

static int luma(const uint8_t c[4]) { return c[0] * 3 + c[1] * 6 + c[2]; }
static int is_black(const uint8_t c[4]) { return c[0] < 40 && c[1] < 40 && c[2] < 40; }
static int is_white(const uint8_t c[4]) { return c[0] > 240 && c[1] > 240 && c[2] > 240; }

// ---- building a fill out of loops (even-odd: a loop inside another is a hole) -------

typedef struct {
    float *pts;
    int n, cap;
    int *loops;
    int nloops, caploops;
} Poly;

static void poly_begin_loop(Poly *p) {
    if (p->nloops + 2 > p->caploops) {
        p->caploops = p->caploops ? p->caploops * 2 : 8;
        p->loops = realloc(p->loops, sizeof(int) * p->caploops);
    }
    p->loops[p->nloops++] = p->n;
}

static void poly_pt(Poly *p, float x, float y) {
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 64;
        p->pts = realloc(p->pts, sizeof(float) * 2 * p->cap);
    }
    p->pts[p->n * 2] = x;
    p->pts[p->n * 2 + 1] = y;
    p->n++;
}

static void ring(Poly *p, float cx, float cy, float r, int segs) {
    poly_begin_loop(p);
    for (int i = 0; i < segs; i++) {
        float a = 6.2831853f * i / segs;
        poly_pt(p, cx + r * cosf(a), cy + r * sinf(a));
    }
}

static void regular(Poly *p, float cx, float cy, float r, int sides, float start) {
    poly_begin_loop(p);
    for (int i = 0; i < sides; i++) {
        float a = start + 6.2831853f * i / sides;
        poly_pt(p, cx + r * cosf(a), cy + r * sinf(a));
    }
}

static void rect(Poly *p, float x0, float y0, float x1, float y1) {
    poly_begin_loop(p);
    poly_pt(p, x0, y0); poly_pt(p, x1, y0); poly_pt(p, x1, y1); poly_pt(p, x0, y1);
}

// the symbol for a button, centred at cx,cy, about s across
static void symbol(Poly *p, int which, float cx, float cy, float s) {
    float t = s * 0.17f;   // stroke width
    switch (which) {
    case CC_BTN_CROSS: {   // an X as one outline (a plus turned 45 degrees)
        float L = s * 0.5f, w = t * 0.55f;
        float plus[12][2] = {{w, L}, {w, w}, {L, w}, {L, -w}, {w, -w}, {w, -L},
                             {-w, -L}, {-w, -w}, {-L, -w}, {-L, w}, {-w, w}, {-w, L}};
        poly_begin_loop(p);
        for (int i = 0; i < 12; i++) {
            float x = plus[i][0], y = plus[i][1];
            poly_pt(p, cx + (x - y) * 0.7071f, cy + (x + y) * 0.7071f);
        }
        break;
    }
    case CC_BTN_CIRCLE:
        ring(p, cx, cy, s * 0.46f, 40);
        ring(p, cx, cy, s * 0.46f - t, 40);
        break;
    case CC_BTN_SQUARE: {
        float a = s * 0.40f;
        rect(p, cx - a, cy - a, cx + a, cy + a);
        rect(p, cx - a + t, cy - a + t, cx + a - t, cy + a - t);
        break;
    }
    case CC_BTN_TRIANGLE: {   // pointing up; the inner one is the outer offset inward by t
        float R = s * 0.56f, oy = cy + s * 0.08f;
        regular(p, cx, oy, R, 3, -1.5707963f);
        regular(p, cx, oy, R - 2 * t, 3, -1.5707963f);
        break;
    }
    }
}

// "L" or "R" in a box (letters for the shoulder buttons), as non-overlapping pieces
static void letter(Poly *p, char c, Box b) {
    float h = b.y1 - b.y0, w = h * 0.62f, t = h * 0.22f;
    float x0 = (b.x0 + b.x1) / 2 - w / 2, y0 = b.y0, y1 = b.y1;
    if (c == 'L') {
        rect(p, x0, y0, x0 + t, y1 - t);
        rect(p, x0, y1 - t, x0 + w, y1);
        return;
    }
    // R: stem, bowl (a ring of a rounded box), leg
    rect(p, x0, y0, x0 + t, y1);
    float bx0 = x0 + t, by0 = y0, by1 = y0 + h * 0.56f, bx1 = x0 + w;
    float r = (by1 - by0) / 2;
    poly_begin_loop(p);
    poly_pt(p, bx0, by0);
    for (int i = 0; i <= 12; i++) {   // right half of the bowl
        float a = -1.5707963f + 3.1415926f * i / 12;
        poly_pt(p, bx1 - r + r * cosf(a), by0 + r + r * sinf(a));
    }
    poly_pt(p, bx0, by1);
    float ri = r - t * 0.9f;
    poly_begin_loop(p);
    poly_pt(p, bx0, by0 + t * 0.9f);
    for (int i = 0; i <= 12; i++) {
        float a = -1.5707963f + 3.1415926f * i / 12;
        poly_pt(p, bx1 - r + ri * cosf(a), by0 + r + ri * sinf(a));
    }
    poly_pt(p, bx0, by1 - t * 0.9f);
    poly_begin_loop(p);   // leg, from under the bowl to the bottom right
    poly_pt(p, bx0 + t * 0.2f, by1);
    poly_pt(p, bx0 + t * 1.2f, by1);
    poly_pt(p, x0 + w, y1);
    poly_pt(p, x0 + w - t * 1.1f, y1);
}

static ShapeFill fill_from(Poly *p, const uint8_t colour[4]) {
    ShapeFill f;
    memset(&f, 0, sizeof f);
    f.style.type = FILL_SOLID;
    memcpy(f.style.color, colour, 4);
    poly_begin_loop(p);   // end marker
    p->nloops--;
    f.npoints = p->n;
    f.pts = p->pts;
    f.nloops = p->nloops;
    f.loop_start = p->loops;
    return f;
}

// Drops every loop that lies inside box b and is no bigger than max_area from every fill:
// the letter, its black outline (part of the button's outline fill) and the hole it cuts
// in the colour fills. The button's own body loops are bigger, so they stay.
static void remove_loops_in(Shape *s, Box b, float max_area) {
    for (int f = 0; f < s->nfills; f++) {
        ShapeFill *sf = &s->fills[f];
        int out_pts = 0, out_loops = 0;
        for (int l = 0; l < sf->nloops; l++) {
            int a = sf->loop_start[l], e = sf->loop_start[l + 1];
            Box lb = {1e30f, 1e30f, -1e30f, -1e30f};
            for (int i = a; i < e; i++) {
                float x = sf->pts[i * 2], y = sf->pts[i * 2 + 1];
                if (x < lb.x0) lb.x0 = x;
                if (y < lb.y0) lb.y0 = y;
                if (x > lb.x1) lb.x1 = x;
                if (y > lb.y1) lb.y1 = y;
            }
            if ((lb.x1 - lb.x0) * (lb.y1 - lb.y0) <= max_area && lb.x0 >= b.x0 && lb.y0 >= b.y0 && lb.x1 <= b.x1 &&
                lb.y1 <= b.y1)
                continue;
            sf->loop_start[out_loops++] = out_pts;
            memmove(sf->pts + out_pts * 2, sf->pts + a * 2, sizeof(float) * 2 * (e - a));
            out_pts += e - a;
        }
        sf->loop_start[out_loops] = out_pts;
        sf->nloops = out_loops;
        sf->npoints = out_pts;
    }
}

static Box grow_box(Box b, float k) {
    float dx = (b.x1 - b.x0) * k, dy = (b.y1 - b.y0) * k;
    return (Box){b.x0 - dx, b.y0 - dy, b.x1 + dx, b.y1 + dy};
}

static void append_fill(Shape *s, ShapeFill f) {
    s->fills = realloc(s->fills, sizeof(ShapeFill) * (s->nfills + 1));
    s->fills[s->nfills++] = f;   // last: drawn on top
}

// ---- recolouring the ball ------------------------------------------------------------

typedef struct { uint8_t shade[4], base[4], light[4], symbol[4]; } Palette;

static const Palette COLOURED[] = {
    [CC_BTN_CROSS] = {{0x33, 0x55, 0xcc, 255}, {0x55, 0x88, 0xee, 255}, {0x88, 0xbb, 0xff, 255}, {255, 255, 255, 255}},
    [CC_BTN_CIRCLE] = {{0x99, 0x00, 0x00, 255}, {0xcc, 0x11, 0x11, 255}, {0xff, 0x44, 0x44, 255}, {255, 255, 255, 255}},
    [CC_BTN_SQUARE] = {{0xb0, 0x3a, 0x8c, 255}, {0xe0, 0x5f, 0xb5, 255}, {0xff, 0x9a, 0xdc, 255}, {255, 255, 255, 255}},
    [CC_BTN_TRIANGLE] = {{0x1f, 0x8a, 0x5c, 255}, {0x33, 0xb3, 0x80, 255}, {0x66, 0xe0, 0xaa, 255}, {255, 255, 255, 255}},
};
static const uint8_t DARK_SHADE[4] = {0x22, 0x22, 0x26, 255}, DARK_BASE[4] = {0x3c, 0x3c, 0x44, 255},
                     DARK_LIGHT[4] = {0x6a, 0x6a, 0x76, 255};
static const uint8_t SYMBOL_COLOUR[][4] = {
    [CC_BTN_CROSS] = {0x7d, 0xa7, 0xff, 255}, [CC_BTN_CIRCLE] = {0xff, 0x66, 0x66, 255},
    [CC_BTN_SQUARE] = {0xff, 0x8c, 0xd9, 255}, [CC_BTN_TRIANGLE] = {0x4c, 0xe0, 0xae, 255},
};

static void make_ball(Shape *s, int which, int style) {
    // the letter: the white fill; the ball: the coloured fills, darkest to lightest
    int letter_i = -1, ncol = 0, col[8];
    for (int i = 0; i < s->nfills; i++) {
        const uint8_t *c = s->fills[i].style.color;
        if (is_white(c) && (letter_i < 0 || s->fills[i].npoints < s->fills[letter_i].npoints)) letter_i = i;
    }
    for (int i = 0; i < s->nfills && ncol < 8; i++)
        if (i != letter_i && !is_black(s->fills[i].style.color)) col[ncol++] = i;
    for (int a = 0; a < ncol; a++)   // sort by brightness
        for (int b = a + 1; b < ncol; b++)
            if (luma(s->fills[col[b]].style.color) < luma(s->fills[col[a]].style.color)) { int t = col[a]; col[a] = col[b]; col[b] = t; }
    for (int k = 0; k < ncol; k++) {
        const uint8_t *to;
        if (style == CC_BTN_STYLE_DARK) to = k == 0 ? DARK_SHADE : k == ncol - 1 ? DARK_LIGHT : DARK_BASE;
        else to = k == 0 ? COLOURED[which].shade : k == ncol - 1 ? COLOURED[which].light : COLOURED[which].base;
        memcpy(s->fills[col[k]].style.color, to, 3);
    }
    if (letter_i < 0) return;
    // the symbol goes where the ball's colour is, sized like the letter
    Box lb = fill_box(&s->fills[letter_i]), bb = ncol ? fill_box(&s->fills[col[0]]) : lb;
    float size = fmaxf(lb.x1 - lb.x0, lb.y1 - lb.y0) * 1.0f;
    remove_loops_in(s, grow_box(lb, 0.6f), (lb.x1 - lb.x0) * (lb.y1 - lb.y0) * 1.6f);
    Poly p = {0};
    symbol(&p, which, (lb.x0 + lb.x1) / 2, (lb.y0 + lb.y1) / 2, size);
    (void)bb;
    append_fill(s, fill_from(&p, style == CC_BTN_STYLE_DARK ? SYMBOL_COLOUR[which] : COLOURED[which].symbol));
}

static void make_trigger(Shape *s, char c) {
    // the letters: the darkest fill that isn't the pure black outline (dark grey "LT"/"RT")
    int li = -1;
    for (int i = 0; i < s->nfills; i++) {
        const uint8_t *col = s->fills[i].style.color;
        if (!luma(col) || is_white(col)) continue;
        if (li < 0 || luma(col) < luma(s->fills[li].style.color)) li = i;
    }
    if (li < 0) return;
    Box b = fill_box(&s->fills[li]);
    uint8_t colour[4];
    memcpy(colour, s->fills[li].style.color, 4);
    remove_loops_in(s, grow_box(b, 0.08f), 1e30f);
    Poly p = {0};
    letter(&p, c, b);
    append_fill(s, fill_from(&p, colour));
}

static Shape *exported_shape(Movie *m, const char *name) {
    for (int c = 0; c < 65536; c++) {
        if (m->chars[c].type != CHAR_SPRITE || !m->chars[c].export) continue;
        if (strcmp(m->names[m->chars[c].export], name)) continue;
        const Sprite *sp = m->chars[c].ptr;
        if (!sp->nframes || !sp->frames[0].count) return NULL;
        const Character *sc = &m->chars[sp->frames[0].places[0].cid];
        return sc->type == CHAR_SHAPE ? sc->ptr : NULL;
    }
    return NULL;
}

int cc_buttons_playstation(Movie *m, int style) {
    static const struct { const char *name; int which; } balls[] = {
        {"Abutton", CC_BTN_CROSS}, {"Bbutton", CC_BTN_CIRCLE}, {"Xbutton", CC_BTN_SQUARE}, {"Ybutton", CC_BTN_TRIANGLE}};
    int done = 0;
    for (int i = 0; i < 4; i++) {
        Shape *s = exported_shape(m, balls[i].name);
        if (s && !s->tessellated) { make_ball(s, balls[i].which, style); done++; }
    }
    Shape *l = exported_shape(m, "Ltrigger"), *r = exported_shape(m, "Rtrigger");
    if (l && !l->tessellated) { make_trigger(l, 'L'); done++; }
    if (r && !r->tessellated) { make_trigger(r, 'R'); done++; }
    return done;
}
