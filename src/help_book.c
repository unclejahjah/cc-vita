// Help & Options' books over the art PAUSE.swf and MENU.swf share: "bookbg" (the open book),
// "pageturn" (a page flipping over), "howtoplay" (seven pages of pictures, with text fields
// the executable fills) and "volume" (a frame, a highlight and ten blocks v1..v10). The
// executable's own book code isn't in the files, so this lays them out as it appears to: the
// book in the middle of the 848x480 stage, a page turn on L/R, the words in the game font.
// The words come from the text table (strings_en.txt, made from the user's castle.exe):
// help.howto.<page>.<field> for the How to Play fields, help.* for the rest; a few plain
// fallbacks keep the Controls and Settings pages readable without it.
#include "help_book.h"
#include "as_render.h"
#include "cc_buttons.h"
#include "cc_host.h"
#include "strtab.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STAGE_W 848.0f
#define STAGE_H 480.0f
#define TURN_SECONDS 0.45f

void help_book_open(HelpBook *hb, int book, int music, int sfx) {
    memset(hb, 0, sizeof *hb);
    hb->book = book;
    hb->volume[0] = music;
    hb->volume[1] = sfx;
}

int help_book_update(HelpBook *hb, unsigned pressed, float dt, int *sound) {
    *sound = -1;
    hb->time += dt;
    hb->alpha = fminf(1, hb->alpha + dt * 5);
    if (hb->turn > 0) {
        hb->turn -= dt;
        if (hb->turn < 0) hb->turn = 0;
        return 1;
    }
    if (pressed & HB_BACK) {
        *sound = 1;
        return 0;
    }
    if (hb->book == HB_HOWTO) {
        int step = pressed & (HB_NEXT | HB_RIGHT) ? 1 : pressed & (HB_PREV | HB_LEFT) ? -1 : 0;
        if (step && hb->page + step >= 0 && hb->page + step < HB_HOWTO_PAGES) {
            hb->page += step;
            hb->turn = TURN_SECONDS;
            hb->turn_dir = step;
            *sound = 2;
        }
    } else if (hb->book == HB_SETTINGS) {
        if (pressed & (HB_UP | HB_DOWN)) {
            hb->row ^= 1;
            *sound = 0;
        }
        int step = pressed & HB_RIGHT ? 1 : pressed & HB_LEFT ? -1 : 0;
        int v = hb->volume[hb->row] + step;
        if (step && v >= 0 && v <= HB_VOLUME_STEPS) {
            hb->volume[hb->row] = v;
            hb->changed = 1;
            *sound = 0;
        }
    }
    return 1;
}

// ---- drawing --------------------------------------------------------------------------

static int find_export(Movie *mv, const char *name) {
    for (int c = 0; c < 65536; c++)
        if (mv->chars[c].export && !strcmp(mv->names[mv->chars[c].export], name)) return c;
    return -1;
}

static void list_free(DrawList *dl) {
    free(dl->items);
    free(dl->cv);
    free(dl->cmd);
}

// pixel bounds (x0, y0, x1, y1) of what a draw list holds
static void list_bounds(const DrawList *dl, float b[4]) {
    b[0] = b[1] = 1e30f;
    b[2] = b[3] = -1e30f;
    for (int k = 0; k < dl->n; k++) {
        const float *mb = dl->items[k].mesh->bounds;
        Mat mm = dl->items[k].m;
        for (int q = 0; q < 4; q++) {
            float x = mb[q & 1 ? 2 : 0], y = mb[q & 2 ? 3 : 1];
            float px = mm.a * x + mm.c * y + mm.tx, py = mm.b * x + mm.d * y + mm.ty;
            if (px < b[0]) b[0] = px;
            if (px > b[2]) b[2] = px;
            if (py < b[1]) b[1] = py;
            if (py > b[3]) b[3] = py;
        }
    }
}

// pixel bounds of a clip's first frame at scale 1
static void clip_bounds(Movie *mv, int cid, float b[4]) {
    DrawList dl = {0};
    render_begin(&dl);
    render_char(mv, cid, MAT_IDENTITY, CX_IDENTITY, 0, &dl);
    list_bounds(&dl, b);
    for (int i = 0; i < 4; i++) b[i] /= 20;
    list_free(&dl);
}

