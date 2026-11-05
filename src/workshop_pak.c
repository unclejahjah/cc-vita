// Steam Workshop item files: see workshop_pak.h.
#include "workshop_pak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// "PKTB", version 3, then the file's 16-character id; a table of 32-byte entries from 0x20, the
// first 16 bytes of each XORed with the id: name hash, offset, size, stored size (u32 LE).
#define PKTB_MAGIC "PKTB\3\0\0\0"
#define HASH_PAINTING 0xf4a94b84u
#define HASH_XML 0xfe238e42u

typedef struct { uint32_t hash, off, size, stored; } Entry;

int cc_workshop_is(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char h[8];
    int ok = fread(h, 1, 8, f) == 8 && !memcmp(h, PKTB_MAGIC, 8);
    fclose(f);
    return ok;
}

static int find_entry(FILE *f, uint32_t hash, Entry *out) {
    uint8_t head[32], e[32];
    if (fseek(f, 0, SEEK_SET) || fread(head, 1, 32, f) != 32) return 0;
    for (int i = 0; i < 64; i++) {
        if (fread(e, 1, 32, f) != 32) return 0;
        uint32_t w[4];
        for (int k = 0; k < 16; k++) e[k] ^= head[8 + k];
        memcpy(w, e, 16);
        if (!w[1]) return 0;
        if (w[0] == hash) {
            out->hash = w[0], out->off = w[1], out->size = w[2], out->stored = w[3];
            return 1;
        }
    }
    return 0;
}

// The entry's bytes as stored (still scrambled and compressed)
static uint8_t *read_entry(FILE *f, const Entry *e) {
    if (e->stored > 64u << 20) return NULL;
    uint8_t *b = malloc(e->stored ? e->stored : 1);
    if (!b) return NULL;
    if (fseek(f, (long)e->off, SEEK_SET) || fread(b, 1, e->stored, f) != e->stored) { free(b); return NULL; }
    return b;
}

// Unscrambles and decompresses an entry in place into a new buffer of e->size bytes: not known
// yet (the key's 4 name bytes are still being searched for), so no Workshop file loads.
static uint8_t *unpack(const Entry *e, uint8_t *stored) {
    (void)e;
    (void)stored;
    return NULL;
}

uint8_t *cc_workshop_read(const char *path, size_t *png_len, char *xml, size_t xmln) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    Entry pe, xe;
    uint8_t *png = NULL;
    xml[0] = 0;
    if (find_entry(f, HASH_XML, &xe)) {
        uint8_t *s = read_entry(f, &xe), *x = s ? unpack(&xe, s) : NULL;
        if (x) {
            size_t l = xe.size < xmln - 1 ? xe.size : xmln - 1;
            memcpy(xml, x, l);
            xml[l] = 0;
        }
        free(s);
        free(x);
    }
    if (find_entry(f, HASH_PAINTING, &pe)) {
        uint8_t *s = read_entry(f, &pe);
        png = s ? unpack(&pe, s) : NULL;
        free(s);
        if (png) *png_len = pe.size;
    }
    fclose(f);
    return png;
}
