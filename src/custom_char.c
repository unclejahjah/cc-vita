// Custom characters from the Painter Boss Paradise template: see custom_char.h.
#include "custom_char.h"
#include "paint_jr.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "../third_party/stb/stb_image.h"

#define TEMPLATE_SIZE 2048
#define TEXTURE_SIZE 1024   // the painting at half size: the template is drawn at twice the game's size

// The template's part boxes (x, y, w, h in template pixels), each the inside of a black frame.
// 1, 2: two more head angles the PC game's newer PLAYER.swf has and the original one doesn't.
static const int16_t BOX[][4] = {
    {163, 111, 234, 332}, {411, 111, 234, 332}, {659, 111, 234, 332}, {907, 111, 234, 332},   // heads
    {1155, 111, 234, 332}, {1403, 111, 234, 332}, {1651, 111, 234, 332},
    {163, 533, 234, 258}, {411, 533, 234, 258}, {659, 533, 234, 258}, {907, 533, 234, 258},   // bodies
    {1155, 533, 234, 258},
    {1541, 533, 96, 258},                                                                     // weapon
    {1691, 661, 90, 130}, {1795, 661, 90, 130},                                               // shield
    {163, 865, 292, 296}, {469, 865, 270, 296}, {753, 865, 292, 296}, {1059, 865, 292, 296},  // beefy
    {1365, 865, 292, 296}, {1671, 865, 270, 296},
    {163, 1179, 158, 152}, {335, 1179, 156, 152}, {505, 1179, 234, 152}, {753, 1179, 234, 152},   // beefy
    {1001, 1179, 234, 152}, {1249, 1179, 234, 152}, {1497, 1179, 160, 152}, {1671, 1179, 214, 152}, // arms
    {163, 1345, 158, 152}, {335, 1345, 156, 152}, {505, 1345, 234, 152}, {753, 1345, 140, 152},
    {163, 1515, 234, 152}, {411, 1515, 234, 152}, {659, 1515, 234, 152},
    {1201, 1477, 130, 130}, {1345, 1477, 258, 514},                                           // HUD icon, portrait
};

enum { PART_ALWAYS, PART_WEAPON };

// The part clips of PLAYER.swf: each calls one of MAIN's selectors on its first frame
// (_root.f_Helmet(this) ...), which shows the player's frame of it. A selector's clips are told
// apart by their order in the file (their ids differ by a few between the game's builds).
enum { G_HELMET, G_BELT, G_WEAPON, G_SHIELD, NGROUPS, G_NAMED = NGROUPS };
static const char *const SELECTOR[NGROUPS] = {"f_Helmet", "f_Belt", "f_Weapon", "f_Shield"};
#define MAX_PER_GROUP 32

// Which box each part clip shows, found by fitting the box's guide lines onto the part's own art
// (tools/custom/): the part's origin sits at ox, oy in the box, turned by rot
// degrees (mirrored first if flip) and scaled k template pixels per game pixel. All 31 frames
// of a part clip are the characters' versions of the same part.
typedef struct {
    uint8_t group, nth;        // the nth clip (in file order) that calls SELECTOR[group]
    uint8_t box, kind;
    float k, rot;
    uint8_t flip;
    float ox, oy;
} Part;