// one sprite frame (the clips here stop on their first frame, so render_char can't pick one)
static void sprite_frame(Movie *mv, int cid, int frame, Mat m, CXform cx, DrawList *out) {
    if (cid < 0 || mv->chars[cid].type != CHAR_SPRITE) return;
    const Sprite *sp = mv->chars[cid].ptr;
    if (frame < 0 || frame >= sp->nframes) return;
    render_display_list(mv, &sp->frames[frame], m, cx, 0, out);
}

static const char *words(const char *key, const char *fallback) {
    const char *s = strtab_get(key);
    return s ? s : fallback;
}

// a line of the game font in rgb at stage x, y (y: the middle of the line); align 0 right,
// 1 left, 2 centre; size in stage pixels
static void text(DrawList *out, float k, const char *s, float x, float y, float size, int align, uint32_t rgb,
                 float alpha) {
    if (!s || !*s) return;
    Mat m = {k, 0, 0, k, 0, 0};
    CXform cx = {{(rgb >> 16 & 255) / 255.0f, (rgb >> 8 & 255) / 255.0f, (rgb & 255) / 255.0f, alpha}, {0, 0, 0, 0}};
    as_render_host_text(s, x, y, size / 20, size / 20, align, m, cx, out);
}

// a straight stroke from a to b (stage pixels), w wide
static void stroke(DrawList *out, float k, float ax, float ay, float bx, float by, float w, const uint8_t c[4],
                   float alpha) {
    static Shape s[256];   // reused round robin: more than one frame ever draws
    static int next;
    Shape *sh = &s[next++ & 255];
    float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.5f) return;
    float nx = -dy / l * w / 2, ny = dx / l * w / 2;
    // the shape is a unit-free quad, so its tessellation can be kept: rebuilt each call
    free(sh->mesh.xy);
    free(sh->mesh.rgba);
    if (sh->fills) {
        free(sh->fills[0].pts);
        free(sh->fills[0].loop_start);
        free(sh->fills);
    }
    memset(sh, 0, sizeof *sh);
    ShapeFill *f = calloc(1, sizeof *f);
    f->style.type = FILL_SOLID;
    memcpy(f->style.color, c, 4);
    f->npoints = 4;
    f->pts = malloc(sizeof(float) * 8);
    float p[8] = {ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny};
    for (int i = 0; i < 8; i++) f->pts[i] = p[i] * 20;
    f->nloops = 1;
    f->loop_start = malloc(sizeof(int) * 2);
    f->loop_start[0] = 0;
    f->loop_start[1] = 4;
    sh->fills = f;
    sh->nfills = 1;
    Mat m = {k / 20, 0, 0, k / 20, 0, 0};
    CXform cx = {{1, 1, 1, alpha}, {0, 0, 0, 0}};
    render_shape(sh, m, cx, out);
}

typedef struct {
    Movie *mv;
    int book, pageturn, howto, volume, bar, block, buttons[4], trig[2];
    int icon, icon_inner;      // "buttonicon" and the clip in it with a frame per button
    Mat icon_m;
    CXform icon_cx;
    float book_b[4];           // the book's bounds (stage pixels, at scale 1, from its origin)
    float button_b[4][4], trig_b[2][4], volume_b[4];
    Mat place;                 // book space (twips) -> stage pixels
    Shape vita, dpad[2];       // dpad: left lit, right lit
    float spots[CC_VITA_SPOTS][2];
} Art;

static Art *art_for(Movie *mv) {
    static Art a;
    if (a.mv == mv) return &a;
    memset(&a, 0, sizeof a);
    a.mv = mv;
    a.book = find_export(mv, "bookbg");
    a.pageturn = find_export(mv, "pageturn");
    a.howto = find_export(mv, "howtoplay");
    a.volume = find_export(mv, "volume");
    a.icon = find_export(mv, "buttonicon");
    if (a.icon >= 0) {
        const Sprite *is = mv->chars[a.icon].ptr;
        if (is->nframes && is->frames[0].count && mv->chars[is->frames[0].places[0].cid].type == CHAR_SPRITE) {
            a.icon_inner = is->frames[0].places[0].cid;
            a.icon_m = is->frames[0].places[0].m;
            a.icon_cx = is->frames[0].places[0].cx;
        } else {
            a.icon = -1;
        }
    }
    static const char *bn[4] = {"Abutton", "Bbutton", "Xbutton", "Ybutton"};
    for (int i = 0; i < 4; i++) {
        a.buttons[i] = find_export(mv, bn[i]);
        if (a.buttons[i] >= 0) clip_bounds(mv, a.buttons[i], a.button_b[i]);
    }
    a.trig[0] = find_export(mv, "Ltrigger");
    a.trig[1] = find_export(mv, "Rtrigger");
    for (int i = 0; i < 2; i++)
        if (a.trig[i] >= 0) clip_bounds(mv, a.trig[i], a.trig_b[i]);
    if (a.volume >= 0) {
        clip_bounds(mv, a.volume, a.volume_b);
        const Sprite *vs = mv->chars[a.volume].ptr;
        a.bar = a.block = -1;
        for (int i = 0; vs->nframes && i < vs->frames[0].count; i++) {
            int c = vs->frames[0].places[i].cid;
            if (mv->chars[c].type != CHAR_SPRITE) continue;
            if (vs->frames[0].places[i].name) a.block = c;
            else a.bar = c;
        }
    }
    // the book: where the original puts it, at the stage's top left at full size, so it fills
    // the screen (its bottom runs off it) and the page turn sweeps across all of it
    a.place = (Mat){1.0f / 20, 0, 0, 1.0f / 20, 0, 0};
    if (a.book >= 0) clip_bounds(mv, a.book, a.book_b);
    cc_vita_drawing(&a.vita, a.spots);
    cc_dpad_icon(&a.dpad[0], 0);
    cc_dpad_icon(&a.dpad[1], 1);
    return &a;
}

