#include "swf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#if __has_include(<zlib.h>)
#include <zlib.h>
#else   // desktop test builds without zlib's headers (the library itself is linked)
typedef unsigned long uLongf;
#define Z_OK 0
int uncompress(unsigned char *dest, uLongf *dest_len, const unsigned char *src, unsigned long src_len);
#endif

const Mat MAT_IDENTITY = {1, 0, 0, 1, 0, 0};
const CXform CX_IDENTITY = {{1, 1, 1, 1}, {0, 0, 0, 0}};

// ---- growable arrays ---------------------------------------------------------

#define VEC(T) struct { T *v; int n, cap; }
#define VPUSH(vec, x) do { \
    if ((vec).n == (vec).cap) { (vec).cap = (vec).cap ? (vec).cap * 2 : 16; \
        (vec).v = realloc((vec).v, sizeof(*(vec).v) * (vec).cap); } \
    (vec).v[(vec).n++] = (x); } while (0)

// ---- bit reader --------------------------------------------------------------

typedef struct {
    const uint8_t *d;
    size_t len;
    size_t p;   // bit position
    int bad;
} Bits;

static void b_init(Bits *b, const uint8_t *d, size_t len, size_t byte_pos) {
    b->d = d; b->len = len; b->p = byte_pos * 8; b->bad = 0;
}
static void b_align(Bits *b) { b->p = (b->p + 7) & ~(size_t)7; }
static size_t b_byte(Bits *b) { b_align(b); return b->p >> 3; }

static uint32_t ub(Bits *b, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        size_t byte = b->p >> 3;
        if (byte >= b->len) { b->bad = 1; return 0; }
        v = (v << 1) | ((b->d[byte] >> (7 - (b->p & 7))) & 1);
        b->p++;
    }
    return v;
}
static int32_t sb(Bits *b, int n) {
    if (n == 0) return 0;
    uint32_t v = ub(b, n);
    if (v & (1u << (n - 1))) return (int32_t)(v | (~0u << n));
    return (int32_t)v;
}
static float fb(Bits *b, int n) { return sb(b, n) / 65536.0f; }
static uint8_t u8(Bits *b) { b_align(b); return (uint8_t)ub(b, 8); }
static uint16_t u16(Bits *b) { uint16_t lo = u8(b); return lo | (uint16_t)(u8(b) << 8); }
static uint32_t u32(Bits *b) { uint32_t lo = u16(b); return lo | ((uint32_t)u16(b) << 16); }
static float f16(Bits *b) { return (int16_t)u16(b) / 256.0f; }

static char *cstr(Bits *b) {
    size_t s = b_byte(b), e = s;
    while (e < b->len && b->d[e]) e++;
    char *out = malloc(e - s + 1);
    memcpy(out, b->d + s, e - s);
    out[e - s] = 0;
    b->p = (e + 1) * 8;
    return out;
}

static void rect(Bits *b, int32_t r[4]) {
    b_align(b);
    int n = ub(b, 5);
    for (int i = 0; i < 4; i++) r[i] = sb(b, n);
    b_align(b);
}

static Mat matrix(Bits *b) {
    Mat m = MAT_IDENTITY;
    b_align(b);
    if (ub(b, 1)) { int n = ub(b, 5); m.a = fb(b, n); m.d = fb(b, n); }
    if (ub(b, 1)) { int n = ub(b, 5); m.b = fb(b, n); m.c = fb(b, n); }
    int n = ub(b, 5);
    m.tx = (float)sb(b, n);
    m.ty = (float)sb(b, n);
    b_align(b);
    return m;
}

static void color(Bits *b, uint8_t out[4], int alpha) {
    out[0] = u8(b); out[1] = u8(b); out[2] = u8(b);
    out[3] = alpha ? u8(b) : 255;
}

static CXform cxform(Bits *b, int alpha) {
    CXform cx = CX_IDENTITY;
    b_align(b);
    int has_add = ub(b, 1), has_mul = ub(b, 1);
    int n = ub(b, 4), cnt = alpha ? 4 : 3;
    if (has_mul) for (int i = 0; i < cnt; i++) cx.mul[i] = sb(b, n) / 256.0f;
    if (has_add) for (int i = 0; i < cnt; i++) cx.add[i] = (float)sb(b, n);
    b_align(b);
    return cx;
}

Mat mat_mul(Mat m, Mat n) {
    Mat r;
    r.a = m.a * n.a + m.c * n.b;
    r.b = m.b * n.a + m.d * n.b;
    r.c = m.a * n.c + m.c * n.d;
    r.d = m.b * n.c + m.d * n.d;
    r.tx = m.a * n.tx + m.c * n.ty + m.tx;
    r.ty = m.b * n.tx + m.d * n.ty + m.ty;
    return r;
}

CXform cx_mul(CXform o, CXform in) {
    CXform r;
    for (int i = 0; i < 4; i++) {
        r.mul[i] = o.mul[i] * in.mul[i];
        r.add[i] = o.add[i] + o.mul[i] * in.add[i];
    }
    return r;
}

// ---- tags --------------------------------------------------------------------

typedef struct { const uint8_t *d; size_t pos, end; } TagIter;

