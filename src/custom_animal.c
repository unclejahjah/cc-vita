// Custom animal orb: see custom_animal.h.
#include "custom_animal.h"
#include "as_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_GIF
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../third_party/stb/stb_image.h"
#pragma GCC diagnostic pop

#define MAX_TEXTURE 256      // the picture is scaled down to this (a pet is about 60 pixels tall)
#define FIRST_TEXT (180 + 3 * CC_CUSTOM_ANIMAL)   // the Vet's three lines (180 + 3 * animal_type)
#define LINE_CHARS 19        // what fits on one chalkboard line (its fields are 22 wide)
// its Animal Ark spot: right of the Golden Whale's (barn29, 2516,-125), on the same straw
#define ARK_X 2626.0
#define ARK_Y -125.0

static struct {
    int loaded, off;
    char name[64], lines[3][64];
    int stat[4];             // strength, defense, agility, magic
    int like;
    int every;               // its own ability: every this many frames (30 a second)...
    float heal;              // ...heals its owner this share of their health (0.05 = 5%)
    int gold;                // ...and drops this many coins
    float size, mirror;
    Bitmap bm;
    BitmapFill fill;         // the picture on the unit square
} g;

int cc_animal_type(void) { return g.loaded && !g.off ? CC_CUSTOM_ANIMAL : 0; }
const char *cc_animal_name(void) { return g.name; }
void cc_animal_set_active(int on) { g.off = !on; }

// pet.xml: an attribute's text, or 0 if it's not there
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

// The description in up to three chalkboard lines, broken at spaces.
static void wrap(const char *text) {
    memset(g.lines, 0, sizeof g.lines);
    const char *p = text;
    for (int line = 0; line < 3 && *p; line++) {
        while (*p == ' ') p++;
        size_t n = strlen(p), cut = n;
        if (n > LINE_CHARS) {
            cut = LINE_CHARS;
            while (cut > 0 && p[cut] != ' ') cut--;
            if (!cut) cut = LINE_CHARS;   // one long word: break it
        }
        if (line == 2 && cut < n) cut = n < sizeof g.lines[2] - 1 ? n : sizeof g.lines[2] - 1;   // the rest
        memcpy(g.lines[line], p, cut);
        p += cut;
    }
}

static void unit_quad(BitmapFill *bf) {
    static const float xy[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    Mesh *m = &bf->mesh;
    m->nverts = 6;
    m->xy = malloc(sizeof xy);
    m->uv = malloc(sizeof xy);
    memcpy(m->xy, xy, sizeof xy);
    memcpy(m->uv, xy, sizeof xy);
    m->bounds[0] = m->bounds[1] = 0;
    m->bounds[2] = m->bounds[3] = 1;
    bf->bitmap = &g.bm;
    bf->clip = 1;
    bf->smooth = 1;
}

int cc_animal_load(const char *dir) {
    char path[512];
    snprintf(path, sizeof path, "%spet.png", dir);
    int w, h, ch;
    uint8_t *px = stbi_load(path, &w, &h, &ch, 4);
    if (!px) return 0;
    // down to MAX_TEXTURE by whole steps, colours weighted by alpha so clear pixels don't fringe
    int step = 1;
    while (w / step > MAX_TEXTURE || h / step > MAX_TEXTURE) step++;
    int tw = w / step, th = h / step;
    uint8_t *t = malloc((size_t)tw * th * 4);
    if (!t || !tw || !th) { free(t); stbi_image_free(px); return 0; }
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            unsigned sum[4] = {0, 0, 0, 0};
            for (int j = 0; j < step; j++)
                for (int i = 0; i < step; i++) {
                    const uint8_t *s = px + ((size_t)(y * step + j) * w + x * step + i) * 4;
                    for (int k = 0; k < 3; k++) sum[k] += s[k] * s[3];
                    sum[3] += s[3];
                }
            uint8_t *d = t + ((size_t)y * tw + x) * 4;
            for (int k = 0; k < 3; k++) d[k] = sum[3] ? sum[k] / sum[3] : 0;
            d[3] = sum[3] / (step * step);
        }
    stbi_image_free(px);
    g.bm.w = tw;
    g.bm.h = th;
    g.bm.rgba = t;
    if (swf_bitmap_loaded) swf_bitmap_loaded(&g.bm);
    unit_quad(&g.fill);

    snprintf(g.name, sizeof g.name, "Custom Pet");
    char desc[256] = "";
    g.size = 1;
    g.mirror = 0;
    snprintf(path, sizeof path, "%spet.xml", dir);
    FILE *f = fopen(path, "rb");
    if (f) {
        char xml[2048], v[256];
        size_t len = fread(xml, 1, sizeof xml - 1, f);
        fclose(f);
        xml[len] = 0;
        static const char *const STATS[4] = {"strength", "defense", "agility", "magic"};
        if (xml_attr(xml, "displayname", v, sizeof v)) snprintf(g.name, sizeof g.name, "%.63s", v);
        if (xml_attr(xml, "description", v, sizeof v)) snprintf(desc, sizeof desc, "%s", v);
        for (int i = 0; i < 4; i++)
            if (xml_attr(xml, STATS[i], v, sizeof v)) {
                int n = atoi(v);
                g.stat[i] = n < -9 ? -9 : n > 9 ? 9 : n;
            }
        if (xml_attr(xml, "like", v, sizeof v) && atoi(v) >= 1 && atoi(v) <= CC_ANIMALS) g.like = atoi(v);
        if (xml_attr(xml, "every", v, sizeof v) && atof(v) >= 0.5 && atof(v) <= 600) g.every = (int)(atof(v) * 30);
        if (xml_attr(xml, "heal", v, sizeof v) && atof(v) > 0 && atof(v) <= 100) g.heal = (float)atof(v) / 100;
        if (xml_attr(xml, "gold", v, sizeof v) && atoi(v) > 0) g.gold = atoi(v) < 10 ? atoi(v) : 10;
        if (xml_attr(xml, "size", v, sizeof v) && atof(v) > 0.1 && atof(v) < 4) g.size = (float)atof(v);
        if (xml_attr(xml, "facing", v, sizeof v) && !strcmp(v, "left")) g.mirror = 1;
    }
    if (!desc[0]) snprintf(desc, sizeof desc, "%s is a pet of your very own.", g.name);
    wrap(desc);
    g.loaded = 1;
    return 1;
}

