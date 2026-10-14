#include "hostfont.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 ? malloc(n) : NULL;
    if (d && fread(d, 1, n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f);
    *len = d ? (size_t)n : 0;
    return d;
}

static int rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static unsigned ru16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t ru32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

typedef struct { unsigned x, y, w, h; } Rect;

HostFont *hostfont_load(const char *fnt_path, const char *dds_path, char *err, int errlen) {
    size_t flen, dlen;
    uint8_t *fnt = read_file(fnt_path, &flen);
    if (!fnt || flen < 4 || memcmp(fnt, "BMF\3", 4)) {
        snprintf(err, errlen, "can't read the font %s", fnt_path);
        free(fnt);
        return NULL;
    }
    HostFont *hf = calloc(1, sizeof(HostFont));
    Rect src[256] = {{0}};
    // blocks: type, size, body (1 info, 2 common, 3 pages, 4 chars, 5 kerning)
    for (size_t p = 4; p + 5 <= flen;) {
        int type = fnt[p];
        uint32_t size = ru32(fnt + p + 1);
        const uint8_t *b = fnt + p + 5;
        if (p + 5 + size > flen) break;
        if (type == 1 && size >= 2) {
            int s = rd16(b);
            hf->size = s < 0 ? -s : s;
        } else if (type == 2 && size >= 4) {
            hf->line_height = ru16(b);
            hf->base = ru16(b + 2);
        } else if (type == 4) {
            for (uint32_t i = 0; i + 20 <= size; i += 20) {
                const uint8_t *c = b + i;
                uint32_t id = ru32(c);
                if (id >= 256 || c[18] != 0) continue;   // Latin-1, first page
                HostGlyph *g = &hf->glyphs[id];
                src[id] = (Rect){ru16(c + 4), ru16(c + 6), ru16(c + 8), ru16(c + 10)};
                g->xoff = rd16(c + 12);
                g->yoff = rd16(c + 14);
                g->advance = rd16(c + 16);
                g->present = 1;
            }
        }
        p += 5 + size;
    }
    free(fnt);

    uint8_t *dds = read_file(dds_path, &dlen);
    if (!dds || dlen < 128 || memcmp(dds, "DDS ", 4) || ru32(dds + 88) != 32) {
        snprintf(err, errlen, "can't read the font page %s (32-bit DDS expected)", dds_path);
        free(dds);
        free(hf);
        return NULL;
    }
    unsigned dh = ru32(dds + 12), dw = ru32(dds + 16);
    // channel masks tell where R, G, B, A sit in each 32-bit pixel
    uint32_t mask[4] = {ru32(dds + 92), ru32(dds + 96), ru32(dds + 100), ru32(dds + 104)};
    int shift[4];
    for (int k = 0; k < 4; k++) {
        shift[k] = 0;
        while (mask[k] && !(mask[k] >> shift[k] & 1)) shift[k]++;
    }
    const uint8_t *px = dds + 128;
    if ((size_t)dw * dh * 4 + 128 > dlen) { snprintf(err, errlen, "font page too short"); free(dds); free(hf); return NULL; }

    // shelf-pack the glyphs into a 512-wide atlas with a pixel of space around each
    const unsigned AW = 512;
    unsigned x = 1, y = 1, row_h = 0;
    Rect dst[256];
    for (int c = 0; c < 256; c++) {
        if (!hf->glyphs[c].present || !src[c].w || !src[c].h) continue;
        if (x + src[c].w + 1 > AW) { x = 1; y += row_h + 1; row_h = 0; }
        dst[c] = (Rect){x, y, src[c].w, src[c].h};
        x += src[c].w + 1;
        if (src[c].h > row_h) row_h = src[c].h;
    }
    unsigned AH = 1;
    while (AH < y + row_h + 1) AH *= 2;
    Bitmap *a = &hf->atlas;
    a->w = AW;
    a->h = AH;
    a->rgba = calloc((size_t)AW * AH, 4);
    for (int c = 0; c < 256; c++) {
        HostGlyph *g = &hf->glyphs[c];
        if (!g->present || !src[c].w || !src[c].h) continue;
        Rect s = src[c], d = dst[c];
        for (unsigned j = 0; j < s.h && s.y + j < dh; j++)
            for (unsigned i = 0; i < s.w && s.x + i < dw; i++) {
                uint32_t v = ru32(px + ((size_t)(s.y + j) * dw + s.x + i) * 4);
                uint8_t *o = a->rgba + ((size_t)(d.y + j) * AW + d.x + i) * 4;
                for (int k = 0; k < 4; k++) o[k] = mask[k] ? (v & mask[k]) >> shift[k] : 255;
            }
        // the glyph quad, in font pixels from the top-left of the line
        float x0 = g->xoff, y0 = g->yoff, x1 = x0 + s.w, y1 = y0 + s.h;
        float u0 = (float)d.x / AW, v0 = (float)d.y / AH, u1 = (float)(d.x + s.w) / AW, v1 = (float)(d.y + s.h) / AH;
        float xy[12] = {x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1};
        float uv[12] = {u0, v0, u1, v0, u1, v1, u0, v0, u1, v1, u0, v1};
        Mesh *m = &g->box.mesh;
        m->nverts = 6;
        m->xy = malloc(sizeof xy);
        m->uv = malloc(sizeof uv);
        memcpy(m->xy, xy, sizeof xy);
        memcpy(m->uv, uv, sizeof uv);
        m->bounds[0] = x0; m->bounds[1] = y0; m->bounds[2] = x1; m->bounds[3] = y1;
        g->box.bitmap = a;
        g->box.clip = 1;
        g->box.smooth = 1;
    }
    free(dds);
    if (!hf->size) hf->size = hf->line_height ? hf->line_height : 20;
    return hf;
}