// Some decompiled files are cut short at the very end; stop cleanly there.
static int next_tag(TagIter *it, int *code, size_t *body, size_t *len) {
    if (it->pos + 2 > it->end) return 0;
    uint16_t h = it->d[it->pos] | (it->d[it->pos + 1] << 8);
    it->pos += 2;
    *code = h >> 6;
    size_t l = h & 0x3f;
    if (l == 0x3f) {
        if (it->pos + 4 > it->end) return 0;
        l = it->d[it->pos] | (it->d[it->pos + 1] << 8) | (it->d[it->pos + 2] << 16) |
            ((size_t)it->d[it->pos + 3] << 24);
        it->pos += 4;
    }
    // a tag running past the end (a file cut short): keep what's there, the parsers check bounds
    if (it->pos + l > it->end) l = it->end - it->pos;
    *body = it->pos;
    *len = l;
    it->pos += l;
    return *code != 0;
}

// ---- shapes ------------------------------------------------------------------

typedef struct { FillStyle *v; int n; } FillTable;
typedef VEC(ShapeFill) ShapeFillVec;
typedef VEC(ShapeLine) ShapeLineVec;
typedef struct { float w; uint8_t c[4]; } LineStyle;
typedef struct { LineStyle *v; int n; } LineTable;

static void read_fill(Bits *b, int ver, FillStyle *f) {
    memset(f, 0, sizeof(*f));
    int t = u8(b);
    if (t == 0x00) {
        f->type = FILL_SOLID;
        color(b, f->color, ver >= 3);
    } else if (t == 0x10 || t == 0x12 || t == 0x13) {
        f->type = t == 0x10 ? FILL_LINEAR : FILL_RADIAL;
        f->gmat = matrix(b);
        b_align(b);
        int cnt = u8(b) & 0x0f;
        if (cnt > 15) cnt = 15;
        f->nstops = cnt;
        for (int i = 0; i < cnt; i++) {
            f->ratio[i] = u8(b);
            color(b, f->stop[i], ver >= 3);
        }
        if (t == 0x13) f->focal = f16(b);
        if (cnt) memcpy(f->color, f->stop[0], 4);
    } else if (t >= 0x40 && t <= 0x43) {
        f->type = FILL_BITMAP;
        f->bitmap_id = u16(b);
        f->bitmap_clip = t & 1;           // 0x41, 0x43: clipped; else repeating
        f->bitmap_smooth = !(t & 2);      // 0x42, 0x43: not smoothed
        f->gmat = matrix(b);
        f->color[0] = 255; f->color[1] = 0; f->color[2] = 255; f->color[3] = 255;
    } else {
        b->bad = 1;
    }
}

static FillTable read_fills(Bits *b, int ver) {
    FillTable t;
    int n = u8(b);
    if (n == 0xff && ver >= 2) n = u16(b);
    t.n = n;
    t.v = calloc(n ? n : 1, sizeof(FillStyle));
    for (int i = 0; i < n && !b->bad; i++) read_fill(b, ver, &t.v[i]);
    return t;
}

static LineTable read_lines(Bits *b, int ver) {
    LineTable t;
    int n = u8(b);
    if (n == 0xff) n = u16(b);
    t.n = n;
    t.v = calloc(n ? n : 1, sizeof(LineStyle));
    for (int i = 0; i < n && !b->bad; i++) {
        t.v[i].w = u16(b);
        if (ver == 4) {
            ub(b, 2); int join = ub(b, 2); int has_fill = ub(b, 1); ub(b, 5); ub(b, 8);
            if (join == 2) u16(b);
            if (has_fill) {
                FillStyle f;
                read_fill(b, 4, &f);
                memcpy(t.v[i].c, f.color, 4);
            } else {
                color(b, t.v[i].c, 1);
            }
        } else {
            color(b, t.v[i].c, ver >= 3);
        }
    }
    return t;
}

typedef struct { float x, y; } Pt;
typedef VEC(Pt) PtVec;
typedef struct { int start, count; int64_t k0, k1; } Edge;   // into point pool
typedef VEC(Edge) EdgeVec;

static int64_t pkey(Pt p) {
    int64_t x = (int64_t)lrintf(p.x), y = (int64_t)lrintf(p.y);
    return (x << 32) ^ (y & 0xffffffff);
}

static void flatten_quad(Pt p0, Pt c, Pt p1, PtVec *out) {
    // The curve strays at most |p0 - 2c + p1| / 4 from its chord, and n segments
    // cut that by n^2. Aim for a quarter pixel (5 twips).
    float ex = p0.x - 2 * c.x + p1.x, ey = p0.y - 2 * c.y + p1.y;
    float dev = sqrtf(ex * ex + ey * ey) / 4;
    int n = (int)ceilf(sqrtf(dev / 5.0f));
    if (n < 1) n = 1;
    if (n > 16) n = 16;
    for (int i = 1; i <= n; i++) {
        float t = (float)i / n, mt = 1 - t;
        Pt q = {mt * mt * p0.x + 2 * mt * t * c.x + t * t * p1.x,
                mt * mt * p0.y + 2 * mt * t * c.y + t * t * p1.y};
        VPUSH(*out, q);
    }
}

// Per-style edge bucket for the current style table.
typedef struct { EdgeVec edges; } Bucket;

static int cmp_edge_key(const void *a, const void *b) {
    int64_t x = ((const Edge *)a)->k0, y = ((const Edge *)b)->k0;
    return x < y ? -1 : x > y;
}

