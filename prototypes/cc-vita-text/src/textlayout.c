// Text layout with inline button glyphs: see textlayout.h.
#include "textlayout.h"

#include <stdlib.h>
#include <string.h>

#include "prompts.h"

uint32_t utf8_next(const char *str, int len, int *i) {
    const uint8_t *s = (const uint8_t *)str;
    uint8_t c = s[*i];
    int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (n == 0) { (*i)++; return c; }
    if (n < 0 || *i + n >= len) { (*i)++; return 0xFFFD; }
    uint32_t cp = c & (0x3F >> n);
    for (int k = 1; k <= n; k++) {
        if ((s[*i + k] & 0xC0) != 0x80) { (*i)++; return 0xFFFD; }
        cp = cp << 6 | (s[*i + k] & 0x3F);
    }
    *i += n + 1;
    return cp;
}

enum { AT_WORD, AT_SPACE, AT_GLYPH, AT_NEWLINE };

typedef struct {
    uint8_t kind, glyph;
    int start, len;
    float w;
    int line;
    float x;
} Atom;

typedef struct {
    Atom *a;
    int n, cap;
} Atoms;

static Atom *push(Atoms *v) {
    if (v->n == v->cap) {
        int nc = v->cap ? v->cap * 2 : 64;
        Atom *na = realloc(v->a, nc * sizeof *na);
        if (!na) return NULL;
        v->a = na;
        v->cap = nc;
    }
    Atom *at = &v->a[v->n++];
    memset(at, 0, sizeof *at);
    return at;
}

static float measure(const TextMetrics *m, const char *s, int start, int len) {
    float w = 0;
    int i = start, end = start + len;
    while (i < end) w += m->advance(m->ud, utf8_next(s, end, &i));
    return w;
}

// Append text bytes [start, start+len) as word atoms (extending the previous
// word when it's contiguous).
static void add_text(Atoms *v, const TextMetrics *m, const char *s, int start, int len) {
    if (len <= 0) return;
    Atom *prev = v->n ? &v->a[v->n - 1] : NULL;
    if (prev && prev->kind == AT_WORD && prev->start + prev->len == start) {
        prev->len += len;
        prev->w += measure(m, s, start, len);
        return;
    }
    Atom *a = push(v);
    if (!a) return;
    a->kind = AT_WORD;
    a->start = start;
    a->len = len;
    a->w = measure(m, s, start, len);
}

static void add_glyph(Atoms *v, const TextMetrics *m, int g, int start) {
    Atom *a = push(v);
    if (!a) return;
    a->kind = AT_GLYPH;
    a->glyph = (uint8_t)g;
    a->start = start;
    a->w = m->glyph_size * prompts_aspect((PromptGlyph)g) + 2 * m->glyph_pad;
}

static void tokenize(Atoms *v, const char *s, int n, const TextMetrics *m) {
    int i = 0;
    while (i < n) {
        char c = s[i];
        if (c == '\n') {
            Atom *a = push(v);
            if (a) { a->kind = AT_NEWLINE; a->start = i; }
            i++;
        } else if (c == ' ') {
            Atom *a = push(v);
            if (a) { a->kind = AT_SPACE; a->start = i; a->len = 1; a->w = m->advance(m->ud, ' '); }
            i++;
        } else if (c == '{' && i + 1 < n && s[i + 1] == '{') {
            add_text(v, m, s, i + 1, 1);   // literal "{"
            i += 2;
        } else if (c == '{') {
            const char *close = memchr(s + i + 1, '}', n - i - 1);
            int g = close ? prompts_lookup(s + i + 1, (int)(close - s - i - 1)) : -1;
            if (g >= 0) {
                add_glyph(v, m, g, i);
                i = (int)(close - s) + 1;
            } else {
                add_text(v, m, s, i, 1);   // not a token: show it as typed
                i++;
            }
        } else {
            int j = i;
            while (j < n && s[j] != ' ' && s[j] != '\n' && s[j] != '{') {
                int k = j;
                int g = prompts_lookup_codepoint(utf8_next(s, n, &k));
                if (g >= 0) {   // an icon character: becomes a glyph
                    add_text(v, m, s, i, j - i);
                    add_glyph(v, m, g, j);
                    i = k;
                }
                j = k;
            }
            add_text(v, m, s, i, j - i);
            i = j;
        }
    }
}