static float place_scale(const Art *a) { return a->place.a * 20; }

// book space pixels -> stage pixels
static void to_stage(const Art *a, float x, float y, float *sx, float *sy) {
    *sx = a->place.a * 20 * x + a->place.tx;
    *sy = a->place.d * 20 * y + a->place.ty;
}

// a button clip centred at stage x, y, d stage pixels across
static void button(DrawList *out, Movie *mv, int cid, const float b[4], float k, float x, float y, float d,
                   float alpha) {
    if (cid < 0) return;
    float w = fmaxf(b[2] - b[0], b[3] - b[1]);
    if (w <= 0) return;
    float s = d / w;
    Mat m = {s * k / 20, 0, 0, s * k / 20, (x - (b[0] + b[2]) / 2 * s) * k, (y - (b[1] + b[3]) / 2 * s) * k};
    CXform cx = {{1, 1, 1, alpha}, {0, 0, 0, 0}};
    render_char(mv, cid, m, cx, 0, out);
}

static const uint32_t INK = 0xffffff, TITLE = 0xffffff;   // the book's text fields are all white

// ---- the pages -------------------------------------------------------------------------

// How to Play: the clip's page (its own copy of the book included) and its words
static void howto_page(DrawList *out, Art *a, float k, int page, float alpha) {
    Movie *mv = a->mv;
    if (a->howto < 0) return;
    const Sprite *sp = mv->chars[a->howto].ptr;
    if (page >= sp->nframes) return;
    Mat screen = {k, 0, 0, k, 0, 0}, m = mat_mul(screen, a->place);
    CXform cx = {{1, 1, 1, alpha}, {0, 0, 0, 0}};
    const Frame *f = &sp->frames[page];
    // the button icons show whichever button the executable sends them to (their clip's
    // frames: X, Y, A, B, LT, RT), which cc_buttons has made Square, Triangle, Cross,
    // Circle, L and R
    static const struct { int page, depth, frame; } icons[] = {
        {0, 61, 0}, {0, 63, 1},                 // Uppercut: light, heavy
        {0, 99, 0}, {0, 101, 0}, {0, 118, 0},   // Fast Attack: light, light, light
        {1, 61, 5}, {1, 63, 2},                 // Magic Jump: R + jump
        {1, 99, 5}, {1, 101, 3},                // Magic Projectile: R + use item
        {1, 118, 5},                            // hold R
        {3, 21, 0},                             // the store's Purchase Item: Square on the Vita
    };
    int masked = 0;
    for (int i = 0; i < f->count; i++) masked |= f->places[i].clip != 0;
    if (masked || a->icon < 0) {
        render_display_list(mv, f, m, cx, 0, out);
    } else {
        for (int i = 0; i < f->count; i++) {
            const Place *p = &f->places[i];
            if (mv->chars[p->cid].type == CHAR_EDITTEXT) continue;
            Mat pm = mat_mul(m, p->m);
            CXform pc = cx_mul(cx, p->cx);
            if (p->cid != a->icon) {
                render_char(mv, p->cid, pm, pc, 0, out);
                continue;
            }
            int fr = 0;
            for (size_t j = 0; j < sizeof icons / sizeof *icons; j++)
                if (icons[j].page == page && icons[j].depth == p->depth) fr = icons[j].frame;
            sprite_frame(mv, a->icon_inner, fr, mat_mul(pm, a->icon_m), cx_mul(pc, a->icon_cx), out);
        }
    }
    for (int i = 0; i < f->count; i++) {
        const Place *p = &f->places[i];
        if (mv->chars[p->cid].type != CHAR_EDITTEXT || !p->name) continue;
        const EditText *et = mv->chars[p->cid].ptr;
        char key[64];
        snprintf(key, sizeof key, "help.howto.%d.%s", page + 1, mv->names[p->name]);
        const char *s = strtab_get(key);
        if (!s) continue;
        // the field's box in book pixels, then on the stage
        float x0 = (p->m.a * et->bounds[0] + p->m.tx) / 20, x1 = (p->m.a * et->bounds[1] + p->m.tx) / 20;
        float y0 = (p->m.d * et->bounds[2] + p->m.ty) / 20;
        float size = et->height / 20.0f;
        float y = y0 + size * 0.75f + 2;
        float x = et->align == 1 ? x1 : et->align == 2 ? (x0 + x1) / 2 : x0 + et->left_margin / 20.0f;
        int al = et->align == 1 ? 0 : et->align == 2 ? 2 : 1;
        float sx, sy;
        to_stage(a, x, y, &sx, &sy);
        uint32_t rgb = (uint32_t)et->color[0] << 16 | (uint32_t)et->color[1] << 8 | et->color[2];
        text(out, k, s, sx, sy, size * place_scale(a) * 1.15f, al, rgb, alpha * et->color[3] / 255.0f);
    }
}