// Join edges into closed loops and append a ShapeFill.
static void chain_fill(const FillStyle *style, Bucket *bk, PtVec *pool, ShapeFillVec *out) {
    int n = bk->edges.n;
    if (!n) return;
    Edge *e = bk->edges.v;
    qsort(e, n, sizeof(Edge), cmp_edge_key);
    char *used = calloc(n, 1);
    PtVec pts = {0};
    VEC(int) loops = {0};
    for (int i = 0; i < n; i++) {
        if (used[i]) continue;
        used[i] = 1;
        VPUSH(loops, pts.n);
        for (int k = 0; k < e[i].count; k++) VPUSH(pts, pool->v[e[i].start + k]);
        int64_t start = e[i].k0, cur = e[i].k1;
        while (cur != start) {
            // binary search first edge whose k0 == cur
            int lo = 0, hi = n;
            while (lo < hi) { int mid = (lo + hi) / 2; if (e[mid].k0 < cur) lo = mid + 1; else hi = mid; }
            int nxt = -1;
            for (int j = lo; j < n && e[j].k0 == cur; j++) if (!used[j]) { nxt = j; break; }
            if (nxt < 0) break;
            used[nxt] = 1;
            for (int k = 1; k < e[nxt].count; k++) VPUSH(pts, pool->v[e[nxt].start + k]);
            cur = e[nxt].k1;
        }
    }
    VPUSH(loops, pts.n);
    ShapeFill sf;
    sf.style = *style;
    sf.npoints = pts.n;
    sf.pts = (float *)pts.v;
    sf.nloops = loops.n - 1;
    sf.loop_start = loops.v;
    VPUSH(*out, sf);
    free(used);
}

static void parse_records(Bits *bp, int ver, FillTable fills, LineTable lines, Shape *s);

static Shape *parse_shape(const uint8_t *d, size_t len, size_t pos, int code) {
    int ver = code == 2 ? 1 : code == 22 ? 2 : code == 32 ? 3 : 4;
    Bits b;
    b_init(&b, d, len, pos);
    Shape *s = calloc(1, sizeof(Shape));
    s->id = u16(&b);
    int32_t r[4];
    rect(&b, r);
    if (ver == 4) { rect(&b, r); u8(&b); }
    FillTable fills = read_fills(&b, ver);
    LineTable lines = read_lines(&b, ver);
    parse_records(&b, ver, fills, lines, s);
    return s;
}

// A font glyph: shape records filled with style 1 (white; text colour is a colour transform).
static Shape *parse_glyph(const uint8_t *d, size_t len, size_t pos) {
    Bits b;
    b_init(&b, d, len, pos);
    Shape *s = calloc(1, sizeof(Shape));
    FillTable fills = {calloc(1, sizeof(FillStyle)), 1};
    fills.v[0].type = FILL_SOLID;
    memset(fills.v[0].color, 255, 4);
    LineTable lines = {calloc(1, sizeof(LineStyle)), 0};
    parse_records(&b, 1, fills, lines, s);
    return s;
}

// Shape records from the style counts on; takes ownership of the style tables.
static void parse_records(Bits *bp, int ver, FillTable fills, LineTable lines, Shape *s) {
    Bits b = *bp;
    b_align(&b);
    int nf = ub(&b, 4), nl = ub(&b, 4);

    PtVec pool = {0};
    Bucket *fb_ = calloc(fills.n + 1, sizeof(Bucket));
    Bucket *lb_ = calloc(lines.n + 1, sizeof(Bucket));
    ShapeFillVec out_fills = {0};
    ShapeLineVec out_lines = {0};

#define FLUSH() do { \
        for (int i = 1; i <= fills.n; i++) { chain_fill(&fills.v[i - 1], &fb_[i], &pool, &out_fills); \
            free(fb_[i].edges.v); } \
        for (int i = 1; i <= lines.n; i++) if (lb_[i].edges.n) { \
            ShapeLine sl; sl.width = lines.v[i - 1].w; memcpy(sl.color, lines.v[i - 1].c, 4); \
            PtVec lp = {0}; VEC(int) ps = {0}; \
            for (int k = 0; k < lb_[i].edges.n; k++) { Edge *ed = &lb_[i].edges.v[k]; VPUSH(ps, lp.n); \
                for (int q = 0; q < ed->count; q++) VPUSH(lp, pool.v[ed->start + q]); } \
            VPUSH(ps, lp.n); sl.npoints = lp.n; sl.pts = (float *)lp.v; sl.npolys = ps.n - 1; sl.poly_start = ps.v; \
            VPUSH(out_lines, sl); free(lb_[i].edges.v); } \
        free(fb_); free(lb_); free(fills.v); free(lines.v); \
    } while (0)

    int x = 0, y = 0, f0 = 0, f1 = 0, ln = 0;
    while (!b.bad) {
        if (ub(&b, 1) == 0) {
            int flags = ub(&b, 5);
            if (flags == 0) break;
            if (flags & 1) { int n = ub(&b, 5); x = sb(&b, n); y = sb(&b, n); }
            if (flags & 2) f0 = ub(&b, nf);
            if (flags & 4) f1 = ub(&b, nf);
            if (flags & 8) ln = ub(&b, nl);
            if (flags & 16) {
                FLUSH();
                fills = read_fills(&b, ver);
                lines = read_lines(&b, ver);
                b_align(&b);
                nf = ub(&b, 4); nl = ub(&b, 4);
                fb_ = calloc(fills.n + 1, sizeof(Bucket));
                lb_ = calloc(lines.n + 1, sizeof(Bucket));
                f0 = f1 = ln = 0;
            }
        } else {
            Pt p0 = {(float)x, (float)y};
            int start = pool.n;
            VPUSH(pool, p0);
            if (ub(&b, 1)) {
                int n = ub(&b, 4) + 2;
                if (ub(&b, 1)) { x += sb(&b, n); y += sb(&b, n); }
                else if (ub(&b, 1)) y += sb(&b, n);
                else x += sb(&b, n);
                Pt p1 = {(float)x, (float)y};
                VPUSH(pool, p1);
            } else {
                int n = ub(&b, 4) + 2;
                int cx = x + sb(&b, n), cy = y + sb(&b, n);
                x = cx + sb(&b, n); y = cy + sb(&b, n);
                flatten_quad(p0, (Pt){(float)cx, (float)cy}, (Pt){(float)x, (float)y}, &pool);
            }
            int count = pool.n - start;
            Pt a = pool.v[start], z = pool.v[pool.n - 1];
            if (f1 > 0 && f1 <= fills.n) {
                Edge e = {start, count, pkey(a), pkey(z)};
                VPUSH(fb_[f1].edges, e);
            }
            if (f0 > 0 && f0 <= fills.n) {
                // reversed copy so the fill is on the same side
                int rs = pool.n;
                for (int k = count - 1; k >= 0; k--) { Pt q = pool.v[start + k]; VPUSH(pool, q); }
                Edge e = {rs, count, pkey(z), pkey(a)};
                VPUSH(fb_[f0].edges, e);
            }
            if (ln > 0 && ln <= lines.n) {
                Edge e = {start, count, 0, 0};
                VPUSH(lb_[ln].edges, e);
            }
        }
    }
    FLUSH();
