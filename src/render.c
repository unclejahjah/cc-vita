#include "render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tesselator.h"

RenderStats render_stats;

// ---- tessellation (once per shape, cached) ---------------------------------------

typedef struct {
    float *xy;
    uint8_t *rgba;
    int n, cap;
} MeshBuf;

static void mesh_push(MeshBuf *mb, float x, float y, const uint8_t c[4]) {
    if (mb->n == mb->cap) {
        mb->cap = mb->cap ? mb->cap * 2 : 64;
        mb->xy = realloc(mb->xy, sizeof(float) * 2 * mb->cap);
        mb->rgba = realloc(mb->rgba, 4 * mb->cap);
    }
    mb->xy[mb->n * 2] = x;
    mb->xy[mb->n * 2 + 1] = y;
    memcpy(mb->rgba + mb->n * 4, c, 4);
    mb->n++;
}

static void gradient_color(const FillStyle *f, float x, float y, uint8_t out[4]) {
    // Map shape space into the gradient square (-16384..16384) with the inverse matrix.
    const Mat *g = &f->gmat;
    float det = g->a * g->d - g->b * g->c;
    if (fabsf(det) < 1e-12f || f->nstops == 0) { memcpy(out, f->color, 4); return; }
    float px = x - g->tx, py = y - g->ty;
    float u = (g->d * px - g->c * py) / det;
    float v = (-g->b * px + g->a * py) / det;
    float t = f->type == FILL_LINEAR ? (u + 16384.0f) / 32768.0f : sqrtf(u * u + v * v) / 16384.0f;
    float r = t * 255.0f;
    if (r <= f->ratio[0]) { memcpy(out, f->stop[0], 4); return; }
    for (int i = 1; i < f->nstops; i++) {
        if (r <= f->ratio[i]) {
            float span = (float)(f->ratio[i] - f->ratio[i - 1]);
            float k = span > 0 ? (r - f->ratio[i - 1]) / span : 1.0f;
            for (int c = 0; c < 4; c++)
                out[c] = (uint8_t)(f->stop[i - 1][c] + (f->stop[i][c] - f->stop[i - 1][c]) * k);
            return;
        }
    }
    memcpy(out, f->stop[f->nstops - 1], 4);
}

static void tessellate_fill(TESStesselator *tess, const ShapeFill *sf, MeshBuf *mb) {
    for (int l = 0; l < sf->nloops; l++) {
        int s = sf->loop_start[l], e = sf->loop_start[l + 1];
        if (e - s >= 3) tessAddContour(tess, 2, sf->pts + s * 2, sizeof(float) * 2, e - s);
    }
    if (!tessTesselate(tess, TESS_WINDING_ODD, TESS_POLYGONS, 3, 2, NULL)) return;
    const float *verts = tessGetVertices(tess);
    const TESSindex *elems = tessGetElements(tess);
    int ne = tessGetElementCount(tess);
    for (int i = 0; i < ne; i++) {
        const TESSindex *tri = &elems[i * 3];
        if (tri[0] == TESS_UNDEF || tri[1] == TESS_UNDEF || tri[2] == TESS_UNDEF) continue;
        for (int k = 0; k < 3; k++) {
            float x = verts[tri[k] * 2], y = verts[tri[k] * 2 + 1];
            uint8_t c[4];
            if (sf->style.type == FILL_LINEAR || sf->style.type == FILL_RADIAL) gradient_color(&sf->style, x, y, c);
            else memcpy(c, sf->style.color, 4);
            mesh_push(mb, x, y, c);
        }
    }
}

static void tessellate_line(const ShapeLine *sl, MeshBuf *mb) {
    float hw = (sl->width < 20 ? 20 : sl->width) * 0.5f;
    for (int p = 0; p < sl->npolys; p++) {
        for (int i = sl->poly_start[p]; i + 1 < sl->poly_start[p + 1]; i++) {
            float x0 = sl->pts[i * 2], y0 = sl->pts[i * 2 + 1];
            float x1 = sl->pts[i * 2 + 2], y1 = sl->pts[i * 2 + 3];
            float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-3f) continue;
            float nx = -dy / len * hw, ny = dx / len * hw;
            // extend each end by half the width so joints overlap
            float ex = dx / len * hw, ey = dy / len * hw;
            x0 -= ex; y0 -= ey; x1 += ex; y1 += ey;
            mesh_push(mb, x0 + nx, y0 + ny, sl->color);
            mesh_push(mb, x1 + nx, y1 + ny, sl->color);
            mesh_push(mb, x1 - nx, y1 - ny, sl->color);
            mesh_push(mb, x0 + nx, y0 + ny, sl->color);
            mesh_push(mb, x1 - nx, y1 - ny, sl->color);
            mesh_push(mb, x0 - nx, y0 - ny, sl->color);
        }
    }
}