// the book, with How to Play's vine borders and the sword over the title (its first page's
// depths 4 to 6) so the other books look like pages of the same one
static void book_art(DrawList *out, Art *a, float k, float alpha) {
    if (a->book < 0) return;
    Mat screen = {k, 0, 0, k, 0, 0}, m = mat_mul(screen, a->place);
    CXform cx = {{1, 1, 1, alpha}, {0, 0, 0, 0}};
    render_char(a->mv, a->book, m, cx, 0, out);
    if (a->howto < 0) return;
    const Sprite *sp = a->mv->chars[a->howto].ptr;
    for (int i = 0; sp->nframes && i < sp->frames[0].count; i++) {
        const Place *p = &sp->frames[0].places[i];
        if (p->depth >= 4 && p->depth <= 6) render_char(a->mv, p->cid, mat_mul(m, p->m), cx_mul(cx, p->cx), 0, out);
    }
}

// the middle of the book on the stage, and its width
static void book_middle(const Art *a, float *x, float *y, float *w) {
    // the middle of the part of the book that's on the screen
    to_stage(a, (a->book_b[0] + a->book_b[2]) / 2, (a->book_b[1] + a->book_b[3]) / 2, x, y);
    float top, bottom, dummy;
    to_stage(a, 0, a->book_b[1], &dummy, &top);
    to_stage(a, 0, a->book_b[3], &dummy, &bottom);
    *y = (top + fminf(bottom, STAGE_H)) / 2;
    *w = (a->book_b[2] - a->book_b[0]) * place_scale(a);
}

static void page_title(DrawList *out, const Art *a, float k, const char *s, float alpha) {
    float x, y, w;
    book_middle(a, &x, &y, &w);
    float tx, ty;
    to_stage(a, 0, 55, &tx, &ty);   // where the How to Play titles sit
    text(out, k, s, x, ty, 28 * 1.15f * place_scale(a), 2, TITLE, alpha);
}