#undef FLUSH
    free(pool.v);
    s->nfills = out_fills.n;
    s->fills = out_fills.v;
    s->nlines = out_lines.n;
    s->lines = out_lines.v;
}

// DefineFont2 / DefineFont3 (code 48 / 75).
static Font *parse_font(const uint8_t *d, size_t len, size_t pos, size_t end, int code) {
    Bits b;
    b_init(&b, d, end, pos);
    Font *f = calloc(1, sizeof(Font));
    f->id = u16(&b);
    int flags = u8(&b);
    u8(&b);   // language
    int nl = u8(&b);
    f->name = malloc(nl + 1);
    for (int i = 0; i < nl; i++) f->name[i] = u8(&b);
    f->name[nl] = 0;
    f->bold = flags & 1;
    f->italic = (flags >> 1) & 1;
    f->em = code == 75 ? 20480 : 1024;
    int wide_offsets = flags & 0x08, wide_codes = flags & 0x04;
    int n = u16(&b);
    f->nglyphs = n;
    f->glyphs = calloc(n ? n : 1, sizeof(Shape *));
    f->codes = calloc(n ? n : 1, sizeof(uint16_t));
    f->advance = calloc(n ? n : 1, sizeof(int16_t));
    size_t table = b_byte(&b);
    uint32_t *offs = calloc(n + 1, sizeof(uint32_t));
    for (int i = 0; i <= n && !b.bad; i++)   // glyph offsets, then the code table's
        offs[i] = wide_offsets ? u32(&b) : u16(&b);
    if (n && !b.bad) {
        for (int i = 0; i < n; i++)
            if (table + offs[i] < end) f->glyphs[i] = parse_glyph(d, end, table + offs[i]);
        Bits c;
        b_init(&c, d, end, table + offs[n]);
        for (int i = 0; i < n; i++) f->codes[i] = wide_codes ? u16(&c) : u8(&c);
        if (flags & 0x80) {   // layout
            f->ascent = u16(&c);
            f->descent = u16(&c);
            f->leading = (int16_t)u16(&c);
            for (int i = 0; i < n; i++) f->advance[i] = (int16_t)u16(&c);
            f->has_layout = !c.bad;
        }
    }
    free(offs);
    (void)len;
    return f;
}

void (*swf_bitmap_loaded)(Bitmap *bm);