static const Part PARTS[] = {
    // heads (_root.f_Helmet): the newer heads are drawn a little bigger than the original ones
    {G_HELMET, 0, 0, PART_ALWAYS, 2.20f, 0, 0, 112.0f, 279.5f},    // three-quarter
    {G_HELMET, 1, 3, PART_ALWAYS, 2.25f, 0, 0, 119.5f, 237.5f},    // side
    {G_HELMET, 4, 4, PART_ALWAYS, 2.20f, 0, 0, 120.0f, 238.0f},    // front
    {G_HELMET, 3, 5, PART_ALWAYS, 2.30f, 0, 0, 124.5f, 263.0f},    // looking up
    {G_HELMET, 2, 6, PART_ALWAYS, 2.20f, 0, 0, 121.0f, 237.0f},    // back
    // bodies (f_Belt)
    {G_BELT, 0, 7, PART_ALWAYS, 2, 0, 0, 118.0f, 208.0f},
    {G_BELT, 2, 8, PART_ALWAYS, 2, 0, 0, 116.5f, 208.0f},
    {G_BELT, 1, 9, PART_ALWAYS, 2, 0, 0, 117.5f, 212.0f},
    {G_BELT, 3, 10, PART_ALWAYS, 2, 0, 0, 118.5f, 205.0f},
    {G_BELT, 4, 11, PART_ALWAYS, 2, 0, 0, 119.0f, 216.5f},
    // the character's own starting weapon (f_Weapon) and shield (f_Shield)
    {G_WEAPON, 0, 12, PART_WEAPON, 2, 0, 0, 50.0f, 206.0f},
    {G_SHIELD, 1, 13, PART_ALWAYS, 2, 0, 0, 68.5f, 88.0f},       // front
    {G_SHIELD, 0, 14, PART_ALWAYS, 2, 0, 0, 45.5f, 83.5f},       // side
    // beefy (the sandwich) bodies
    {G_BELT, 5, 15, PART_ALWAYS, 2, 0, 0, 142.5f, 242.5f},
    {G_BELT, 14, 16, PART_ALWAYS, 2, 0, 0, 114.5f, 241.0f},
    {G_BELT, 11, 17, PART_ALWAYS, 2, 0, 0, 147.0f, 202.0f},
    {G_BELT, 18, 18, PART_ALWAYS, 2, 0, 0, 157.5f, 201.5f},
    {G_BELT, 22, 19, PART_ALWAYS, 2, 0, 0, 145.5f, 245.5f},
    {G_BELT, 24, 20, PART_ALWAYS, 2, 0, 0, 147.0f, 222.5f},
    // beefy arms
    {G_BELT, 7, 21, PART_ALWAYS, 2.00f, 1, 0, 99, 35},
    {G_BELT, 8, 22, PART_ALWAYS, 2.00f, 4, 0, 71, 32},
    {G_BELT, 12, 23, PART_ALWAYS, 2.02f, 0, 0, 49, 61},
    {G_BELT, 15, 24, PART_ALWAYS, 1.95f, -1, 0, 91, 75},
    {G_BELT, 19, 25, PART_ALWAYS, 2.12f, 343, 1, 114, 76},
    {G_BELT, 17, 26, PART_ALWAYS, 1.97f, -3, 0, 174, 93},
    {G_BELT, 23, 27, PART_ALWAYS, 1.92f, -2, 0, 48, 63},
    {G_BELT, 25, 28, PART_ALWAYS, 1.95f, 269, 0, 168, 37},
    {G_BELT, 9, 29, PART_ALWAYS, 2.02f, 3, 0, 117, 111},
    {G_BELT, 10, 30, PART_ALWAYS, 2.03f, 358, 0, 75, 82},
    {G_BELT, 16, 31, PART_ALWAYS, 2.02f, 88, 0, 59, 54},
    {G_BELT, 6, 32, PART_ALWAYS, 2.12f, 0, 0, 70, 77},
    {G_BELT, 21, 33, PART_ALWAYS, 2.00f, -2, 0, 28, 63},
    {G_BELT, 20, 34, PART_ALWAYS, 1.98f, 1, 0, 40, 35},
    {G_BELT, 13, 35, PART_ALWAYS, 2.03f, 356, 0, 80, 33},
    // not PLAYER.swf (cc_custom_named): the HUD's face (MAIN.swf hudN.pic.pic, fitted to its art's
    // extent) and the character select portrait (LOBBY.swf portrait_mid/top/bot)
    {G_NAMED, CC_ICON, 36, PART_ALWAYS, 2.75f, 0, 0, 267.1f, -238.9f},
    {G_NAMED, CC_PORTRAIT, 37, PART_ALWAYS, 1.05f, 0, 0, 126.0f, 250.0f},
};
#define NPARTS ((int)(sizeof PARTS / sizeof PARTS[0]))

// p_type -> the weapon the character starts with (cc_native.c)
int cc_default_weapon(int p_type);

static struct {
    int p_type, base;
    char name[64];
    Bitmap bm;
    BitmapFill fill[NPARTS];
} g;

static int g_off;   // a Wi-Fi game: off, so every Vita runs the same 31 characters
void cc_custom_set_active(int on) { g_off = !on; }
int cc_custom_p_type(void) { return g_off ? 0 : g.p_type; }
int cc_custom_base(void) { return g.base; }
const char *cc_custom_name(void) { return g.name; }

