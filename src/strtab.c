// String table: see strtab.h.
#include "strtab.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *key;
    char *val;
} Entry;

typedef struct {
    Entry *e;
    int cap;     // power of two
    int n;
} Table;

static Table g_strings;   // loaded strings

static uint32_t hash_str(const char *s) {
    uint32_t h = 2166136261u;   // FNV-1a
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static Entry *table_find(Table *t, const char *key) {
    if (!t->cap) return NULL;
    uint32_t i = hash_str(key) & (t->cap - 1);
    while (t->e[i].key) {
        if (!strcmp(t->e[i].key, key)) return &t->e[i];
        i = (i + 1) & (t->cap - 1);
    }
    return NULL;
}

static char *dup_str(const char *s, int len) {
    char *d = malloc(len + 1);
    if (!d) return NULL;
    memcpy(d, s, len);
    d[len] = 0;
    return d;
}

// Takes ownership of key and val.
static void table_put(Table *t, char *key, char *val) {
    if ((t->n + 1) * 4 >= t->cap * 3) {
        int ncap = t->cap ? t->cap * 2 : 256;
        Entry *ne = calloc(ncap, sizeof *ne);
        if (!ne) { free(key); free(val); return; }
        for (int i = 0; i < t->cap; i++) {
            if (!t->e[i].key) continue;
            uint32_t j = hash_str(t->e[i].key) & (ncap - 1);
            while (ne[j].key) j = (j + 1) & (ncap - 1);
            ne[j] = t->e[i];
        }
        free(t->e);
        t->e = ne;
        t->cap = ncap;
    }
    uint32_t i = hash_str(key) & (t->cap - 1);
    while (t->e[i].key) {
        if (!strcmp(t->e[i].key, key)) {   // override
            free(key);
            free(t->e[i].val);
            t->e[i].val = val;
            return;
        }
        i = (i + 1) & (t->cap - 1);
    }
    t->e[i].key = key;
    t->e[i].val = val;
    t->n++;
}

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// Unescape value text [s, s+len) into a new string.
static char *unescape(const char *s, int len) {
    char *d = malloc(len + 1), *o = d;
    if (!d) return NULL;
    for (int i = 0; i < len; i++) {
        if (s[i] == '\\' && i + 1 < len) {
            char c = s[++i];
            *o++ = c == 'n' ? '\n' : c == 't' ? '\t' : c == 's' ? ' ' : c;
        } else {
            *o++ = s[i];
        }
    }
    *o = 0;
    return d;
}

static int load_mem(const char *text, int len) {
    int count = 0;
    const char *p = text, *end = text + len;
    if (len >= 3 && !memcmp(p, "\xEF\xBB\xBF", 3)) p += 3;
    while (p < end) {
        const char *eol = memchr(p, '\n', end - p);
        if (!eol) eol = end;
        const char *a = p, *b = eol;
        p = eol + 1;
        while (a < b && is_space(*a)) a++;
        if (a == b || *a == '#') continue;
        const char *eq = memchr(a, '=', b - a);
        if (!eq) continue;
        const char *ke = eq;
        while (ke > a && is_space(ke[-1])) ke--;
        if (ke == a) continue;
        const char *v = eq + 1;
        while (v < b && is_space(*v)) v++;
        while (b > v && is_space(b[-1])) b--;
        char *key = dup_str(a, (int)(ke - a));
        char *val = unescape(v, (int)(b - v));
        if (!key || !val) { free(key); free(val); continue; }
        table_put(&g_strings, key, val);
        count++;
    }
    return count;
}

int strtab_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len > 0 ? len : 1);
    if (!buf) { fclose(f); return -1; }
    long got = (long)fread(buf, 1, len, f);
    fclose(f);
    int n = load_mem(buf, (int)got);
    free(buf);
    return n;
}

const char *strtab_get(const char *key) {
    if (!key || !*key) return NULL;
    Entry *e = table_find(&g_strings, key);
    return e ? e->val : NULL;
}

const char *strtab_number(int number) {
    char key[16];
    snprintf(key, sizeof key, "%d", number);
    return strtab_get(key);
}