// DefineBitsLossless (20) / DefineBitsLossless2 (36).
static Bitmap *parse_lossless(const uint8_t *d, size_t body, size_t len, int code) {
    if (len < 7) return NULL;
    const uint8_t *p = d + body;
    int fmt = p[2], w = p[3] | (p[4] << 8), h = p[5] | (p[6] << 8);
    int alpha = code == 36;
    size_t hdr = fmt == 3 ? 8 : 7;
    if (w <= 0 || h <= 0 || len < hdr || (fmt != 3 && fmt != 4 && fmt != 5)) return NULL;
    int ncolors = fmt == 3 ? p[7] + 1 : 0;
    int csize = alpha ? 4 : 3;
    size_t stride = fmt == 3 ? (size_t)(w + 3) & ~3u : fmt == 4 ? ((size_t)w * 2 + 3) & ~3u : (size_t)w * 4;
    uLongf raw_len = (uLongf)(ncolors * csize + stride * h);
    uint8_t *raw = malloc(raw_len);
    if (!raw) return NULL;
    if (uncompress(raw, &raw_len, p + hdr, len - hdr) != Z_OK) { free(raw); return NULL; }
    // 32-bit pixels are turned into RGBA where they are: a 2048x2048 sky is 16 MB, and a
    // second buffer of that size may not be there on the Vita
    uint8_t *rgba = fmt == 5 ? raw : malloc((size_t)w * h * 4);
    Bitmap *bm = rgba ? calloc(1, sizeof(Bitmap)) : NULL;
    if (!bm) { if (rgba != raw) free(rgba); free(raw); return NULL; }
    bm->id = p[0] | (p[1] << 8);
    bm->w = w;
    bm->h = h;
    bm->rgba = rgba;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = raw + ncolors * csize + stride * y;
        for (int x = 0; x < w; x++) {
            uint8_t *o = bm->rgba + ((size_t)y * w + x) * 4;
            if (fmt == 3) {
                int i = row[x];
                const uint8_t *c = raw + (i < ncolors ? i : 0) * csize;
                o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = alpha ? c[3] : 255;
            } else if (fmt == 4) {
                int v = row[x * 2] << 8 | row[x * 2 + 1];
                o[0] = ((v >> 10) & 31) * 255 / 31; o[1] = ((v >> 5) & 31) * 255 / 31; o[2] = (v & 31) * 255 / 31; o[3] = 255;
            } else {
                const uint8_t *c = row + x * 4;   // A (or reserved), R, G, B; o is the same pixel
                uint8_t a = c[0], r = c[1], g = c[2], b = c[3];
                o[0] = r; o[1] = g; o[2] = b; o[3] = alpha ? a : 255;
            }
            if (alpha && o[3] && o[3] < 255)   // Lossless2 colours are premultiplied
                for (int k = 0; k < 3; k++) { int v = o[k] * 255 / o[3]; o[k] = v > 255 ? 255 : v; }
        }
    }
    if (rgba != raw) free(raw);
    if (swf_bitmap_loaded) swf_bitmap_loaded(bm);
    return bm;
}

static void resolve_fills(Movie *m, Shape *s) {
    for (int i = 0; i < s->nfills; i++) {
        FillStyle *f = &s->fills[i].style;
        if (f->type == FILL_BITMAP && m->chars[f->bitmap_id].type == CHAR_BITMAP) f->bitmap = m->chars[f->bitmap_id].ptr;
    }
}

int font_glyph(const Font *f, int code) {
    for (int i = 0; i < f->nglyphs; i++)
        if (f->codes[i] == code) return i;
    return -1;
}

// ---- timelines ---------------------------------------------------------------

typedef VEC(Place) PlaceVec;

static int has_stop(const uint8_t *d, size_t pos, size_t len) {
    size_t end = pos + len;
    while (pos < end) {
        uint8_t op = d[pos++];
        if (op == 0) break;
        if (op == 0x07) return 1;
        if (op >= 0x80) {
            if (pos + 2 > end) break;
            pos += 2 + (d[pos] | (d[pos + 1] << 8));
        }
    }
    return 0;
}

// Collision segment clips carry a one-line script: Push "n_type", N; SetVariable.
static int parse_ntype(const uint8_t *d, size_t pos, size_t len, int *out) {
    static const char key[] = "n_type";
    size_t end = pos + len;
    if (len < 3 || d[pos] != 0x96) return 0;
    size_t plen = d[pos + 1] | (d[pos + 2] << 8), p = pos + 3, pend = p + plen;
    if (pend + 1 > end || d[pend] != 0x1D) return 0;
    if (p + sizeof key + 1 > pend || d[p] != 0 || memcmp(d + p + 1, key, sizeof key)) return 0;
    p += 1 + sizeof key;
    int type = d[p++];
    if (type == 7 && p + 4 <= pend) { *out = (int32_t)(d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24)); return 1; }
    if (type == 1 && p + 4 <= pend) { float f; memcpy(&f, d + p, 4); *out = (int)f; return 1; }
    if (type == 6 && p + 8 <= pend) {   // AVM1 doubles store the high word first
        uint8_t b[8];
        memcpy(b, d + p + 4, 4);
        memcpy(b + 4, d + p, 4);
        double v; memcpy(&v, b, 8); *out = (int)v; return 1;
    }
    return 0;
}