// Controls: laid out like the original's "controls" clip: the pad in the middle of the book,
// each label beside it in the game font (left ones ending near the pad, right ones starting
// near it, rows 26 apart) with a thin black line to its control. Here the pad is a Vita, so
// the labels follow the Vita's controls.
static void controls_page(DrawList *out, Art *a, float k, float alpha) {
    static const uint8_t LINE[4] = {0, 0, 0, 255};
    book_art(out, a, k, alpha);
    page_title(out, a, k, words("help.title.controls", "Controls"), alpha);
    float mx, my, bw;
    book_middle(a, &mx, &my, &bw);
    float S = place_scale(a) * 1.4f;           // the clip's pixels -> stage pixels (a little bigger than the original)
    float ox = mx, oy = my + 52 * place_scale(a);   // the clip's origin
    float vs = 290.0f / 300 * S;               // the Vita, 290 clip pixels across
    float vx = ox, vy = oy - 18 * S;
    Mat vm = {vs * k / 20, 0, 0, vs * k / 20, vx * k, vy * k};
    CXform cx = {{1, 1, 1, alpha}, {0, 0, 0, 0}};
    render_shape(&a->vita, vm, cx, out);
    float px[CC_VITA_SPOTS], py[CC_VITA_SPOTS];
    for (int i = 0; i < CC_VITA_SPOTS; i++) {
        px[i] = vx + a->spots[i][0] / 20 * vs;
        py[i] = vy + a->spots[i][1] / 20 * vs;
    }
    static const struct { int spot, btn; } faces[4] = {
        {CC_VITA_CROSS, 0}, {CC_VITA_CIRCLE, 1}, {CC_VITA_SQUARE, 2}, {CC_VITA_TRIANGLE, 3}};
    for (int i = 0; i < 4; i++)
        button(out, a->mv, a->buttons[faces[i].btn], a->button_b[faces[i].btn], k, px[faces[i].spot],
               py[faces[i].spot], 17 * vs, alpha);
    for (int i = 0; i < 2; i++)   // the shoulders sit a little lower, so they stay small on the body
        button(out, a->mv, a->trig[i], a->trig_b[i], k, px[i ? CC_VITA_R : CC_VITA_L],
               py[i ? CC_VITA_R : CC_VITA_L] + 3 * vs, 34 * vs, alpha);
    // the original's order: its left labels, then Y, X, B, A down the right
    static const struct { int spot; const char *key, *fallback; float row; } left[] = {
        {CC_VITA_L, "help.controls.block", "Block", -3.6f},
        {CC_VITA_DPAD, "help.controls.run", "Run", -1.6f},
        {CC_VITA_LSTICK, "help.controls.move", "Move", 0.4f},
    }, right[] = {
        {CC_VITA_R, "help.controls.magic", "Magic", -3.6f},
        {CC_VITA_TRIANGLE, "help.controls.heavy", "Heavy Attack", -2.6f},
        {CC_VITA_SQUARE, "help.controls.light", "Light Attack", -1.6f},
        {CC_VITA_CIRCLE, "help.controls.item", "Use Item", -0.6f},
        {CC_VITA_CROSS, "help.controls.jump", "Jump", 0.4f},
        {CC_VITA_RSTICK, "help.controls.select", "Item Select", 1.4f},
        {CC_VITA_START, "help.controls.pause", "Pause", 2.4f},
    };
    float size = 19 * S, gap = 26 * S;
    float lx = vx - 150 * vs - 16 * S, rx = vx + 150 * vs + 16 * S;
    int nl = sizeof left / sizeof *left, nr = sizeof right / sizeof *right;
    for (int side = 0; side < 2; side++) {
        int n = side ? nr : nl;
        for (int i = 0; i < n; i++) {
            int spot = side ? right[i].spot : left[i].spot;
            const char *s = side ? words(right[i].key, right[i].fallback) : words(left[i].key, left[i].fallback);
            float ty = vy + (side ? right[i].row : left[i].row) * gap;
            float tx = side ? rx : lx;
            text(out, k, s, tx, ty, size, side ? 1 : 0, INK, alpha);
            float ex = tx + (side ? -5 : 5) * S, sx = px[spot], sy = py[spot];
            // the line stops at the edge of the control
            float r = (spot == CC_VITA_L || spot == CC_VITA_R ? 12 : spot == CC_VITA_DPAD ? 18
                       : spot == CC_VITA_LSTICK || spot == CC_VITA_RSTICK ? 15 : 8.5f) * vs;
            float dx = ex - sx, dy = ty - sy, l = sqrtf(dx * dx + dy * dy);
            if (l > r) stroke(out, k, ex, ty, sx + dx / l * r, sy + dy / l * r, 1.3f * S, LINE, alpha);
        }
    }
}