// PLAYER.swf's part clips by selector and order, for the movie last asked about
static struct {
    const Movie *movie;
    int n[NGROUPS];
    uint16_t id[NGROUPS][MAX_PER_GROUP];
} idx;

static int calls(const Movie *m, const Sprite *s, const char *fn) {
    size_t l = strlen(fn) + 1;   // with its NUL: a whole name in the constant pool
    for (int i = 0; i < s->nactions && s->actions[i].frame == 0; i++) {
        const uint8_t *p = m->data + s->actions[i].off, *e = p + s->actions[i].len;
        for (; p + l <= e; p++)
            if (*p == (uint8_t)fn[0] && !memcmp(p, fn, l)) return 1;
    }
    return 0;
}

static void index_movie(const Movie *m) {
    memset(&idx, 0, sizeof idx);
    idx.movie = m;
    for (int id = 0; id < 65536; id++) {
        if (m->chars[id].type != CHAR_SPRITE) continue;
        const Sprite *s = m->chars[id].ptr;
        for (int g = 0; g < NGROUPS; g++)
            if (idx.n[g] < MAX_PER_GROUP && calls(m, s, SELECTOR[g])) idx.id[g][idx.n[g]++] = id;
    }
}

const BitmapFill *cc_custom_part(const Movie *m, const Sprite *s, int frame, int p_type) {
    if (!g.p_type || p_type != g.p_type) return NULL;
    if (idx.movie != m) index_movie(m);
    for (int i = 0; i < NPARTS; i++) {
        const Part *p = &PARTS[i];
        if (p->group == G_NAMED) continue;
        if (p->nth >= idx.n[p->group] || idx.id[p->group][p->nth] != s->id) continue;
        if (p->kind == PART_WEAPON && frame + 1 != cc_default_weapon(p_type)) return NULL;
        return &g.fill[i];
    }
    return NULL;
}

const BitmapFill *cc_custom_named(int which) {
    if (!g.p_type) return NULL;
    for (int i = 0; i < NPARTS; i++)
        if (PARTS[i].group == G_NAMED && PARTS[i].nth == which) return &g.fill[i];
    return NULL;
}