// Looks for the depth-sorting registration calls in a clip's first-frame script.
// Only Push and ConstantPool are decoded: the calls look like
//   Push this; GetVariable; Push 1, "_root"; GetVariable; Push "f_AddNeutral"; CallMethod
//   Push 300, "this"; GetVariable; Push 2, "_root"; GetVariable; Push "f_Depth"; CallMethod
static void scan_depth_calls(const uint8_t *d, size_t pos, size_t len, Sprite *s) {
    const char *pool[256];
    int npool = 0;
    size_t end = pos + len;
    int have_num_before_this = 0;
    float num_before_this = 0;
    while (pos < end) {
        uint8_t op = d[pos++];
        if (op == 0) break;
        size_t alen = 0;
        if (op >= 0x80) {
            if (pos + 2 > end) break;
            alen = d[pos] | (d[pos + 1] << 8);
            pos += 2;
            if (pos + alen > end) break;
        }
        if (op == 0x88 && alen >= 2) {   // ConstantPool
            int n = d[pos] | (d[pos + 1] << 8);
            size_t p = pos + 2;
            for (int i = 0; i < n && p < pos + alen; i++) {
                if (npool < 256) pool[npool++] = (const char *)d + p;
                while (p < pos + alen && d[p]) p++;
                p++;
            }
        } else if (op == 0x96 || (op >= 0xa0 && op <= 0xa3)) {   // Push (0xa0-a3: the game's own Push variants)
            size_t p = pos, pend = pos + alen;
            int have_num = 0;
            float num = 0;
            while (p < pend) {
                int t = d[p++];
                const char *str = NULL;
                if (t == 0) { str = (const char *)d + p; while (p < pend && d[p]) p++; p++; }
                else if (t == 1) { memcpy(&num, d + p, 4); have_num = 1; p += 4; continue; }
                else if (t == 6) {
                    uint8_t b[8]; double v;
                    memcpy(b, d + p + 4, 4); memcpy(b + 4, d + p, 4); memcpy(&v, b, 8);
                    num = (float)v; have_num = 1; p += 8; continue;
                }
                else if (t == 7) { num = (float)(int32_t)(d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24)); have_num = 1; p += 4; continue; }
                else if (t == 8) { str = d[p] < npool ? pool[d[p]] : NULL; p++; }
                else if (t == 9) { int i = d[p] | (d[p + 1] << 8); str = i < npool ? pool[i] : NULL; p += 2; }
                else if (t == 4 || t == 5) { p++; have_num = 0; continue; }
                else if (t == 2 || t == 3) { have_num = 0; continue; }
                else break;
                if (!str) { have_num = 0; continue; }
                if (!strcmp(str, "this")) { have_num_before_this = have_num; num_before_this = num; }
                else if (!strcmp(str, "f_AddNeutral") || !strcmp(str, "f_AddObject")) {
                    if (s->depth_sort == DEPTH_NONE) s->depth_sort = DEPTH_OWN_Y;
                } else if (!strcmp(str, "f_Depth") && have_num_before_this) {
                    s->depth_sort = DEPTH_FIXED;
                    s->depth_y = num_before_this;
                }
                have_num = 0;
            }
        }
        pos += alen;
    }
}

static Place *display_find(PlaceVec *dl, int depth) {
    for (int i = 0; i < dl->n; i++) if (dl->v[i].depth == depth) return &dl->v[i];
    return NULL;
}

static void display_remove(PlaceVec *dl, int depth) {
    for (int i = 0; i < dl->n; i++)
        if (dl->v[i].depth == depth) { dl->v[i] = dl->v[--dl->n]; return; }
}

static int intern_name(Movie *m, char *s) {
    for (int i = 1; i < m->nnames; i++)
        if (!strcmp(m->names[i], s)) { free(s); return i; }
    if (m->nnames >= 65535) { free(s); return 0; }
    m->names = realloc(m->names, sizeof(char *) * (m->nnames + 1));
    m->names[m->nnames] = s;
    return m->nnames++;
}

static uint32_t next_uid = 1;

static void parse_place(Movie *mv, const uint8_t *d, size_t len, size_t pos, size_t end, int code, PlaceVec *dl) {
    Bits b;
    b_init(&b, d, len, pos);
    int flags = u8(&b), flags2 = 0;
    if (code == 70) flags2 = u8(&b);
    int depth = u16(&b);
    if (code == 70 && ((flags2 & 0x08) || ((flags2 & 0x10) && (flags & 0x02)))) free(cstr(&b));
    int move = flags & 1;
    Place *old = display_find(dl, depth);
    Place p;
    memset(&p, 0, sizeof(p));
    p.m = MAT_IDENTITY;
    p.cx = CX_IDENTITY;
    if (move && old) p = *old;
    p.depth = depth;
    if (!(move && old)) p.uid = next_uid++;   // a fresh instance
    if (flags & 0x02) {
        int cid = u16(&b);
        if (move && old && old->cid != cid) {
            Mat keep = old->m;
            memset(&p, 0, sizeof(p));
            p.depth = depth; p.m = keep; p.cx = CX_IDENTITY;
            p.uid = next_uid++;   // replaced by another character
        }
        p.cid = cid;
    }
    if (flags & 0x04) p.m = matrix(&b);
    if (flags & 0x08) p.cx = cxform(&b, code == 26 || code == 70);
    if (flags & 0x10) p.ratio = u16(&b);
    if (flags & 0x20) p.name = intern_name(mv, cstr(&b));
    if (flags & 0x40) p.clip = u16(&b);
    if (flags & 0x80) {   // clip actions run from here to the end of the tag
        size_t at = b_byte(&b);
        if (at < end) { p.events_off = (uint32_t)at; p.events_len = (uint32_t)(end - at); }
    }
    if (b.bad) return;
    if (old) *old = p; else VPUSH(*dl, p);
}

static int cmp_depth(const void *a, const void *b) {
    return (int)((const Place *)a)->depth - (int)((const Place *)b)->depth;
}

typedef VEC(Frame) FrameVec;

static void push_frame(FrameVec *frames, PlaceVec *dl) {
    Frame f;
    f.count = dl->n;
    f.places = malloc(sizeof(Place) * (dl->n ? dl->n : 1));
    memcpy(f.places, dl->v, sizeof(Place) * dl->n);
    qsort(f.places, f.count, sizeof(Place), cmp_depth);
    VPUSH(*frames, f);
}