// one "volume" bar: its frame, the highlight when chosen, and level blocks lit
static void volume_bar(DrawList *out, Art *a, float k, float x, float y, float w, int level, int chosen,
                       float alpha) {
    Movie *mv = a->mv;
    if (a->volume < 0) return;
    float bw = a->volume_b[2] - a->volume_b[0];
    if (bw <= 0) return;
    float s = w / bw;
    Mat m = {s * k / 20, 0, 0, s * k / 20, (x - (a->volume_b[0] + a->volume_b[2]) / 2 * s) * k,
             (y - (a->volume_b[1] + a->volume_b[3]) / 2 * s) * k};
    CXform cx = {{1, 1, 1, alpha}, {0, 0, 0, 0}};
    const Sprite *vs = mv->chars[a->volume].ptr;
    if (!vs->nframes) return;
    const Frame *f = &vs->frames[0];
    for (int i = 0; i < f->count; i++) {
        const Place *p = &f->places[i];
        Mat pm = mat_mul(m, p->m);
        CXform pc = cx_mul(cx, p->cx);
        if (p->cid == a->bar) sprite_frame(mv, p->cid, chosen ? 1 : 0, pm, pc, out);
        else if (p->cid == a->block && p->name) {
            int n = atoi(mv->names[p->name] + 1);   // v1..v10
            sprite_frame(mv, p->cid, n <= level ? 1 : 0, pm, pc, out);
        } else render_char(mv, p->cid, pm, pc, 0, out);
    }
}

static void settings_page(DrawList *out, Art *a, float k, const HelpBook *hb, float alpha) {
    book_art(out, a, k, alpha);
    page_title(out, a, k, words("help.title.settings", "Settings"), alpha);
    float mx, my, bw;
    book_middle(a, &mx, &my, &bw);
    static const char *keys[2] = {"help.settings.music", "help.settings.sfx"};
    static const char *fb[2] = {"Music Volume", "SFX Volume"};
    float sc = place_scale(a);
    for (int r = 0; r < 2; r++) {
        float y = my - 70 * sc + r * 130 * sc;
        uint32_t c = INK;
        if (r == hb->row) {   // the chosen row pulses yellow like the pause menu's chosen line
            int v = (int)(sinf(hb->time * 10) * 34 + 221) & 255;
            c = (uint32_t)v << 16 | (uint32_t)v << 8;
        }
        text(out, k, words(keys[r], fb[r]), mx, y, 30 * sc, 2, c, alpha);
        volume_bar(out, a, k, mx, y + 50 * sc, bw * 0.40f, hb->volume[r], r == hb->row, alpha);
    }
}

// the words of s that fit in w pixels at size, from *s on; *s moves past them (and the space)
static int wrap(const char **s, float w, float size, char *line, int cap) {
    const char *p = *s, *best = NULL;
    while (*p == ' ') p++;
    if (!*p) return 0;
    for (const char *e = p;; e++) {
        if (*e != ' ' && *e) continue;
        int n = (int)(e - p) < cap - 1 ? (int)(e - p) : cap - 1;
        memcpy(line, p, n);
        line[n] = 0;
        if (best && as_render_host_text_width(line) * size / 20 > w) break;
        best = e;
        if (!*e) break;
    }
    int n = (int)(best - p) < cap - 1 ? (int)(best - p) : cap - 1;
    memcpy(line, p, n);
    line[n] = 0;
    *s = best;
    return 1;
}

// left-aligned words with a dark shadow down and right, so they read on the light page
static void shadow_text(DrawList *out, float k, const char *s, float x, float y, float size, uint32_t rgb,
                        float alpha) {
    float o = fmaxf(1.2f, size / 12);
    text(out, k, s, x + o, y + o, size, 1, 0x1a1408, alpha * 0.9f);
    text(out, k, s, x, y, size, 1, rgb, alpha);
}

