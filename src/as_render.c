// Drawing and measuring the ActionScript runtime's live clip tree.
#include "as_render.h"
#include "hostfont.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static CXform cx_with_alpha(CXform cx, float alpha) {
    cx.mul[3] *= alpha / 100.0f;
    cx.add[3] *= alpha / 100.0f;
    return cx;
}

// ---- text fields --------------------------------------------------------------------

static AsVm *g_vm;
static AsStr *A_text;

void as_render_init(AsVm *vm) {
    g_vm = vm;
    A_text = as_atom(vm, "text");
}

// What a field shows: its variable (a path from the clip it's on) or its "text".
static const char *field_text(AsMc *tf) {
    if (!g_vm) return tf->text->text;
    if (tf->text->var && tf->parent) {
        int found;
        AsVal v = as_resolve_path(g_vm, tf->parent, tf->text->var, &found);
        if (found && v.t != V_UNDEF) return as_tostr(g_vm, v)->s;
    }
    AsProp *p = as_own(&tf->obj, A_text);
    return p ? as_tostr(g_vm, p->v)->s : tf->text->text;
}

// Plain text from a field's HTML: tags dropped, <br> and </p> as line breaks, entities decoded.
static void strip_html(const char *s, char *out, int cap) {
    int n = 0;
    while (*s && n < cap - 1) {
        if (*s == '<') {
            const char *e = strchr(s, '>');
            if (!e) break;
            if (!strncasecmp(s, "<br", 3) || (!strncasecmp(s, "</p", 3) && e[1])) out[n++] = '\n';
            s = e + 1;
        } else if (*s == '&') {
            static const struct { const char *name; char c; } ents[] = {
                {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&nbsp;", ' '}};
            int k;
            for (k = 0; k < 6; k++)
                if (!strncmp(s, ents[k].name, strlen(ents[k].name))) break;
            if (k < 6) { out[n++] = ents[k].c; s += strlen(ents[k].name); }
            else out[n++] = *s++;
        } else {
            out[n++] = *s++;
        }
    }
    out[n] = 0;
}

// Next UTF-8 code point (SWF6+ text is UTF-8).
static int utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    int c = *s++;
    if (c >= 0xC0) {
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        c &= 0x3F >> extra;
        while (extra-- && (*s & 0xC0) == 0x80) c = (c << 6) | (*s++ & 0x3F);
    }
    *p = (const char *)s;
    return c;
}

static float glyph_advance(const Font *f, int g, float scale) {
    if (g < 0) return f->em * 0.3f * scale;
    if (f->has_layout) return f->advance[g] * scale;
    const float *b = f->glyphs[g] ? f->glyphs[g]->mesh.bounds : NULL;
    return b && b[2] > b[0] ? (b[2] + f->em * 0.05f) * scale : f->em * 0.5f * scale;
}

// The font a field is drawn with: the game's own bitmap font when loaded (the SWF
// fonts are placeholders), else the field's embedded outline font.
typedef struct {
    const HostFont *hf;
    const Font *f;
    float k;                   // twips per font unit (host: per font pixel)
} TextFont;

static const HostFont *g_host_font;

void as_render_set_font(const HostFont *hf) { g_host_font = hf; }

static float tf_advance(const TextFont *t, int code) {
    if (t->hf) {
        const HostGlyph *g = code < 256 ? &t->hf->glyphs[code] : NULL;
        return (g && g->present ? g->advance : t->hf->size * 0.3f) * t->k;
    }
    return glyph_advance(t->f, font_glyph(t->f, code), t->k);
}

static float line_width(const TextFont *t, const char *s, const char *end) {
    float w = 0;
    while (s < end) w += tf_advance(t, utf8_next(&s));
    return w;
}

// top: the top of the line (host font) and base: its baseline (outline fonts)
static void draw_line(const TextFont *t, const char *s, const char *end, float x, float top, float base, Mat m,
                      CXform cx, DrawList *out) {
    while (s < end) {
        int code = utf8_next(&s);
        if (t->hf) {
            const HostGlyph *g = code < 256 ? &t->hf->glyphs[code] : NULL;
            if (g && g->present && g->box.mesh.nverts) {
                Mat gm = {t->k, 0, 0, t->k, x, top};
                render_bitmap_fill(&g->box, mat_mul(m, gm), cx, out);
            }
        } else {
            int g = font_glyph(t->f, code);
            if (g >= 0 && t->f->glyphs[g]) {
                Mat gm = {t->k, 0, 0, t->k, x, base};
                render_shape(t->f->glyphs[g], mat_mul(m, gm), cx, out);
            }
        }
        x += tf_advance(t, code);
    }
}

// A line of the executable's own menu text, as castle.exe draws it: (x, y) in stage pixels
// through m, y the middle of the lines; align 0 ends at x, 1 starts there, 2 centres;
// sx/sy scale the font pixels. cx colours the white glyphs. The executable's sizes are for
// the 20 pixel font (synjUnicode_20); newer copies of the game ship it drawn at 60, so its
// pixels are scaled to 20.
static float host_unit(void) { return g_host_font->size > 0 ? 20.0f / g_host_font->size : 1; }

void as_render_host_text(const char *s, float x, float y, float sx, float sy, int align, Mat m, CXform cx,
                         DrawList *out) {
    if (!g_host_font || !s || !*s) return;
    sx *= host_unit();
    sy *= host_unit();
    TextFont t = {g_host_font, NULL, 1};
    int lines = 1;
    for (const char *p = s; *p; p++) lines += *p == '\n';
    float top = y + roundf((g_host_font->line_height + 1) * lines * -0.5f) * sy;
    while (*s) {
        const char *e = s;
        while (*e && *e != '\n') e++;
        float w = line_width(&t, s, e) * sx;
        float lx = align == 0 ? x - w : align == 2 ? x - roundf(w / sx * 0.5f) * sx : x;
        Mat lm = mat_mul(m, (Mat){sx, 0, 0, sy, lx, top});
        draw_line(&t, s, e, 0, 0, 0, lm, cx, out);
        top += g_host_font->line_height * sy;
        s = *e ? e + 1 : e;
    }
}

float as_render_host_text_width(const char *s) {
    if (!g_host_font || !s) return 0;
    TextFont t = {g_host_font, NULL, 1};
    return line_width(&t, s, s + strlen(s)) * host_unit();
}

float as_render_host_line_height(void) { return g_host_font ? g_host_font->line_height * host_unit() : 0; }

static void draw_text(AsMc *tf, Mat m, CXform cx, DrawList *out) {
    const EditText *et = tf->text;
    const Character *fc = &tf->movie->chars[et->font];
    if (fc->type != CHAR_FONT && !g_host_font) return;
    const Font *f = fc->type == CHAR_FONT ? fc->ptr : NULL;
    char buf[2048];
    if (et->html) strip_html(field_text(tf), buf, sizeof buf);
    else snprintf(buf, sizeof buf, "%s", field_text(tf));
    if (AS_ENV("AS_TEXTDBG"))
        printf("  [text] %s.%s '%s' font %d '%s', height %d, var %s, at %.0f,%.0f scale %.2f colour %d,%d,%d,%d alpha x%.2f\n",
               tf->parent && tf->parent->name ? tf->parent->name->s : "?", tf->name ? tf->name->s : "(unnamed)", buf,
               et->font, f ? f->name : "-", et->height, et->var ? et->var : "-", m.tx, m.ty, m.a * 20,
               et->color[0], et->color[1], et->color[2], et->color[3], cx.mul[3]);
    // "fixme": the executable fills these in from its string table (by the field's ntext number)
    if (!buf[0] || !strcmp(buf, "fixme")) return;

    TextFont t = {g_host_font, f, 0};
    float pad = 40;   // Flash's 2 pixel gutter
    float x0 = et->bounds[0] + pad + et->left_margin, x1 = et->bounds[1] - pad - et->right_margin;
    float top = et->bounds[2] + pad, base, line_h;
    CXform tcx;
    if (t.hf) {
        t.k = (float)et->height / t.hf->size;
        line_h = t.hf->line_height * t.k + et->leading;
        base = top + t.hf->base * t.k;
        // white glyphs times the field colour
        tcx = (CXform){{et->color[0] / 255.0f, et->color[1] / 255.0f, et->color[2] / 255.0f, et->color[3] / 255.0f},
                       {0, 0, 0, 0}};
    } else {
        t.k = (float)et->height / f->em;
        float ascent = (f->has_layout ? f->ascent : f->em * 0.8f) * t.k;
        float descent = (f->has_layout ? f->descent : f->em * 0.2f) * t.k;
        line_h = ascent + descent + et->leading;
        base = top + ascent;
        tcx = (CXform){{0, 0, 0, et->color[3] / 255.0f}, {et->color[0], et->color[1], et->color[2], 0}};
    }
    cx = cx_mul(cx, tcx);

    // lines: hard breaks, plus word wrap inside the field's width
    const char *p = buf;
    while (*p) {
        const char *ls = p, *le = p, *brk = NULL;
        float w = 0;
        while (*le && *le != '\n' && *le != '\r') {
            const char *q = le;
            float aw = tf_advance(&t, utf8_next(&q));
            if (et->wordwrap && w + aw > x1 - x0 && brk) { le = brk; break; }
            if (*le == ' ') brk = le;
            w += aw;
            le = q;
        }
        float lw = line_width(&t, ls, le);
        float x = et->align == 1 ? x1 - lw : et->align == 2 ? (x0 + x1 - lw) / 2 : x0;
        draw_line(&t, ls, le, x, top, base, m, cx, out);
        top += line_h;
        base += line_h;
        p = le;
        if (*p == '\r' && p[1] == '\n') p += 2;
        else if (*p == '\n' || *p == '\r' || *p == ' ') p++;
    }
}

void as_render_mc(AsMc *mc, Mat parent, CXform pcx, DrawList *out) {
    if (mc->removed || !mc->visible) return;
    Mat m = mat_mul(parent, mc->m);
    CXform cx = cx_mul(pcx, cx_with_alpha(mc->cx, mc->alpha));
    if (cx.mul[3] <= 0 && cx.add[3] <= 0 && !out->masking) return;
    int heat_here = 0;   // one heat haze per frame: this clip's own items shimmer
    if (mc->shader == 1 && out->heat_at < 0 && !out->masking && !out->level) {
        float b[4];
        render_mc_bounds(mc, m, b);
        if (b[2] > b[0]) {
            out->heat_at = out->n;
            memcpy(out->heat, b, sizeof b);
            heat_here = 1;
        }
    }
    // a mask entry covers the entries after it up to its clip depth
    int open = 0, mask_end[16];
    for (int i = 0; i < mc->ndisp; i++) {
        const AsDisp *e = &mc->disp[i];
        while (open && e->depth > mask_end[open - 1]) { render_mask_pop(out); open--; }
        int is_mask = e->clip != 0;
        if (is_mask) render_mask_push(out);
        if (e->kind == DISP_SHAPE) {
            render_char((Movie *)mc->movie, e->cid, mat_mul(m, e->m), cx_mul(cx, e->cx), 0, out);
        } else if (e->kind == DISP_MC && e->mc) {
            as_render_mc(e->mc, m, cx, out);
        } else if (e->kind == DISP_TEXT && e->mc && e->mc->text && e->mc->visible && !e->mc->removed) {
            AsMc *tf = e->mc;
            draw_text(tf, mat_mul(m, tf->m), cx_mul(cx, cx_with_alpha(tf->cx, tf->alpha)), out);
        }
        if (is_mask) {
            render_mask_apply(out);
            if (open < 16) mask_end[open++] = e->clip - 16384;   // timeline clip depth -> internal depth
            else render_mask_pop(out);
        }
    }
    while (open--) render_mask_pop(out);
    if (heat_here) out->heat_end = out->n;
}

static void grow(float b[4], const float s[4], Mat m) {
    if (s[2] < s[0]) return;
    for (int k = 0; k < 4; k++) {
        float x = s[k & 1 ? 2 : 0], y = s[k & 2 ? 3 : 1];
        float px = m.a * x + m.c * y + m.tx, py = m.b * x + m.d * y + m.ty;
        if (px < b[0]) b[0] = px;
        if (py < b[1]) b[1] = py;
        if (px > b[2]) b[2] = px;
        if (py > b[3]) b[3] = py;
    }
}

static void bounds_into(AsMc *mc, Mat m, float b[4]) {
    for (int i = 0; i < mc->ndisp; i++) {
        const AsDisp *e = &mc->disp[i];
        if (e->kind == DISP_SHAPE) {
            const Character *c = &mc->movie->chars[e->cid];
            if (c->type == CHAR_SHAPE) grow(b, render_shape_bounds(c->ptr), mat_mul(m, e->m));
        } else if (e->kind == DISP_MC && e->mc && !e->mc->removed) {
            bounds_into(e->mc, mat_mul(m, e->mc->m), b);
        }
    }
}

// Bounds (twips) of everything a clip draws, through matrix m.
void render_mc_bounds(AsMc *mc, Mat m, float out[4]) {
    out[0] = out[1] = 1e30f;
    out[2] = out[3] = -1e30f;
    bounds_into(mc, m, out);
}

// Bounds (twips) of a clip's own shapes only, not its child clips, through matrix m.
void render_mc_shape_bounds(AsMc *mc, Mat m, float out[4]) {
    out[0] = out[1] = 1e30f;
    out[2] = out[3] = -1e30f;
    for (int i = 0; i < mc->ndisp; i++) {
        const AsDisp *e = &mc->disp[i];
        const Character *c = &mc->movie->chars[e->cid];
        if (e->kind == DISP_SHAPE && c->type == CHAR_SHAPE) grow(out, render_shape_bounds(c->ptr), mat_mul(m, e->m));
    }
}