static void parse_timeline(Movie *mv, const uint8_t *d, size_t len, size_t pos, size_t end, Sprite *s) {
    PlaceVec dl = {0};
    FrameVec frames = {0};
    VEC(Label) labels = {0};
    VEC(ActionBlock) actions = {0};
    s->first_stop = -1;
    TagIter it = {d, pos, end < len ? end : len};
    int code;
    size_t body, l;
    int pending = 0;   // tags since the last ShowFrame
    while (next_tag(&it, &code, &body, &l)) {
        // a frame being built: something placed, removed, labelled or scripted since the last ShowFrame
        if (code == 26 || code == 70 || code == 28 || code == 5 || code == 43 || code == 12) pending = 1;
        if (code == 26 || code == 70) {
            parse_place(mv, d, len, body, body + l, code, &dl);
        } else if (code == 28 || code == 5) {
            size_t o = code == 28 ? body : body + 2;
            display_remove(&dl, d[o] | (d[o + 1] << 8));
        } else if (code == 43) {
            Bits b;
            b_init(&b, d, len, body);
            Label lab = {cstr(&b), frames.n};
            VPUSH(labels, lab);
        } else if (code == 12) {
            ActionBlock ab = {frames.n, (uint32_t)body, (uint32_t)l};
            VPUSH(actions, ab);
            if (s->first_stop < 0 && has_stop(d, body, l)) s->first_stop = frames.n;
            if (frames.n == 0) {
                parse_ntype(d, body, l, &s->n_type);
                scan_depth_calls(d, body, l, s);
            }
        } else if (code == 1) {
            push_frame(&frames, &dl);
            pending = 0;
        }
    }
    // Level files place their layers on the root but never show a frame, and files cut
    // short (MAIN, PLAYER, EBARBOSS...) lose their last ShowFrame: close that frame.
    if ((frames.n == 0 && dl.n) || pending) push_frame(&frames, &dl);
    free(dl.v);
    s->nframes = frames.n;
    s->frames = frames.v;
    s->nlabels = labels.n;
    s->labels = labels.v;
    s->nactions = actions.n;
    s->actions = actions.v;
}

int sprite_frame_at(const Sprite *s, int tick) {
    if (s->nframes <= 0) return 0;
    if (s->first_stop >= 0) return tick < s->first_stop ? tick : s->first_stop;
    return tick % s->nframes;
}

int sprite_find_label(const Sprite *s, const char *name) {
    for (int i = 0; i < s->nlabels; i++)
        if (!strcmp(s->labels[i].name, name)) return s->labels[i].frame;
    return -1;
}

// ---- movie -------------------------------------------------------------------

