// PlayStation button prompts drawn from the game's own button art. Every SWF exports
// Abutton, Bbutton, Xbutton, Ybutton (a glossy ball: black outline, base colour,
// highlight, shade, and the letter as a white fill) and Ltrigger, Rtrigger (grey
// trigger shapes with dark "LT"/"RT"). Here the letter fill is swapped for a
// PlayStation symbol and the ball recoloured, so prompts keep the game's look.
#include "cc_buttons.h"

#include <math.h>
#include <stdio.h>
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

// one closed loop: a box with its own radius per corner (tl, tr, br, bl)
static void round_box(Poly *p, float x0, float y0, float x1, float y1, const float r[4]) {
    const float cx[4] = {x0 + r[0], x1 - r[1], x1 - r[2], x0 + r[3]};
    const float cy[4] = {y0 + r[0], y0 + r[1], y1 - r[2], y1 - r[3]};
    const float a0[4] = {3.1415926f, 4.7123890f, 0.0f, 1.5707963f};
    poly_begin_loop(p);
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= 8; i++) {
            float a = a0[k] + 1.5707963f * i / 8;
            poly_pt(p, cx[k] + r[k] * cosf(a), cy[k] + r[k] * sinf(a));
        }
}

// The Vita's shoulder buttons: a grey slab with a big rounded outer-top corner and its
// letter, drawn inside the original trigger's footprint.
static void make_trigger(Shape *s, char c) {
    Box b = {1e30f, 1e30f, -1e30f, -1e30f};
    for (int i = 0; i < s->nfills; i++) {
        Box fb = fill_box(&s->fills[i]);
        if (fb.x0 < b.x0) b.x0 = fb.x0;
        if (fb.y0 < b.y0) b.y0 = fb.y0;
        if (fb.x1 > b.x1) b.x1 = fb.x1;
        if (fb.y1 > b.y1) b.y1 = fb.y1;
    }
    if (b.x0 > b.x1) return;
    float h = b.y1 - b.y0, w = fminf(b.x1 - b.x0, h * 1.9f);
    float h2 = h * 0.72f;
    float x0 = (b.x0 + b.x1) / 2 - w / 2, x1 = x0 + w;
    float y0 = (b.y0 + b.y1) / 2 - h2 / 2, y1 = y0 + h2;
    int left = c == 'L';
    float big = h2 * 0.85f, small = h2 * 0.22f;
    float r[4] = {left ? big : small, left ? small : big, small, small};
    float o = h * 0.07f;
    s->nfills = 0;
    s->nlines = 0;
    static const uint8_t BLACK[4] = {0, 0, 0, 255}, SHADE[4] = {0x5a, 0x5a, 0x62, 255},
                         BASE[4] = {0x9a, 0x9a, 0xa4, 255}, LIGHT[4] = {0xd8, 0xd8, 0xe0, 255},
                         INK[4] = {0x26, 0x26, 0x2c, 255};
    Poly p = {0};
    round_box(&p, x0, y0, x1, y1, r);
    append_fill(s, fill_from(&p, BLACK));
    float ri[4];
    for (int k = 0; k < 4; k++) ri[k] = fmaxf(r[k] - o, 0);
    Poly q = {0};
    round_box(&q, x0 + o, y0 + o, x1 - o, y1 - o, ri);
    append_fill(s, fill_from(&q, SHADE));
    Poly bp = {0};
    round_box(&bp, x0 + o, y0 + o, x1 - o, y1 - o * 2.6f, ri);
    append_fill(s, fill_from(&bp, BASE));
    float hr[4];
    for (int k = 0; k < 4; k++) hr[k] = fminf(ri[k] * 0.8f, (h2 * 0.42f - o * 1.6f) * 0.5f);
    Poly lp = {0};
    round_box(&lp, x0 + o * 2.2f, y0 + o * 1.6f, x1 - o * 2.2f, y0 + h2 * 0.42f, hr);
    append_fill(s, fill_from(&lp, LIGHT));
    float lh = h2 * 0.5f, cxm = (x0 + x1) / 2 + (left ? w * 0.06f : -w * 0.06f);
    Poly tp = {0};
    letter(&tp, c, (Box){cxm - lh * 0.31f, y0 + (h2 - lh) / 2 - o * 0.3f, cxm + lh * 0.31f, y0 + (h2 + lh) / 2 - o * 0.3f});
    append_fill(s, fill_from(&tp, INK));
}

// ---- the title's "Steam Edition" badge becomes "Vita Edition" -------------------------
// MENU.swf's title shows the hand-lettered "STEAM EDITION" and the Steam pin as white loops
// in shape 601 (with the flying green knight and his cloud). Those loops are taken out, so
// the sky behind shows through as it always did, and "VITA EDITION" goes in their place in
// one of the file's own fonts, with the four PlayStation symbols where the pin was.
// Positions are worked out on the 960 x 544 screen and mapped back into the logo's space.