static TESStesselator *g_tess;

static void grow_bounds(float bb[4], const float *xy, int n) {
    for (int i = 0; i < n; i++) {
        float x = xy[i * 2], y = xy[i * 2 + 1];
        if (x < bb[0]) bb[0] = x;
        if (y < bb[1]) bb[1] = y;
        if (x > bb[2]) bb[2] = x;
        if (y > bb[3]) bb[3] = y;
    }
}

// A bitmap fill's triangles, with texture coordinates from its matrix (bitmap pixels -> twips).
static void tessellate_bitmap_fill(TESStesselator *tess, const ShapeFill *sf, BitmapFill *bf) {
    MeshBuf mb = {0};
    tessellate_fill(tess, sf, &mb);
    free(mb.rgba);
    const FillStyle *st = &sf->style;
    const Mat *g = &st->gmat;
    float det = g->a * g->d - g->b * g->c;
    bf->bitmap = st->bitmap;
    bf->clip = st->bitmap_clip;
    bf->smooth = st->bitmap_smooth;
    bf->mesh.nverts = mb.n;
    bf->mesh.xy = mb.xy;
    bf->mesh.uv = malloc(sizeof(float) * 2 * (mb.n ? mb.n : 1));
    for (int i = 0; i < mb.n; i++) {
        float px = mb.xy[i * 2] - g->tx, py = mb.xy[i * 2 + 1] - g->ty;
        float u = 0, v = 0;
        if (fabsf(det) > 1e-12f) {
            u = (g->d * px - g->c * py) / det;
            v = (-g->b * px + g->a * py) / det;
        }
        bf->mesh.uv[i * 2] = u / st->bitmap->w;
        bf->mesh.uv[i * 2 + 1] = v / st->bitmap->h;
    }
    float *bb = bf->mesh.bounds;
    bb[0] = bb[1] = 1e30f;
    bb[2] = bb[3] = -1e30f;
    grow_bounds(bb, mb.xy, mb.n);
}

static void tessellate_shape(Shape *s) {
    if (s->tessellated) return;
    if (!g_tess) g_tess = tessNewTess(NULL);
    MeshBuf mb = {0};
    for (int i = 0; i < s->nfills; i++) {
        const ShapeFill *sf = &s->fills[i];
        if (sf->style.type == FILL_BITMAP && sf->style.bitmap) {
            s->bfills = realloc(s->bfills, sizeof(BitmapFill) * (s->nbfills + 1));
            BitmapFill *bf = &s->bfills[s->nbfills++];
            memset(bf, 0, sizeof *bf);
            tessellate_bitmap_fill(g_tess, sf, bf);
        } else {
            tessellate_fill(g_tess, sf, &mb);
        }
    }
    for (int i = 0; i < s->nlines; i++) tessellate_line(&s->lines[i], &mb);
    s->mesh.nverts = mb.n;
    s->mesh.xy = mb.xy;
    s->mesh.rgba = mb.rgba;
    float *bb = s->mesh.bounds;   // of everything, bitmap fills included
    bb[0] = bb[1] = 1e30f;
    bb[2] = bb[3] = -1e30f;
    grow_bounds(bb, mb.xy, mb.n);
    for (int i = 0; i < s->nbfills; i++) grow_bounds(bb, s->bfills[i].mesh.xy, s->bfills[i].mesh.nverts);
    if (s->has_box) {
        float corners[4] = {s->box[0], s->box[1], s->box[2], s->box[3]};
        grow_bounds(bb, corners, 2);
    }
    s->tessellated = 1;
    render_stats.shapes_tessellated++;
}

// Every shape of a movie, font glyphs included: calls fn(shape, ctx).
static void each_shape(Movie *m, void (*fn)(Shape *s, void *ctx), void *ctx) {
    for (int i = 0; i < 65536; i++) {
        if (m->chars[i].type == CHAR_SHAPE) fn(m->chars[i].ptr, ctx);
        else if (m->chars[i].type == CHAR_FONT) {
            Font *f = m->chars[i].ptr;
            for (int k = 0; k < f->nglyphs; k++)
                if (f->glyphs[k]) fn(f->glyphs[k], ctx);
        }
    }
}