Movie *swf_load(const char *path, char *err, int errlen) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "Can't open %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(len);
    if (fread(d, 1, len, f) != (size_t)len) { fclose(f); free(d); snprintf(err, errlen, "Read failed"); return NULL; }
    fclose(f);
    if (len < 16 || d[0] != 'F' || d[1] != 'W' || d[2] != 'S') {
        free(d);
        snprintf(err, errlen, "%s is not an uncompressed SWF", path);
        return NULL;
    }
    Movie *m = calloc(1, sizeof(Movie));
    m->version = d[3];
    const char *base = strrchr(path, '/');
    m->is_player = !strcasecmp(base ? base + 1 : path, "PLAYER.swf");
    m->names = malloc(sizeof(char *));
    m->names[0] = strdup("");
    m->nnames = 1;
    uint32_t declared = d[4] | (d[5] << 8) | (d[6] << 16) | ((uint32_t)d[7] << 24);
    m->truncated = declared > (uint32_t)len;
    Bits b;
    b_init(&b, d, len, 8);
    int32_t r[4];
    rect(&b, r);
    m->stage_w = (r[1] - r[0]) / 20;
    m->stage_h = (r[3] - r[2]) / 20;
    m->fps = f16(&b);
    u16(&b);
    size_t start = b_byte(&b);

    TagIter it = {d, start, (size_t)len};
    int code;
    size_t body, l;
    while (next_tag(&it, &code, &body, &l)) {
        if (code == 2 || code == 22 || code == 32 || code == 83) {
            Shape *s = parse_shape(d, body + l, body, code);
            m->chars[s->id].type = CHAR_SHAPE;
            m->chars[s->id].ptr = s;
        } else if (code == 148 && l >= 20) {
            // The game's own tag for the invisible boxes levels use as trigger and hit
            // areas (hitzone, zone, ripoff): u32 id, then xmin, xmax, ymin, ymax as s32
            // twips, then pixel data we don't need. Scripts read the box via _width,
            // _height and hitTest, so it must count toward a clip's bounds.
            const uint8_t *q = d + body;
            uint16_t id = q[0] | (q[1] << 8);
            int32_t r[4];
            for (int k = 0; k < 4; k++)
                r[k] = (int32_t)(q[4 + 4 * k] | (q[5 + 4 * k] << 8) | (q[6 + 4 * k] << 16) | ((uint32_t)q[7 + 4 * k] << 24));
            if (m->chars[id].type == CHAR_NONE && r[0] <= r[1] && r[2] <= r[3]) {
                Shape *s = calloc(1, sizeof(Shape));
                s->id = id;
                s->has_box = 1;
                s->box[0] = (float)r[0]; s->box[1] = (float)r[2];
                s->box[2] = (float)r[1]; s->box[3] = (float)r[3];
                m->chars[id].type = CHAR_SHAPE;
                m->chars[id].ptr = s;
            }
        } else if (code == 39 && l >= 4) {   // (a cut-short file can end inside the header)
            Sprite *s = calloc(1, sizeof(Sprite));
            s->id = d[body] | (d[body + 1] << 8);
            parse_timeline(m, d, len, body + 4, body + l, s);
            m->chars[s->id].type = CHAR_SPRITE;
            m->chars[s->id].ptr = s;
        } else if (code == 9 && l >= 3) {
            memcpy(m->bg, d + body, 3);
        } else if (code == 37) {   // DefineEditText
            Bits eb;
            b_init(&eb, d, body + l, body);
            EditText *t = calloc(1, sizeof(EditText));
            t->id = u16(&eb);
            int32_t r[4];
            rect(&eb, r);
            memcpy(t->bounds, r, sizeof r);
            int f1 = u8(&eb), f2 = u8(&eb);
            t->wordwrap = (f1 >> 6) & 1;
            t->multiline = (f1 >> 5) & 1;
            t->html = (f2 >> 1) & 1;
            if (f1 & 0x01) { t->font = u16(&eb); t->height = u16(&eb); }        // HasFont
            else if (f2 & 0x80) { free(cstr(&eb)); t->height = u16(&eb); }     // HasFontClass
            if (f1 & 0x04) color(&eb, t->color, 1);                              // HasTextColor
            else { t->color[3] = 255; }
            if (f1 & 0x02) u16(&eb);                                             // HasMaxLength
            if (f2 & 0x20) {   // HasLayout
                t->align = u8(&eb);
                t->left_margin = u16(&eb);
                t->right_margin = u16(&eb);
                t->indent = u16(&eb);
                t->leading = (int16_t)u16(&eb);
            }
            char *var = cstr(&eb);
            if (var[0]) t->var = var; else free(var);
            if (f1 & 0x80) t->text = cstr(&eb);                                  // HasText
            if (!t->text) t->text = strdup("");
            m->chars[t->id].type = CHAR_EDITTEXT;
            m->chars[t->id].ptr = t;
        } else if (code == 20 || code == 36) {   // DefineBitsLossless / Lossless2
            Bitmap *bm = parse_lossless(d, body, l, code);
            if (bm) {
                m->chars[bm->id].type = CHAR_BITMAP;
                m->chars[bm->id].ptr = bm;
            }
        } else if (code == 48 || code == 75) {   // DefineFont2 / DefineFont3
            Font *f = parse_font(d, len, body, body + l, code);
            m->chars[f->id].type = CHAR_FONT;
            m->chars[f->id].ptr = f;
        } else if (code == 59 && l >= 2) {   // DoInitAction
            ActionBlock ab = {d[body] | (d[body + 1] << 8), (uint32_t)(body + 2), (uint32_t)(l - 2)};
            m->init = realloc(m->init, sizeof(ActionBlock) * (m->ninit + 1));
            m->init[m->ninit++] = ab;
        } else if (code == 56) {
            Bits eb;
            b_init(&eb, d, body + l, body);
            int n = u16(&eb);
            for (int i = 0; i < n && !eb.bad; i++) {
                int cid = u16(&eb);
                int name = intern_name(m, cstr(&eb));
                if (!eb.bad) m->chars[cid].export = name;
            }
        }
    }
    parse_timeline(m, d, len, start, len, &m->root);
    for (int i = 0; i < 65536; i++)
        if (m->chars[i].type == CHAR_SHAPE) resolve_fills(m, m->chars[i].ptr);
    m->data = d;   // kept: the scripts run straight from the file
    m->len = len;
    return m;
}

static void free_sprite(Sprite *s) {
    for (int i = 0; i < s->nframes; i++) free(s->frames[i].places);
    for (int i = 0; i < s->nlabels; i++) free(s->labels[i].name);
    free(s->frames);
    free(s->labels);
    free(s->actions);
}

static void free_shape(Shape *s) {
    if (!s) return;
    for (int k = 0; k < s->nfills; k++) { free(s->fills[k].pts); free(s->fills[k].loop_start); }
    for (int k = 0; k < s->nlines; k++) { free(s->lines[k].pts); free(s->lines[k].poly_start); }
    free(s->fills); free(s->lines); free(s->mesh.xy); free(s->mesh.rgba);
    for (int k = 0; k < s->nbfills; k++) { free(s->bfills[k].mesh.xy); free(s->bfills[k].mesh.uv); }
    free(s->bfills);
    free(s);
}

void swf_free(Movie *m) {
    if (!m) return;
    for (int i = 0; i < 65536; i++) {
        if (m->chars[i].type == CHAR_SHAPE) {
            free_shape(m->chars[i].ptr);
        } else if (m->chars[i].type == CHAR_BITMAP) {
            Bitmap *bm = m->chars[i].ptr;   // the platform frees bm->tex first
            free(bm->rgba);
            free(bm);
        } else if (m->chars[i].type == CHAR_FONT) {
            Font *f = m->chars[i].ptr;
            for (int k = 0; k < f->nglyphs; k++) free_shape(f->glyphs[k]);
            free(f->glyphs); free(f->codes); free(f->advance); free(f->name);
            free(f);
        } else if (m->chars[i].type == CHAR_SPRITE) {
            free_sprite(m->chars[i].ptr);
            free(m->chars[i].ptr);
        } else if (m->chars[i].type == CHAR_EDITTEXT) {
            EditText *t = m->chars[i].ptr;
            free(t->var);
            free(t->text);
            free(t);
        }
    }
    free_sprite(&m->root);
    for (int i = 0; i < m->nnames; i++) free(m->names[i]);
    free(m->names);
    free(m->init);
    free(m->data);
    free(m);
}