// Drops the loops of one fill whose box lies inside x0..x1, y0..y1 (twips).
static void drop_loops(ShapeFill *sf, float x0, float y0, float x1, float y1) {
    int out = 0, pts = 0;
    for (int l = 0; l < sf->nloops; l++) {
        int s = sf->loop_start[l], e = sf->loop_start[l + 1];
        float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
        for (int i = s; i < e; i++) {
            float x = sf->pts[i * 2], y = sf->pts[i * 2 + 1];
            if (x < bx0) bx0 = x;
            if (y < by0) by0 = y;
            if (x > bx1) bx1 = x;
            if (y > by1) by1 = y;
        }
        if (bx0 >= x0 && bx1 <= x1 && by0 >= y0 && by1 <= y1) continue;
        memmove(sf->pts + pts * 2, sf->pts + s * 2, sizeof(float) * 2 * (e - s));
        sf->loop_start[out++] = pts;
        pts += e - s;
    }
    sf->loop_start[out] = pts;
    sf->nloops = out;
    sf->npoints = pts;
}

static void remove_steam_badge(Movie *m) {
    if (m->chars[601].type != CHAR_SHAPE) return;
    Shape *s = m->chars[601].ptr;
    if (s->tessellated) return;
    for (int f = 0; f < s->nfills; f++) {
        const uint8_t *c = s->fills[f].style.color;
        // the white letters and pin: left of the knight, below his cloud (x -190..-78, y -11..78 px)
        if (s->fills[f].style.type == FILL_SOLID && c[0] == 255 && c[1] == 255 && c[2] == 255)
            drop_loops(&s->fills[f], -200 * 20, -12 * 20, -77 * 20, 80 * 20);
    }
}

typedef struct { Mat to_shape; float k; } BadgeSpace;   // screen px -> logo shape twips

static void bpt(Poly *p, const BadgeSpace *bs, float sx, float sy) {
    float x = sx / bs->k * 20, y = sy / bs->k * 20;   // stage twips
    poly_pt(p, bs->to_shape.a * x + bs->to_shape.c * y + bs->to_shape.tx,
            bs->to_shape.b * x + bs->to_shape.d * y + bs->to_shape.ty);
}

static Mat mat_invert(Mat m) {
    float det = m.a * m.d - m.b * m.c;
    if (det == 0) det = 1;
    Mat r = {m.d / det, -m.b / det, -m.c / det, m.a / det, 0, 0};
    r.tx = -(r.a * m.tx + r.c * m.ty);
    r.ty = -(r.b * m.tx + r.d * m.ty);
    return r;
}

static Mat mat_mul2(Mat p, Mat c) {   // p after c
    return (Mat){p.a * c.a + p.c * c.b, p.b * c.a + p.d * c.b, p.a * c.c + p.c * c.d, p.b * c.c + p.d * c.d,
                 p.a * c.tx + p.c * c.ty + p.tx, p.b * c.tx + p.d * c.ty + p.ty};
}

// a font with every letter of the badge, the one with the most glyphs
static const Font *badge_font(const Movie *m) {
    const Font *best = NULL;
    for (int c = 0; c < 65536; c++) {
        if (m->chars[c].type != CHAR_FONT) continue;
        const Font *f = m->chars[c].ptr;
        int ok = 1;
        for (const char *s = "VITAEDON"; *s && ok; s++) ok = font_glyph(f, *s) >= 0 && f->glyphs[font_glyph(f, *s)];
        if (ok && (!best || f->nglyphs > best->nglyphs)) best = f;
    }
    return best;
}

// text at screen x,y (top-left), h px tall caps, tilted by slope (dy per dx)
static float badge_text(Poly *p, const BadgeSpace *bs, const Font *f, const char *s, float x, float y, float h, float slope) {
    float scale = h / (f->ascent ? f->ascent * 0.72f : f->em * 0.7f);   // cap height about 0.72 of the ascent
    float pen = 0;
    for (; *s; s++) {
        int gi = font_glyph(f, *s);
        if (gi >= 0 && f->glyphs[gi]) {
            const Shape *g = f->glyphs[gi];
            for (int fi = 0; fi < g->nfills; fi++) {
                const ShapeFill *sf = &g->fills[fi];
                for (int l = 0; l < sf->nloops; l++) {
                    poly_begin_loop(p);
                    for (int i = sf->loop_start[l]; i < sf->loop_start[l + 1]; i++) {
                        float gx = (pen + sf->pts[i * 2]) * scale, gy = sf->pts[i * 2 + 1] * scale;
                        bpt(p, bs, x + gx, y + h + gy + gx * slope);
                    }
                }
            }
        }
        float adv = f->has_layout && gi >= 0 ? f->advance[gi] : f->em * 0.6f;
        pen += adv * 1.1f;   // a little apart, like the hand lettering
    }
    return pen * scale;
}