static void tessellate_cb(Shape *s, void *ctx) { (void)ctx; tessellate_shape(s); }

void render_tessellate_all(Movie *m) { each_shape(m, tessellate_cb, NULL); }

static inline uint32_t pack(const uint8_t *p) {
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

static void count_cb(Shape *s, void *ctx) {
    size_t *bytes = ctx;
    if (s->mesh.xy) *bytes += s->mesh.nverts * sizeof(Vertex);
    for (int i = 0; i < s->nbfills; i++)
        if (s->bfills[i].mesh.xy) *bytes += s->bfills[i].mesh.nverts * sizeof(TexVertex);
}

static void upload_cb(Shape *s, void *ctx) {
    uint8_t **p = ctx;
    Mesh *mesh = &s->mesh;
    if (mesh->nverts && mesh->xy) {
        Vertex *v = (Vertex *)*p;
        for (int k = 0; k < mesh->nverts; k++)
            v[k] = (Vertex){mesh->xy[k * 2], mesh->xy[k * 2 + 1], 0.5f, pack(mesh->rgba + k * 4)};
        mesh->gpu = v;
        *p += mesh->nverts * sizeof(Vertex);
    }
    for (int i = 0; i < s->nbfills; i++) {
        Mesh *bm = &s->bfills[i].mesh;
        if (!bm->nverts || !bm->xy) continue;
        TexVertex *t = (TexVertex *)*p;
        for (int k = 0; k < bm->nverts; k++)
            t[k] = (TexVertex){bm->xy[k * 2], bm->xy[k * 2 + 1], 0.5f, bm->uv[k * 2], bm->uv[k * 2 + 1]};
        bm->gpu = t;
        *p += bm->nverts * sizeof(TexVertex);
    }
}

void *render_upload(Movie *m, void *(*alloc)(size_t bytes, void *ctx), void *ctx) {
    size_t total = 0;
    each_shape(m, count_cb, &total);
    if (!total) return NULL;
    uint8_t *block = alloc(total, ctx);
    if (!block) return NULL;
    uint8_t *p = block;
    each_shape(m, upload_cb, &p);
    return block;
}

static void release_cb(Shape *s, void *ctx) {
    (void)ctx;
    if (!s->tessellated || (s->mesh.nverts && !s->mesh.gpu)) return;
    for (int i = 0; i < s->nbfills; i++) {
        Mesh *bm = &s->bfills[i].mesh;
        if (bm->nverts && !bm->gpu) continue;
        free(bm->xy);
        free(bm->uv);
        bm->xy = bm->uv = NULL;
    }
    for (int k = 0; k < s->nfills; k++) { free(s->fills[k].pts); free(s->fills[k].loop_start); }
    for (int k = 0; k < s->nlines; k++) { free(s->lines[k].pts); free(s->lines[k].poly_start); }
    free(s->fills);
    free(s->lines);
    s->fills = NULL;
    s->lines = NULL;
    s->nfills = s->nlines = 0;
    free(s->mesh.xy);   // the colours stay: colour transforms recolour from them
    s->mesh.xy = NULL;
}

void render_release_cpu(Movie *m) { each_shape(m, release_cb, NULL); }

// ---- per-frame draw list ----------------------------------------------------------

// Start a new draw command unless the current one already has this state.
static void set_cmd(DrawList *dl, int ref, int op) {
    if (dl->ncmd) {
        DrawCmd *last = &dl->cmd[dl->ncmd - 1];
        if (last->ref == ref && last->op == op) return;
        if (last->count == 0) { last->ref = ref; last->op = op; return; }
    }
    if (dl->ncmd == dl->cmdcap) {
        dl->cmdcap = dl->cmdcap ? dl->cmdcap * 2 : 64;
        dl->cmd = realloc(dl->cmd, sizeof(DrawCmd) * dl->cmdcap);
    }
    dl->cmd[dl->ncmd++] = (DrawCmd){dl->n, 0, ref, op};
}

void render_begin(DrawList *out) {
    out->n = 0;
    out->ncv = 0;
    out->ncmd = 0;
    out->level = 0;
    out->masking = 0;
    out->nparts = 0;
    out->nmask = 0;
    out->heat_at = -1;
    set_cmd(out, 0, STENCIL_KEEP);
    memset(&render_stats.triangles, 0, sizeof render_stats - sizeof render_stats.shapes_tessellated);
}

static void add_item(DrawList *dl, const DrawItem *it) {
    if (dl->n == dl->cap) {
        dl->cap = dl->cap ? dl->cap * 2 : 1024;
        dl->items = realloc(dl->items, sizeof(DrawItem) * dl->cap);
    }
    dl->items[dl->n++] = *it;
    dl->cmd[dl->ncmd - 1].count++;
    render_stats.draws++;
    render_stats.triangles += it->mesh->nverts / 3;
}

static uint32_t *reserve_cv(DrawList *dl, int n) {
    if (dl->ncv + n > dl->cvcap) {
        while (dl->cvcap < dl->ncv + n) dl->cvcap = dl->cvcap ? dl->cvcap * 2 : 4096;
        dl->cv = realloc(dl->cv, sizeof(uint32_t) * dl->cvcap);
    }
    return dl->cv + dl->ncv;
}

static inline uint8_t clamp8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

static int is_identity(const CXform *cx) {
    return cx->mul[0] == 1 && cx->mul[1] == 1 && cx->mul[2] == 1 && cx->mul[3] == 1 &&
           !cx->add[0] && !cx->add[1] && !cx->add[2] && !cx->add[3];
}

// True if the transformed bounding box misses the viewport.
static int offscreen(const float bb[4], Mat m, const float view[4]) {
    float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    for (int k = 0; k < 4; k++) {
        float x = bb[k & 1 ? 2 : 0], y = bb[k & 2 ? 3 : 1];
        float sx = m.a * x + m.c * y + m.tx, sy = m.b * x + m.d * y + m.ty;
        if (sx < minx) minx = sx;
        if (sx > maxx) maxx = sx;
        if (sy < miny) miny = sy;
        if (sy > maxy) maxy = sy;
    }
    return maxx < view[0] || maxy < view[1] || minx > view[2] || miny > view[3];
}

// The mesh's vertex colours with cx applied (positions are untouched: they stay where
// they were uploaded and only this colour array changes).
static void recolour(const Mesh *mesh, const CXform *cx, uint32_t *out) {
    uint32_t last_in = 0xffffffffu, last_out = 0;
    for (int i = 0; i < mesh->nverts; i++) {
        uint32_t in;
        memcpy(&in, mesh->rgba + i * 4, 4);
        if (in != last_in) {   // most shapes are a handful of flat colours
            const uint8_t *p = (const uint8_t *)&in;
            uint8_t c[4] = {clamp8(p[0] * cx->mul[0] + cx->add[0]), clamp8(p[1] * cx->mul[1] + cx->add[1]),
                            clamp8(p[2] * cx->mul[2] + cx->add[2]), clamp8(p[3] * cx->mul[3] + cx->add[3])};
            last_out = pack(c);
            last_in = in;
        }
        out[i] = last_out;
    }
}

// ---- recolour cache ----

struct RecolourEntry {
    const Mesh *mesh;
    CXform cx;
    uint32_t *v;
};

void recolour_cache_init(RecolourCache *rc, void *mem, size_t bytes) {
    rc->mem = mem;
    rc->cap = mem ? (int)(bytes / sizeof(uint32_t)) : 0;
    rc->used = 0;
    rc->table_size = 8192;
    rc->table = calloc(rc->table_size, sizeof(struct RecolourEntry));
    rc->entries = 0;
}

void recolour_cache_free(RecolourCache *rc) {
    free(rc->table);
    rc->table = NULL;
    rc->cap = rc->used = rc->entries = 0;
}

static uint32_t cache_hash(const Mesh *mesh, const CXform *cx) {
    uint32_t h = (uint32_t)(uintptr_t)mesh * 2654435761u;
    const uint32_t *w = (const uint32_t *)cx;
    for (size_t i = 0; i < sizeof(CXform) / 4; i++) h = (h ^ w[i]) * 16777619u;
    return h;
}

// The cached recolour of mesh under cx, made on first use; NULL if the cache is full.
static const uint32_t *cache_get(RecolourCache *rc, const Mesh *mesh, const CXform *cx) {
    if (!rc || !rc->table) return NULL;
    uint32_t i = cache_hash(mesh, cx) & (rc->table_size - 1);
    for (;;) {
        struct RecolourEntry *e = &rc->table[i];
        if (!e->mesh) break;
        if (e->mesh == mesh && !memcmp(&e->cx, cx, sizeof(CXform))) return e->v;
        i = (i + 1) & (rc->table_size - 1);
    }
    // keep the table under 3/4 full so probing stays short
    if (rc->used + mesh->nverts > rc->cap || rc->entries * 4 >= rc->table_size * 3) return NULL;
    struct RecolourEntry *e = &rc->table[i];
    e->mesh = mesh;
    e->cx = *cx;
    e->v = rc->mem + rc->used;
    rc->used += mesh->nverts;
    rc->entries++;
    recolour(mesh, cx, e->v);
    render_stats.cx_tris += mesh->nverts / 3;
    return e->v;
}

void render_bitmap_fill(const BitmapFill *bf, Mat m, CXform cx, DrawList *out) {
    if (!bf->mesh.nverts) return;
    if (out->view[2] > out->view[0] && offscreen(bf->mesh.bounds, m, out->view)) return;
    DrawItem it = {&bf->mesh, m, NULL, -1, bf, {0, 0, 0, 0}};
    if (!out->masking) {   // the GPU tint multiplies; additive colour is folded in roughly
        for (int k = 0; k < 4; k++) it.tint[k] = cx.mul[k] + (k == 3 ? 0 : cx.add[k] / 255.0f);
        for (int k = 0; k < 4; k++) it.tint[k] = it.tint[k] < 0 ? 0 : it.tint[k] > 1 ? 1 : it.tint[k];
    }
    add_item(out, &it);
}

static void emit_bitmaps(Shape *s, Mat m, CXform cx, DrawList *out) {
    for (int i = 0; i < s->nbfills; i++) render_bitmap_fill(&s->bfills[i], m, cx, out);
}

static void emit_shape(Shape *s, Mat m, CXform cx, DrawList *out) {
    tessellate_shape(s);
    const Mesh *mesh = &s->mesh;
    int n = mesh->nverts;
    if (!n && !s->nbfills) return;
    if (out->view[2] > out->view[0] && offscreen(mesh->bounds, m, out->view)) {
        render_stats.culled++;
        return;
    }
    if (!n) { emit_bitmaps(s, m, cx, out); return; }
    // positions: the uploaded copy (or mesh->xy); colours: the mesh's own, or recoloured
    DrawItem it = {mesh, m, mesh->gpu, -1, NULL, {0, 0, 0, 0}};
    int recoloured = out->masking || !is_identity(&cx);
    // masks: all-zero colour transform, alpha 0, so only the stencil changes
    if (out->masking) memset(&cx, 0, sizeof cx);
    if (recoloured) {
        it.col = cache_get(out->cache, mesh, &cx);
        if (it.col) {
            render_stats.cached_tris += n / 3;
        } else {
            uint32_t *c = reserve_cv(out, n);
            recolour(mesh, &cx, c);
            it.cv = out->ncv;
            out->ncv += n;
            render_stats.cx_tris += n / 3;
        }
    }
    add_item(out, &it);
    if (s->nbfills) emit_bitmaps(s, m, cx, out);
}

void render_shape(Shape *s, Mat m, CXform cx, DrawList *out) { emit_shape(s, m, cx, out); }

static void emit_char(Movie *mv, int cid, Mat m, CXform cx, int tick, DrawList *out);

// Draws places in order until one is deeper than max_depth. A clip place masks
// the places after it up to its clip depth.
static void render_range(Movie *mv, const Frame *f, int *i, int max_depth, Mat m, CXform cx, int tick,
                         DrawList *out) {
    while (*i < f->count && f->places[*i].depth <= max_depth) {
        const Place *p = &f->places[(*i)++];
        if (p->clip) {
            // Flash ignores masks inside mask shapes; 255 is the stencil limit.
            if (out->masking || out->level >= 255) continue;
            int lvl = out->level;
            set_cmd(out, lvl, STENCIL_INCR);
            int ms = out->n;
            out->masking = 1;
            emit_char(mv, p->cid, mat_mul(m, p->m), cx, tick, out);
            out->masking = 0;
            int mn = out->n - ms;

            out->level = lvl + 1;
            set_cmd(out, lvl + 1, STENCIL_KEEP);
            render_range(mv, f, i, p->clip < max_depth ? p->clip : max_depth, m, cx, tick, out);
            out->level = lvl;

            // Undo with the same triangles so the stencil is back to lvl.
            if (mn) {
                set_cmd(out, lvl + 1, STENCIL_DECR);
                for (int k = 0; k < mn; k++) {
                    DrawItem it = out->items[ms + k];   // copy: add_item may realloc
                    add_item(out, &it);
                }
            }
            set_cmd(out, lvl, STENCIL_KEEP);
            render_stats.masks++;
            continue;
        }
        if (out->masking || p->cx.mul[3] > 0 || p->cx.add[3] > 0)
            emit_char(mv, p->cid, mat_mul(m, p->m), cx_mul(cx, p->cx), tick, out);
    }
}

void render_display_list(Movie *mv, const Frame *f, Mat m, CXform cx, int tick, DrawList *out) {
    int i = 0;
    render_range(mv, f, &i, 0xffff, m, cx, tick, out);
}

void render_char(Movie *mv, int cid, Mat m, CXform cx, int tick, DrawList *out) {
    emit_char(mv, cid, m, cx, tick, out);
}

const float *render_shape_bounds(Shape *s) {
    tessellate_shape(s);
    return s->mesh.bounds;
}

void render_mask_push(DrawList *out) {
    if (out->nmask >= 16 || out->masking || out->level >= 255) {   // too deep, or inside a mask: unmasked
        if (out->nmask < 16) out->mask_start[out->nmask] = -1;
        out->nmask++;
        return;
    }
    set_cmd(out, out->level, STENCIL_INCR);
    out->mask_start[out->nmask] = out->n;
    out->masking = 1;
    out->nmask++;
}

void render_mask_apply(DrawList *out) {
    int i = out->nmask - 1;
    if (i < 0 || i >= 16 || out->mask_start[i] < 0 || !out->masking) return;
    out->masking = 0;
    out->mask_count[i] = out->n - out->mask_start[i];
    out->level++;
    set_cmd(out, out->level, STENCIL_KEEP);
}

void render_mask_pop(DrawList *out) {
    int i = --out->nmask;
    if (i < 0 || i >= 16 || out->mask_start[i] < 0) { if (out->nmask < 0) out->nmask = 0; return; }
    int start = out->mask_start[i], n = out->mask_count[i];
    if (n) {
        set_cmd(out, out->level, STENCIL_DECR);
        for (int k = 0; k < n; k++) {
            DrawItem it = out->items[start + k];   // copy: add_item may realloc
            add_item(out, &it);
        }
    }
    out->level--;
    set_cmd(out, out->level, STENCIL_KEEP);
    render_stats.masks++;
}

static void emit_char(Movie *mv, int cid, Mat m, CXform cx, int tick, DrawList *out) {
    Character *c = &mv->chars[cid];
    if (c->type == CHAR_SHAPE) {
        emit_shape(c->ptr, m, cx, out);
    } else if (c->type == CHAR_SPRITE) {
        Sprite *s = c->ptr;
        if (!s->nframes) return;
        int f = sprite_frame_at(s, tick);
        if (s->first_stop == 0)
            for (int i = 0; i < out->nparts; i++)
                if (out->part_count[i] == s->nframes) f = out->part_frame[i];
        render_display_list(mv, &s->frames[f], m, cx, tick, out);
    }
}

void render_item_uv(const DrawItem *it, int i, float uv[2]) {
    const Mesh *m = it->mesh;
    if (m->uv) { uv[0] = m->uv[i * 2]; uv[1] = m->uv[i * 2 + 1]; }
    else { const TexVertex *t = m->gpu; uv[0] = t[i].u; uv[1] = t[i].v; }
}

Vertex render_item_vertex(const DrawList *dl, const DrawItem *it, int i) {
    Vertex v;
    if (it->bf) {
        const Mesh *m = it->mesh;
        if (m->xy) { v.x = m->xy[i * 2]; v.y = m->xy[i * 2 + 1]; }
        else { const TexVertex *t = m->gpu; v.x = t[i].x; v.y = t[i].y; }
        v.z = 0.5f;
        uint8_t c[4];
        for (int k = 0; k < 4; k++) c[k] = (uint8_t)(it->tint[k] * 255);
        v.color = pack(c);
    } else {
        const Mesh *m = it->mesh;
        if (it->vp) v = it->vp[i];
        else { v.x = m->xy[i * 2]; v.y = m->xy[i * 2 + 1]; v.z = 0.5f; memcpy(&v.color, m->rgba + i * 4, 4); }
        if (it->col) v.color = it->col[i];
        else if (it->cv >= 0) v.color = dl->cv[it->cv + i];
    }
    float x = v.x, y = v.y;
    v.x = it->m.a * x + it->m.c * y + it->m.tx;
    v.y = it->m.b * x + it->m.d * y + it->m.ty;
    return v;
}