// Achievements: the twelve on the book's two pages, each with a tick when it's unlocked,
// its name and what it asks for; the locked ones dimmed
static void achievements_page(DrawList *out, Art *a, float k, const HelpBook *hb, float alpha) {
    static const uint8_t TICK[4] = {0x9c, 0xf0, 0x5a, 255}, BOX[4] = {0xff, 0xff, 0xff, 255},
                         SHADOW[4] = {0, 0, 0, 255};
    book_art(out, a, k, alpha);
    int n = 0;
    for (int id = 1; id <= CC_ACHIEVEMENTS; id++) n += hb->achieved >> id & 1;
    char title[64];
    snprintf(title, sizeof title, "%s  %d / %d", words("help.title.achievements", "Achievements"), n, CC_ACHIEVEMENTS);
    page_title(out, a, k, title, alpha);
    float sc = place_scale(a);
    for (int id = 1; id <= CC_ACHIEVEMENTS; id++) {
        int col = (id - 1) / 6, row = (id - 1) % 6, got = hb->achieved >> id & 1;
        float bx, by;
        to_stage(a, col ? 448 : 104, 108 + row * 57, &bx, &by);
        float al = alpha * (got ? 1 : 0.8f);
        // the box, and the tick in it
        float r = 8 * sc;
        stroke(out, k, bx - r, by - r, bx + r, by - r, 2 * sc, BOX, al);
        stroke(out, k, bx + r, by - r, bx + r, by + r, 2 * sc, BOX, al);
        stroke(out, k, bx + r, by + r, bx - r, by + r, 2 * sc, BOX, al);
        stroke(out, k, bx - r, by + r, bx - r, by - r, 2 * sc, BOX, al);
        if (got) {
            stroke(out, k, bx - r * 0.7f + sc, by + sc, bx - r * 0.1f + sc, by + r * 0.8f + sc, 4 * sc, SHADOW, alpha);
            stroke(out, k, bx - r * 0.1f + sc, by + r * 0.8f + sc, bx + r * 1.2f + sc, by - r * 1.1f + sc, 4 * sc, SHADOW,
                   alpha);
            stroke(out, k, bx - r * 0.7f, by, bx - r * 0.1f, by + r * 0.8f, 4 * sc, TICK, alpha);
            stroke(out, k, bx - r * 0.1f, by + r * 0.8f, bx + r * 1.2f, by - r * 1.1f, 4 * sc, TICK, alpha);
        }
        float tx = bx + 20 * sc;
        shadow_text(out, k, cc_achievement_name(id), tx, by, 21 * sc, got ? 0xffff99 : 0xd8d8d8, al);
        const char *how = cc_achievement_how(id);
        char line[96];
        float ly = by + 19 * sc;
        for (int l = 0; l < 2 && wrap(&how, 280 * sc, 16 * sc, line, sizeof line); l++, ly += 16 * sc)
            shadow_text(out, k, line, tx, ly, 16 * sc, INK, al);
    }
}

// the button prompts along the bottom: a clip and its word
static void prompt(DrawList *out, Art *a, float k, int cid, const float b[4], float x, const char *label,
                   float alpha) {
    float y = 452;
    button(out, a->mv, cid, b, k, x, y, cid == a->trig[0] || cid == a->trig[1] ? 40 : 26, alpha);
    text(out, k, label, x + (cid == a->trig[0] || cid == a->trig[1] ? 26 : 18), y, 16, 1, 0xdddddd, alpha);
}

// the page-turn prompts: the D-pad with the way to press lit
static void dpad_prompt(DrawList *out, Art *a, float k, int right, float x, const char *label, float alpha) {
    float y = 452, d = 30;
    Mat m = {d / 34 * k / 20, 0, 0, d / 34 * k / 20, x * k, y * k};
    CXform cx = {{1, 1, 1, alpha}, {0, 0, 0, 0}};
    render_shape(&a->dpad[right], m, cx, out);
    text(out, k, label, x + d / 2 + 6, y, 16, 1, 0xdddddd, alpha);
}

void help_book_build(const HelpBook *hb, Movie *mv, float k, DrawList *out) {
    Art *a = art_for(mv);
    float alpha = hb->alpha;
    if (hb->book == HB_HOWTO) {
        // turning: the old page under the flip for its first half, the new one after
        int page = hb->page;
        if (hb->turn > TURN_SECONDS / 2) page -= hb->turn_dir;
        howto_page(out, a, k, page, alpha);
        if (hb->turn > 0 && a->pageturn >= 0) {
            float t = 1 - hb->turn / TURN_SECONDS;
            int first = hb->turn_dir > 0 ? 1 : 17, last = hb->turn_dir > 0 ? 16 : 29;
            int fr = first + (int)(t * (last - first) + 0.5f);
            Mat screen = {k, 0, 0, k, 0, 0};
            CXform cx = {{1, 1, 1, alpha}, {0, 0, 0, 0}};
            sprite_frame(mv, a->pageturn, fr, mat_mul(screen, a->place), cx, out);
        }
        char pg[16];
        snprintf(pg, sizeof pg, "%d / %d", hb->page + 1, HB_HOWTO_PAGES);
        text(out, k, pg, STAGE_W / 2, 452, 16, 2, 0xdddddd, alpha);
        if (hb->page > 0) dpad_prompt(out, a, k, 0, STAGE_W * 0.16f, words("help.prev", "prev"), alpha);
        if (hb->page < HB_HOWTO_PAGES - 1) dpad_prompt(out, a, k, 1, STAGE_W * 0.66f, words("help.next", "next"), alpha);
    } else if (hb->book == HB_CONTROLS) {
        controls_page(out, a, k, alpha);
    } else if (hb->book == HB_ACHIEVEMENTS) {
        achievements_page(out, a, k, hb, alpha);
    } else {
        settings_page(out, a, k, hb, alpha);
    }
    prompt(out, a, k, a->buttons[1], a->button_b[1], STAGE_W * 0.82f, words("help.back", "Back"), alpha);
}