// ---- drawing ----------------------------------------------------------------------------------

static AsStr *A_animal_type;

static int animal_type_of(AsMc *mc) {
    AsProp *p = mc && A_animal_type ? as_own(&mc->obj, A_animal_type) : NULL;
    return p && p->v.t == V_NUM ? (int)p->v.n : 0;
}

// ANIMALS.swf's body clip: one frame per animal (29), on _parent._parent.animal_type; the
// custom pet's number is past its end, so it stands on the last (the Golden Whale's) frame, and
// the picture is drawn in that frame's place instead: as wide or tall as it, standing on its foot.
const BitmapFill *cc_animal_art(AsMc *mc, Mat *box) {
    if (!cc_animal_type() || !mc->def || mc->def->nframes != CC_ANIMALS || !mc->parent) return NULL;
    if (animal_type_of(mc->parent->parent) != CC_CUSTOM_ANIMAL) return NULL;
    float b[4];
    render_mc_bounds(mc, MAT_IDENTITY, b);
    if (b[2] <= b[0] || b[3] <= b[1]) return NULL;
    float bw = b[2] - b[0], bh = b[3] - b[1];
    float k = bw / g.bm.w < bh / g.bm.h ? bw / g.bm.w : bh / g.bm.h;
    float w = g.bm.w * k * g.size, h = g.bm.h * k * g.size;
    float cx = (b[0] + b[2]) / 2;
    *box = (Mat){g.mirror ? -w : w, 0, 0, h, g.mirror ? cx + w / 2 : cx - w / 2, b[3] - h};
    return &g.fill;
}

const char *cc_animal_text(AsVm *vm, int n) {
    if (!cc_animal_type() || n < FIRST_TEXT || n > FIRST_TEXT + 2) return NULL;
    // only on the chalkboard: these numbers are other words elsewhere (Castle Keep's)
    AsMc *tf = vm->text_field;
    if (!tf || !tf->parent || !tf->parent->name || strcmp(tf->parent->name->s, "chalkboard")) return NULL;
    return g.lines[n - FIRST_TEXT];
}

// ---- the game's scripts -----------------------------------------------------------------------

static AsVal get(AsVm *vm, AsVal o, const char *name) { return as_get(vm, o, as_atom(vm, name)); }
static void set_num(AsVm *vm, AsVal o, const char *name, double v) { as_set(vm, o, as_atom(vm, name), as_num(v)); }
static AsVal root(AsVm *vm) { return as_objv(&vm->root->obj); }