// The box as two triangles in the part's own coordinates (twips), textured from the painting.
static void make_fill(const Part *p, BitmapFill *bf) {
    const int16_t *b = BOX[p->box];
    float r = p->rot * (float)M_PI / 180, c = cosf(r), s = sinf(r), f = p->flip ? -1 : 1;
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    // 2 pixels in from the frame, so the filtered edge never picks up the frame's black
    float corner[4][2] = {{2, 2}, {b[2] - 2, 2}, {b[2] - 2, b[3] - 2}, {2, b[3] - 2}};
    bf->bitmap = &g.bm;
    bf->clip = 1;
    bf->smooth = 1;
    Mesh *m = &bf->mesh;
    m->nverts = 6;
    m->xy = malloc(sizeof(float) * 12);
    m->uv = malloc(sizeof(float) * 12);
    m->bounds[0] = m->bounds[1] = 1e9f;
    m->bounds[2] = m->bounds[3] = -1e9f;
    for (int i = 0; i < 6; i++) {
        float px = corner[order[i]][0], py = corner[order[i]][1];
        // box = origin + rotate(r) * mirror * k * local; local in game pixels, then twips
        float dx = (px - p->ox) / p->k, dy = (py - p->oy) / p->k;
        float lx = f * (c * dx + s * dy) * 20, ly = (-s * dx + c * dy) * 20;
        m->xy[i * 2] = lx;
        m->xy[i * 2 + 1] = ly;
        m->uv[i * 2] = (b[0] + px) / TEMPLATE_SIZE;
        m->uv[i * 2 + 1] = (b[1] + py) / TEMPLATE_SIZE;
        if (lx < m->bounds[0]) m->bounds[0] = lx;
        if (ly < m->bounds[1]) m->bounds[1] = ly;
        if (lx > m->bounds[2]) m->bounds[2] = lx;
        if (ly > m->bounds[3]) m->bounds[3] = ly;
    }
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// pbp.xml: <character displayname="..." base="N" .../>: an attribute's text, or 0 if it's not there
static int xml_attr(const char *xml, const char *name, char *out, size_t n) {
    char key[40];
    snprintf(key, sizeof key, " %s=\"", name);
    const char *p = strstr(xml, key);
    if (!p) return 0;
    p += strlen(key);
    const char *e = strchr(p, '"');
    if (!e || e == p) return 0;
    size_t l = (size_t)(e - p) < n - 1 ? (size_t)(e - p) : n - 1;
    memcpy(out, p, l);
    out[l] = 0;
    return 1;
}

int cc_custom_load(const char *dir) {
    char path[512];
    snprintf(path, sizeof path, "%spainting.png", dir);
    if (!file_exists(path)) return 0;
    int w, h, ch;
    uint8_t *px = stbi_load(path, &w, &h, &ch, 4);
    if (!px) return 0;
    if (w != TEMPLATE_SIZE || h != TEMPLATE_SIZE) { stbi_image_free(px); return 0; }
    // the blank template's own pixels (guide lines, frames) are not part of the painting
    snprintf(path, sizeof path, "%sstencil.png", dir);
    int sw, sh;
    uint8_t *st = stbi_load(path, &sw, &sh, &ch, 4);
    if (st && sw == w && sh == h) {
        for (size_t i = 0; i < (size_t)w * h; i++)
            if (!memcmp(px + i * 4, st + i * 4, 4)) px[i * 4 + 3] = 0;
    }
    if (st) stbi_image_free(st);
    // half size, colours weighted by alpha so clear pixels don't fringe the edges
    uint8_t *half = malloc((size_t)TEXTURE_SIZE * TEXTURE_SIZE * 4);
    if (!half) { stbi_image_free(px); return 0; }
    for (int y = 0; y < TEXTURE_SIZE; y++)
        for (int x = 0; x < TEXTURE_SIZE; x++) {
            unsigned sum[4] = {0, 0, 0, 0};
            for (int j = 0; j < 4; j++) {
                const uint8_t *s = px + ((size_t)(y * 2 + (j >> 1)) * w + x * 2 + (j & 1)) * 4;
                for (int k = 0; k < 3; k++) sum[k] += s[k] * s[3];
                sum[3] += s[3];
            }
            uint8_t *d = half + ((size_t)y * TEXTURE_SIZE + x) * 4;
            for (int k = 0; k < 3; k++) d[k] = sum[3] ? sum[k] / sum[3] : 0;
            d[3] = sum[3] / 4;
        }
    stbi_image_free(px);
    g.bm.w = g.bm.h = TEXTURE_SIZE;
    g.bm.rgba = half;
    if (swf_bitmap_loaded) swf_bitmap_loaded(&g.bm);
    for (int i = 0; i < NPARTS; i++) make_fill(&PARTS[i], &g.fill[i]);
    snprintf(g.name, sizeof g.name, "CUSTOM");
    g.base = 1;
    snprintf(path, sizeof path, "%spbp.xml", dir);
    FILE *f = fopen(path, "rb");
    if (f) {
        char xml[1024], v[64];
        size_t len = fread(xml, 1, sizeof xml - 1, f);
        fclose(f);
        xml[len] = 0;
        if (xml_attr(xml, "displayname", v, sizeof v)) snprintf(g.name, sizeof g.name, "%s", v);
        if (xml_attr(xml, "base", v, sizeof v) && atoi(v) >= 1 && atoi(v) <= CC_CHARACTERS) g.base = atoi(v);
    }
    g.p_type = CC_CUSTOM_P_TYPE;
    return 1;
}

// ---- the 32nd character in the game's scripts -------------------------------------------------

// Runs MAIN's own version of a function the host overrode with self.
static AsVal call_script(AsVm *vm, const char *name, AsNative self, AsVal this_, int argc, AsVal *argv) {
    AsVal fn = as_get(vm, as_objv(&vm->root->obj), as_atom(vm, name));
    if (fn.t != V_OBJ || fn.o->native != self) return as_undef();
    fn.o->native = NULL;
    AsVal r = as_call(vm, fn, this_, argc, argv);
    fn.o->native = self;
    return r;
}

static void set_num(AsVm *vm, AsVal o, const char *name, double v) { as_set(vm, o, as_atom(vm, name), as_num(v)); }

// f_InitSaveSystem: the save has room after the 31 records (64 + 48 * 31 is free), and the
// character select (LOBBY/SELECT.swf) and char_colors go up to num_characters: 32 with Paint Jr.,
// 33 with a custom character (slot 32 then stays locked without Paint Jr.'s files).
static AsVal init_save_system(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsVal r = call_script(vm, "f_InitSaveSystem", init_save_system, this_, argc, argv);
    AsVal info = as_get(vm, as_objv(&vm->root->obj), as_atom(vm, "save_data_info"));
    int n = cc_custom_p_type() ? CC_CUSTOM_P_TYPE : CC_PAINTJR_P_TYPE;
    if (info.t == V_OBJ) set_num(vm, info, "num_characters", n);
    return r;
}

static void copy_color(AsVm *vm, AsVal colors, AsVal from, int to) {
    char idx[8];
    snprintf(idx, sizeof idx, "%d", to);
    if (colors.t == V_OBJ && from.t == V_OBJ) as_set(vm, colors, as_str(vm, idx), from);
}

// f_InitCharColors: the custom character's select-screen background and HUD colour are its
// base's; Paint Jr.'s are the Blue Knight's (the PC's MAIN has none of its own for him)
static AsVal init_char_colors(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsVal r = call_script(vm, "f_InitCharColors", init_char_colors, this_, argc, argv);
    static const char *const KNIGHT[] = {"", "color_green", "color_red", "color_blue", "color_orange"};
    AsVal root = as_objv(&vm->root->obj);
    AsVal colors = as_get(vm, root, as_atom(vm, "char_colors"));
    if (cc_paintjr_p_type()) copy_color(vm, colors, as_get(vm, root, as_atom(vm, "color_blue")), CC_PAINTJR_P_TYPE);
    if (!cc_custom_p_type()) return r;
    AsVal base = g.base <= 4 ? as_get(vm, root, as_atom(vm, KNIGHT[g.base]))
                             : (colors.t == V_OBJ && g.base < colors.o->nitems ? colors.o->items[g.base] : as_undef());
    copy_color(vm, colors, base, CC_CUSTOM_P_TYPE);
    return r;
}

// f_SetPlayerMagic(player, type, human): f_SpawnPlayer gives a character n of 5..31 helmet,
// shield and emblem n + 1, then calls this with n + 1. For the custom character (33) it sets up
// its base instead: the base's magic, arrows and part frames (the parts the template paints
// are drawn from the painting, the rest show the base's).
static AsVal set_player_magic(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsVal p = argc > 0 ? argv[0] : as_undef();
    int type = argc > 1 ? as_toint32(vm, argv[1]) : 0;
    if (type != CC_CUSTOM_P_TYPE + 1 || argc < 3 || !as_tobool(vm, argv[2]) || p.t != V_OBJ)
        return call_script(vm, "f_SetPlayerMagic", set_player_magic, this_, argc, argv);
    int b = g.base;
    if (b <= 4) {   // the knights' own lines in f_SpawnPlayer
        static const int frame[5] = {0, 2, 4, 3, 5};
        set_num(vm, p, "magic_type", b);
        set_num(vm, p, "helmet", frame[b]);
        set_num(vm, p, "shield", frame[b]);
        set_num(vm, p, "emblem", frame[b]);
        set_num(vm, p, "arrow_type", frame[b]);
        if (b == 1) set_num(vm, p, "u_punch2_2", 2);
        return as_undef();
    }
    set_num(vm, p, "helmet", b + 1);
    set_num(vm, p, "shield", b + 1);
    set_num(vm, p, "emblem", b + 1 == 23 ? 19 : b + 1 == 18 ? 23 : b + 1 == 31 ? 18 : b + 1);
    if (b + 1 == 18) set_num(vm, p, "shield", 21);
    AsVal args[3] = {p, as_num(b + 1), argv[2]};
    return call_script(vm, "f_SetPlayerMagic", set_player_magic, this_, 3, args);
}

void cc_custom_init(AsVm *vm) {
    if (!cc_custom_p_type() && !cc_paintjr_p_type()) return;
    as_override(vm, "f_InitSaveSystem", init_save_system);
    as_override(vm, "f_InitCharColors", init_char_colors);
    if (cc_custom_p_type()) as_override(vm, "f_SetPlayerMagic", set_player_magic);
}