// ---- a page for other menus ------------------------------------------------------------

static uint32_t pulse(float time) {   // the chosen line's yellow, as Settings'
    int v = (int)(sinf(time * 10) * 34 + 221) & 255;
    return (uint32_t)v << 16 | (uint32_t)v << 8;
}

void help_book_build_page(const HbPage *pg, Movie *mv, float k, DrawList *out) {
    static const uint8_t BOX[4] = {0xff, 0xff, 0xff, 255};
    Art *a = art_for(mv);
    float alpha = pg->alpha, sc = place_scale(a);
    book_art(out, a, k, alpha);
    if (pg->title) page_title(out, a, k, pg->title, alpha);
    float mx, my, bw;
    book_middle(a, &mx, &my, &bw);
    float dummy, y0, gap = 40 * sc;
    to_stage(a, 0, 135, &dummy, &y0);
    for (int i = 0; i < pg->nlines && i < HB_PAGE_LINES; i++) {
        if (!pg->line[i] || !*pg->line[i]) continue;
        uint32_t c = i == pg->chosen ? pulse(pg->time) : pg->dim >> i & 1 ? 0xb4aca0 : INK;
        text(out, k, pg->line[i], mx, y0 + i * gap, 26 * sc, 2, c, alpha);
    }
    if (pg->code) {
        // letters in boxes, centred, a wider gap at each '-'
        int n = 0, gaps = 0;
        for (const char *p = pg->code; *p; p++) *p == '-' ? gaps++ : n++;
        float cw = 46 * sc, gw = 22 * sc, h = 54 * sc, cy = y0 + pg->code_line * gap + 10 * sc;
        float x = mx - (n * cw + gaps * gw) / 2;
        int slot = 0;
        for (const char *p = pg->code; *p; p++) {
            if (*p == '-') {
                text(out, k, "-", x + gw / 2, cy, 34 * sc, 2, INK, alpha);
                x += gw;
                continue;
            }
            int chosen = slot == pg->code_chosen;
            float bx0 = x + 4 * sc, bx1 = x + cw - 4 * sc, by0 = cy - h / 2, by1 = cy + h / 2;
            float w = (chosen ? 3 : 1.6f) * sc, al = alpha * (chosen ? 1 : 0.7f);
            stroke(out, k, bx0, by0, bx1, by0, w, BOX, al);
            stroke(out, k, bx1, by0, bx1, by1, w, BOX, al);
            stroke(out, k, bx1, by1, bx0, by1, w, BOX, al);
            stroke(out, k, bx0, by1, bx0, by0, w, BOX, al);
            char ch[2] = {*p, 0};
            text(out, k, ch, x + cw / 2, cy + 2 * sc, 38 * sc, 2, chosen ? pulse(pg->time) : INK, alpha);
            if (chosen) {   // up and down arrows: Up / Down change it
                float ax = x + cw / 2, t = by0 - 8 * sc, b = by1 + 8 * sc, r = 7 * sc;
                stroke(out, k, ax - r, t, ax, t - r, 2.5f * sc, BOX, alpha);
                stroke(out, k, ax, t - r, ax + r, t, 2.5f * sc, BOX, alpha);
                stroke(out, k, ax - r, b, ax, b + r, 2.5f * sc, BOX, alpha);
                stroke(out, k, ax, b + r, ax + r, b, 2.5f * sc, BOX, alpha);
            }
            x += cw;
            slot++;
        }
    }
    if (pg->note && *pg->note) {
        const char *s = pg->note;
        char line[128];
        float ny;
        to_stage(a, 0, 362, &dummy, &ny);
        for (int l = 0; l < 2 && wrap(&s, 560 * sc, 18 * sc, line, sizeof line); l++, ny += 21 * sc)
            text(out, k, line, mx, ny, 18 * sc, 2, 0xe8e0d0, alpha);
    }
    if (pg->accept) prompt(out, a, k, a->buttons[0], a->button_b[0], STAGE_W * 0.60f, pg->accept, alpha);
    if (pg->back) prompt(out, a, k, a->buttons[1], a->button_b[1], STAGE_W * 0.82f, pg->back, alpha);
}