// The custom pet's fp_timer, run every frame with the pet: its like-pet's ability (kept in
// like_timer), then its own every g.every frames while its owner is alive: heal (as the Troll
// does, f_Heal) and coins (as the Golden Whale drops them, f_ItemSpawn item 9, gem_type 13).
static AsVal custom_timer(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)this_;
    AsVal a = argc > 0 ? argv[0] : as_undef();
    if (a.t != V_OBJ) return as_undef();
    AsVal like = get(vm, a, "like_timer");
    if (like.t == V_OBJ) as_call(vm, like, as_undef(), 1, &a);
    if (!g.every || (!g.heal && !g.gold)) return as_undef();
    double t = as_tonum(vm, get(vm, a, "custom_timer")) + 1;
    if (t < g.every) { set_num(vm, a, "custom_timer", t); return as_undef(); }
    set_num(vm, a, "custom_timer", 0);
    AsVal owner = get(vm, a, "owner");
    if (owner.t != V_OBJ || !as_tobool(vm, get(vm, owner, "alive")) || as_tonum(vm, get(vm, owner, "health")) <= 0)
        return as_undef();
    if (g.heal) {
        double hp = as_tonum(vm, get(vm, owner, "health")), max = as_tonum(vm, get(vm, owner, "health_max"));
        if (hp < max) {
            AsVal args[2] = {owner, as_num(max * g.heal)};
            as_call_method(vm, root(vm), "f_Heal", 2, args);
        }
    }
    for (int i = 0; i < g.gold; i++) {
        AsVal args[3] = {get(vm, a, "_x"), get(vm, a, "_y"), as_num(9)};
        AsVal coin = as_call_method(vm, root(vm), "f_ItemSpawn", 3, args);
        if (coin.t == V_OBJ) set_num(vm, coin, "gem_type", 13);
    }
    return as_undef();
}

// f_SetAnimalTimer(animal): the pet's stat points and the ability it runs every frame (fp_timer).
// The custom pet takes its like-pet's (if any), then its own points and ability on top.
static AsVal set_animal_timer(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsVal a = argc > 0 ? argv[0] : as_undef();
    int custom = a.t == V_OBJ && as_tonum(vm, get(vm, a, "animal_type")) == CC_CUSTOM_ANIMAL;
    if (custom && g.like) set_num(vm, a, "animal_type", g.like);
    AsVal r = as_call_script(vm, this_, argc, argv);
    if (!custom) return r;
    set_num(vm, a, "animal_type", CC_CUSTOM_ANIMAL);
    as_set(vm, a, as_atom(vm, "like_timer"), g.like ? get(vm, a, "fp_timer") : as_undef());
    if (get(vm, root(vm), "f_CustomAnimalTimer").t == V_UNDEF)
        as_def_fn(vm, &vm->root->obj, "f_CustomAnimalTimer", custom_timer);
    as_set(vm, a, as_atom(vm, "fp_timer"), get(vm, root(vm), "f_CustomAnimalTimer"));
    set_num(vm, a, "custom_timer", 0);
    static const char *const STATS[4] = {"strength", "defense", "agility", "magic"};
    for (int i = 0; i < 4; i++)
        if (g.stat[i]) set_num(vm, a, STATS[i], as_tonum(vm, get(vm, a, STATS[i])) + g.stat[i]);
    return r;
}

// f_AssignAnimal(player, type): with the custom pet off (Wi-Fi games), its owner goes without.
static AsVal assign_animal(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    if (argc > 1 && as_tonum(vm, argv[1]) == CC_CUSTOM_ANIMAL && !cc_animal_type()) return as_num(0);
    return as_call_script(vm, this_, argc, argv);
}

// The Animal Ark's f_InitAnimals puts each owned pet nobody has on its spot, barn1..barn29 (the
// trash can sends a pet back to barn<type>). The custom pet gets a spot of its own, barn30.
static AsVal init_animals(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    AsVal r = as_call_script(vm, this_, argc, argv);
    if (!cc_animal_type()) return r;
    int found;
    AsVal game = as_resolve_path(vm, vm->root, "loader.game.game", &found);
    if (game.t != V_OBJ || get(vm, game, "barn29").t != V_OBJ) return r;   // not the Ark
    AsObj *spot = as_obj_new(vm, OBJ_PLAIN, vm->object_proto);
    set_num(vm, as_objv(spot), "_x", ARK_X);
    set_num(vm, as_objv(spot), "_y", ARK_Y);
    as_set(vm, game, as_atom(vm, "barn30"), as_objv(spot));
    for (int i = 1; i <= 4; i++) {   // a player has it already
        char hud[8];
        snprintf(hud, sizeof hud, "hud%d", i);
        AsVal h = get(vm, root(vm), hud);
        if (h.t == V_OBJ && as_tobool(vm, get(vm, h, "active")) &&
            as_tonum(vm, get(vm, h, "animal_type")) == CC_CUSTOM_ANIMAL)
            return r;
    }
    AsVal args[3] = {as_num(CC_CUSTOM_ANIMAL), as_num(ARK_X), as_num(ARK_Y)};
    AsVal pet = as_call_method(vm, root(vm), "f_PlaceAnimal", 3, args);
    if (pet.t == V_OBJ) as_set(vm, pet, as_atom(vm, "barn"), as_objv(spot));
    return r;
}

void cc_animal_init(AsVm *vm) {
    A_animal_type = as_atom(vm, "animal_type");
    if (!g.loaded) return;
    as_override(vm, "f_SetAnimalTimer", set_animal_timer);
    as_override(vm, "f_AssignAnimal", assign_animal);
    as_override(vm, "f_InitAnimals", init_animals);
}