int cc_menu_vita_badge(Movie *m) {
    // MENU.swf: the title sprite 631 shows the logo (shape 354) at depth 60; the root's "main"
    // frame shows 631 as "body"
    if (m->chars[631].type != CHAR_SPRITE || m->chars[354].type != CHAR_SHAPE || m->stage_w <= 0) return 0;
    const Sprite *title = m->chars[631].ptr;
    const Place *logo = NULL, *body = NULL;
    for (int i = 0; title->nframes && i < title->frames[0].count; i++)
        if (title->frames[0].places[i].cid == 354) logo = &title->frames[0].places[i];
    for (int fr = 0; fr < m->root.nframes && !body; fr++)
        for (int i = 0; i < m->root.frames[fr].count; i++)
            if (m->root.frames[fr].places[i].cid == 631) { body = &m->root.frames[fr].places[i]; break; }
    if (!logo || !body) return 0;
    Shape *s = m->chars[354].ptr;
    if (s->tessellated) return 0;
    BadgeSpace bs = {mat_invert(mat_mul2(body->m, logo->m)), 960.0f / m->stage_w};
    static const uint8_t WHITE[4] = {255, 255, 255, 255};
    remove_steam_badge(m);
    const Font *f = badge_font(m);
    if (f) {
        float slope = -0.12f;   // the original lettering rises to the right
        // drawn a few times a pixel apart: as heavy as the hand-lettered original
        static const float OFF[3][2] = {{0, 0}, {0.8f, 0}, {0, 0.8f}};
        for (int o = 0; o < 3; o++) {
            Poly txt = {0};
            badge_text(&txt, &bs, f, "VITA", 358 + OFF[o][0], 92 + OFF[o][1], 22, slope);
            badge_text(&txt, &bs, f, "EDITION", 340 + OFF[o][0], 117 + OFF[o][1], 16, slope);
            append_fill(s, fill_from(&txt, WHITE));
        }
    }
    // the PlayStation symbols where the pin was, in their colours
    static const uint8_t COLS[4][4] = {{122, 160, 230, 255}, {230, 90, 100, 255}, {220, 120, 200, 255}, {90, 200, 160, 255}};
    static const float POS[4][2] = {{452, 128}, {464, 115}, {440, 115}, {452, 102}};   // cross bottom, circle right, square left, triangle top
    static const int SYM[4] = {CC_BTN_CROSS, CC_BTN_CIRCLE, CC_BTN_SQUARE, CC_BTN_TRIANGLE};
    for (int i = 0; i < 4; i++) {
        Poly sp = {0}, mapped = {0};
        symbol(&sp, SYM[i], POS[i][0], POS[i][1], 12);
        poly_begin_loop(&sp);
        sp.nloops--;
        for (int l = 0; l < sp.nloops; l++) {
            poly_begin_loop(&mapped);
            for (int q = sp.loops[l]; q < sp.loops[l + 1]; q++) bpt(&mapped, &bs, sp.pts[q * 2], sp.pts[q * 2 + 1]);
        }
        free(sp.pts);
        free(sp.loops);
        append_fill(s, fill_from(&mapped, COLS[i]));
    }
    return 1;
}

// debugging: every loop of a shape's fills with its box (px) and colour
void cc_dump_shape(Movie *m, int cid) {
    if (m->chars[cid].type != CHAR_SHAPE) return;
    Shape *s = m->chars[cid].ptr;
    for (int f = 0; f < s->nfills; f++) {
        const ShapeFill *sf = &s->fills[f];
        for (int l = 0; l < sf->nloops; l++) {
            Box lb = {1e30f, 1e30f, -1e30f, -1e30f};
            for (int i = sf->loop_start[l]; i < sf->loop_start[l + 1]; i++) {
                float x = sf->pts[i * 2], y = sf->pts[i * 2 + 1];
                if (x < lb.x0) lb.x0 = x;
                if (y < lb.y0) lb.y0 = y;
                if (x > lb.x1) lb.x1 = x;
                if (y > lb.y1) lb.y1 = y;
            }
            printf("fill %d loop %d  x %.0f..%.0f y %.0f..%.0f  #%02x%02x%02x\n", f, l, lb.x0 / 20, lb.x1 / 20,
                   lb.y0 / 20, lb.y1 / 20, sf->style.color[0], sf->style.color[1], sf->style.color[2]);
        }
    }
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