TextBlock text_layout(const char *s, const TextMetrics *m,
                      float wrap, int align, float box_width,
                      TextRun *runs, int max_runs) {
    TextBlock out = {0};
    int n = s ? (int)strlen(s) : 0;
    Atoms v = {0};
    tokenize(&v, s ? s : "", n, m);

    // Greedy wrap. Breaks only at spaces and newlines: a glyph stuck to a
    // word ("{cross}:") stays with it.
    int line = 0;
    float x = 0;
    int i = 0;
    while (i < v.n) {
        if (v.a[i].kind == AT_NEWLINE) {
            v.a[i].line = line++;
            x = 0;
            i++;
            continue;
        }
        int sp0 = i;   // spaces before the next group
        float sw = 0;
        while (i < v.n && v.a[i].kind == AT_SPACE) sw += v.a[i++].w;
        int g0 = i;
        float gw = 0;
        while (i < v.n && (v.a[i].kind == AT_WORD || v.a[i].kind == AT_GLYPH)) gw += v.a[i++].w;
        int wrapped = wrap > 0 && x > 0 && g0 < i && x + sw + gw > wrap;
        if (wrapped) {
            for (int k = sp0; k < g0; k++) { v.a[k].line = line; v.a[k].len = 0; v.a[k].w = 0; }
            line++;
            x = 0;
        }
        for (int k = wrapped ? g0 : sp0; k < i; k++) {
            v.a[k].line = line;
            v.a[k].x = x;
            x += v.a[k].w;
        }
    }
    int nlines = line + 1;

    // Line widths, ignoring trailing spaces.
    float *lw = calloc(nlines, sizeof *lw);
    float widest = 0;
    for (int k = 0; lw && k < v.n; k++) {
        Atom *a = &v.a[k];
        if (a->kind == AT_WORD || a->kind == AT_GLYPH) {
            if (a->x + a->w > lw[a->line]) lw[a->line] = a->x + a->w;
        }
    }
    for (int k = 0; lw && k < nlines; k++) if (lw[k] > widest) widest = lw[k];
    float box = box_width > 0 ? box_width : widest;

    // Emit runs, merging neighbouring text on the same line.
    float base0 = m->line_height * 0.8f;
    for (int k = 0; k < v.n; k++) {
        Atom *a = &v.a[k];
        if (a->kind == AT_NEWLINE || (a->kind != AT_GLYPH && a->len == 0)) continue;
        float off = 0;
        if (lw && align == ALIGN_CENTER) off = (box - lw[a->line]) * 0.5f;
        else if (lw && align == ALIGN_RIGHT) off = box - lw[a->line];
        if (a->kind != AT_GLYPH && out.nruns > 0) {
            TextRun *p = &runs[out.nruns - 1];
            if (p->kind == RUN_TEXT && p->line == a->line && p->start + p->len == a->start) {
                p->len += a->len;
                p->w += a->w;
                continue;
            }
        }
        if (out.nruns >= max_runs) { out.truncated = 1; break; }
        TextRun *r = &runs[out.nruns++];
        r->kind = a->kind == AT_GLYPH ? RUN_GLYPH : RUN_TEXT;
        r->glyph = a->glyph;
        r->line = (uint16_t)a->line;
        r->start = a->start;
        r->len = a->kind == AT_GLYPH ? 0 : a->len;
        r->x = a->x + off;
        r->y = base0 + a->line * m->line_height;
        r->w = a->w;
    }

    out.width = widest;
    out.height = nlines * m->line_height;
    out.lines = nlines;
    free(lw);
    free(v.a);
    return out;
}

void text_draw_runs(const char *s, const TextMetrics *m,
                    const TextRun *runs, int nruns, float ox, float oy,
                    TextDrawFn draw_text, GlyphDrawFn draw_glyph, void *ud) {
    for (int i = 0; i < nruns; i++) {
        const TextRun *r = &runs[i];
        if (r->kind == RUN_TEXT) {
            if (draw_text) draw_text(ud, s + r->start, r->len, ox + r->x, oy + r->y);
        } else if (draw_glyph) {
            draw_glyph(ud, r->glyph, ox + r->x + m->glyph_pad, oy + r->y - m->glyph_raise, m->glyph_size);
        }
    }
}
