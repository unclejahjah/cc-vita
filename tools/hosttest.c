// Desktop check for the Vita renderer: same parser + tessellator, software
// rasterised to PPM so output can be compared with code/swfproto.
// usage: hosttest PLAYER.swf walk out_prefix [frames]
//        hosttest --level LEVELn.swf PLAYER.swf out_prefix [frames]   (knight walks right)
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include <dirent.h>
#include <strings.h>

#include "../src/as_render.h"
#include "../src/hostfont.h"
#include "../src/cc_buttons.h"
#include "../src/cc_host.h"
#include "../src/pause_menu.h"
#include "../src/enemy.h"

#define SCREEN_W 960
#define SCREEN_H 544
#define MAX_W (SCREEN_W * 2)   // --as with CC_SCALE=2 renders at twice the Vita size
#define MAX_H (SCREEN_H * 2)

static int W = 480, H = 480;
static uint8_t px[MAX_W * MAX_H * 3];
static uint8_t stencil[MAX_W * MAX_H];   // same rules as the GPU path: test stored == ref, then op

static void tri(const Vertex *a, const Vertex *b, const Vertex *c, int ref, int op) {
    float minx = a->x, maxx = a->x, miny = a->y, maxy = a->y;
    const Vertex *v[3] = {a, b, c};
    for (int i = 1; i < 3; i++) {
        if (v[i]->x < minx) minx = v[i]->x;
        if (v[i]->x > maxx) maxx = v[i]->x;
        if (v[i]->y < miny) miny = v[i]->y;
        if (v[i]->y > maxy) maxy = v[i]->y;
    }
    if (maxx < 0 || maxy < 0 || minx >= W || miny >= H) return;
    int x0 = minx < 0 ? 0 : (int)minx, x1 = maxx >= W ? W - 1 : (int)maxx;
    int y0 = miny < 0 ? 0 : (int)miny, y1 = maxy >= H ? H - 1 : (int)maxy;
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (area == 0) return;
    uint32_t col = a->color;
    float al = (col >> 24) / 255.0f;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            float w0 = (b->x - fx) * (c->y - fy) - (b->y - fy) * (c->x - fx);
            float w1 = (c->x - fx) * (a->y - fy) - (c->y - fy) * (a->x - fx);
            float w2 = (a->x - fx) * (b->y - fy) - (a->y - fy) * (b->x - fx);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                uint8_t *s = stencil + y * W + x;
                if (*s != ref) continue;
                if (op == STENCIL_INCR) { if (*s < 255) (*s)++; continue; }
                if (op == STENCIL_DECR) { if (*s > 0) (*s)--; continue; }
                uint8_t *p = px + (y * W + x) * 3;
                for (int k = 0; k < 3; k++) {
                    float s = (col >> (8 * k)) & 0xff;
                    p[k] = (uint8_t)(p[k] * (1 - al) + s * al);
                }
            }
        }
}

// A bitmap-filled triangle: texture coordinates interpolated, nearest texel, times the tint.
static void tri_tex(const Vertex *v[3], const float uv[3][2], const BitmapFill *bf, const float tint[4], int ref, int op) {
    const Bitmap *bm = bf->bitmap;
    float minx = v[0]->x, maxx = minx, miny = v[0]->y, maxy = miny;
    for (int i = 1; i < 3; i++) {
        if (v[i]->x < minx) minx = v[i]->x;
        if (v[i]->x > maxx) maxx = v[i]->x;
        if (v[i]->y < miny) miny = v[i]->y;
        if (v[i]->y > maxy) maxy = v[i]->y;
    }
    if (maxx < 0 || maxy < 0 || minx >= W || miny >= H) return;
    int x0 = minx < 0 ? 0 : (int)minx, x1 = maxx >= W ? W - 1 : (int)maxx;
    int y0 = miny < 0 ? 0 : (int)miny, y1 = maxy >= H ? H - 1 : (int)maxy;
    const Vertex *a = v[0], *b = v[1], *c = v[2];
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (area == 0) return;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            float w0 = (b->x - fx) * (c->y - fy) - (b->y - fy) * (c->x - fx);
            float w1 = (c->x - fx) * (a->y - fy) - (c->y - fy) * (a->x - fx);
            float w2 = (a->x - fx) * (b->y - fy) - (a->y - fy) * (b->x - fx);
            if (!((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))) continue;
            uint8_t *s = stencil + y * W + x;
            if (*s != ref) continue;
            if (op == STENCIL_INCR) { if (*s < 255) (*s)++; continue; }
            if (op == STENCIL_DECR) { if (*s > 0) (*s)--; continue; }
            if (!bm || !bm->rgba) continue;
            float u = (w0 * uv[0][0] + w1 * uv[1][0] + w2 * uv[2][0]) / area;
            float t = (w0 * uv[0][1] + w1 * uv[1][1] + w2 * uv[2][1]) / area;
            int tx = (int)floorf(u * bm->w), ty = (int)floorf(t * bm->h);
            if (bf->clip) { tx = tx < 0 ? 0 : tx >= bm->w ? bm->w - 1 : tx; ty = ty < 0 ? 0 : ty >= bm->h ? bm->h - 1 : ty; }
            else { tx = ((tx % bm->w) + bm->w) % bm->w; ty = ((ty % bm->h) + bm->h) % bm->h; }
            const uint8_t *texel = bm->rgba + ((size_t)ty * bm->w + tx) * 4;
            float al = texel[3] / 255.0f * tint[3];
            uint8_t *p = px + (y * W + x) * 3;
            for (int k = 0; k < 3; k++) p[k] = (uint8_t)(p[k] * (1 - al) + texel[k] * tint[k] * al);
        }
}

static void clear(const uint8_t rgb[3]) {
    for (int i = 0; i < W * H * 3; i += 3) { px[i] = rgb[0]; px[i + 1] = rgb[1]; px[i + 2] = rgb[2]; }
    memset(stencil, 0, (size_t)W * H);
}

static void rasterise(const DrawList *vb) {
    for (int c = 0; c < vb->ncmd; c++) {
        const DrawCmd *cmd = &vb->cmd[c];
        for (int k = cmd->start; k < cmd->start + cmd->count; k++) {
            const DrawItem *it = &vb->items[k];
            for (int i = 0; i + 2 < it->mesh->nverts; i += 3) {
                Vertex a = render_item_vertex(vb, it, i), b = render_item_vertex(vb, it, i + 1),
                       cc = render_item_vertex(vb, it, i + 2);
                if (it->bf) {
                    const Vertex *v[3] = {&a, &b, &cc};
                    float uv[3][2];
                    for (int j = 0; j < 3; j++) render_item_uv(it, i + j, uv[j]);
                    tri_tex(v, uv, it->bf, it->tint, cmd->ref, cmd->op);
                } else {
                    tri(&a, &b, &cc, cmd->ref, cmd->op);
                }
            }
        }
    }
    for (int i = 0; i < W * H; i++)
        if (stencil[i]) { fprintf(stderr, "stencil not restored at pixel %d\n", i); break; }
}

static void write_frame(const char *prefix, int t) {
    char fn[512];
    snprintf(fn, sizeof fn, "%s_%03d.ppm", prefix, t);
    FILE *f = fopen(fn, "wb");
    fprintf(f, "P6 %d %d 255\n", W, H);
    fwrite(px, 1, (size_t)W * H * 3, f);
    fclose(f);
    printf("frame %d: %d triangles in %d draws, %d masks, %d shapes culled, %d tris recoloured on CPU, %d from cache\n",
           t, render_stats.triangles, render_stats.draws, render_stats.masks, render_stats.culled, render_stats.cx_tris,
           render_stats.cached_tris);
}

static void line(float x0, float y0, float x1, float y1, const uint8_t rgb[3]) {
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
    for (int i = 0; i <= n; i++) {
        int x = (int)(x0 + dx * i / n), y = (int)(y0 + dy * i / n);
        for (int oy = 0; oy < 2; oy++)
            for (int ox = 0; ox < 2; ox++)
                if (x + ox >= 0 && x + ox < W && y + oy >= 0 && y + oy < H) memcpy(px + ((y + oy) * W + x + ox) * 3, rgb, 3);
    }
}

static void *host_alloc(size_t bytes, void *ctx) {
    (void)ctx;
    return malloc(bytes);
}

static int level_mode(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s --level LEVEL.swf PLAYER.swf out_prefix [frames]\n", argv[0]); return 1; }
    int frames = argc > 5 ? atoi(argv[5]) : 4;
    char err[256];
    clock_t t0 = clock();
    Level *lv = level_load(argv[2], err, sizeof err);
    if (!lv) { fprintf(stderr, "%s\n", err); return 1; }
    Movie *pl = swf_load(argv[3], err, sizeof err);
    if (!pl) { fprintf(stderr, "%s\n", err); return 1; }
    // same upload path as the Vita, so only colour-transformed shapes get recoloured
    render_tessellate_all(lv->m);
    render_tessellate_all(pl);
    void *lv_block = render_upload(lv->m, host_alloc, NULL), *pl_block = render_upload(pl, host_alloc, NULL);
    printf("loaded in %.2fs: %d layers, spawn %.0f,%.0f, edges %.0f..%.0f, %d markers hidden, bg %02x%02x%02x\n",
           (double)(clock() - t0) / CLOCKS_PER_SEC, lv->nlayers, lv->spawn_x, lv->spawn_y, lv->edge_left,
           lv->edge_right, lv->hidden, lv->m->bg[0], lv->m->bg[1], lv->m->bg[2]);
    for (int i = 0; i < lv->nlayers; i++)
        printf("  layer %s/%s factor %.2f\n", lv->m->names[lv->layer[i].container->name],
               lv->m->names[lv->layer[i].inner->name], lv->layer[i].factor);
    int walk = sprite_find_label(&pl->root, "walk");
    if (walk < 0) walk = 0;
    W = SCREEN_W; H = SCREEN_H;
    float k = W / GAME_W;
    Mat screen = {k / 20, 0, 0, k / 20, 0, 0};
    DrawList vb = {0};
    RecolourCache cache;
    const size_t cache_bytes = 8 * 1024 * 1024;
    void *cache_mem = malloc(cache_bytes);
    recolour_cache_init(&cache, cache_mem, cache_bytes);
    vb.cache = &cache;
    vb.view[2] = W;
    vb.view[3] = H;
    int types[8] = {0}, type_ids[8] = {1, 2, 3, 4, 100, 101, 300, 400};
    for (int i = 0; i < lv->nsegs; i++)
        for (int k2 = 0; k2 < 8; k2++) types[k2] += lv->segs[i].type == type_ids[k2];
    printf("%d play-layer objects depth-sorted, %d static\n", lv->nsorted, lv->game_static.count);
    printf("%d collision segments: type1 %d, 2 %d, 3 %d, 4 %d, 100 %d, 101 %d, 300 %d, 400 %d\n", lv->nsegs, types[0],
           types[1], types[2], types[3], types[4], types[5], types[6], types[7]);
    // The knight walks right with collision (5 px steps, like holding right); one picture per 30 steps.
    // Collision edges are drawn on top: red walls, blue stairs (100/101), green other types.
    // optional: start x y and step size (negative walks left)
    Mover km = {argc > 7 ? atof(argv[6]) : lv->spawn_x, argc > 7 ? atof(argv[7]) : lv->spawn_y, 0, 0};
    float step_dx = argc > 8 ? atof(argv[8]) : 5, step_dy = argc > 9 ? atof(argv[9]) : 0;
    for (int i = 0; i < lv->nsegs; i++)
        if (lv->segs[i].type == 100 || lv->segs[i].type == 101)
            printf("  stair edge %d type %d: %.0f,%.0f - %.0f,%.0f\n", i, lv->segs[i].type, lv->segs[i].x0,
                   lv->segs[i].y0, lv->segs[i].x1, lv->segs[i].y1);
    float kx, ky;
    for (int t = 0; t < frames; t++) {
        for (int step = 0; t && step < 30; step++) {
            level_move(lv, &km, step_dx, argc > 9 ? step_dy : 0);
            if (argc <= 7) level_move(lv, &km, 0, step & 1 ? 3 : -3);   // wiggle up/down to probe the floor edges
        }
        kx = km.x;
        ky = km.y;
        clear(lv->m->bg);
        render_begin(&vb);
        Actor knight = {pl, &pl->root.frames[walk], kx, ky, step_dx < 0 ? -1.0f : 1.0f, t, km.body_y};
        float camx = level_clamp_camera_x(lv, kx);
        level_render(lv, screen, camx, ky, 100, t, &knight, 1, &vb);
        rasterise(&vb);
        Mat gm = level_game_matrix(lv, screen, camx, ky, 100);
        for (int i = 0; i < lv->nsegs; i++) {
            const Segment *s = &lv->segs[i];
            static const uint8_t red[3] = {255, 0, 0}, blue[3] = {0, 80, 255}, green[3] = {0, 200, 0};
            const uint8_t *c = s->type <= 4 ? red : s->type < 200 ? blue : green;
            line(gm.a * s->x0 * 20 + gm.c * s->y0 * 20 + gm.tx, gm.b * s->x0 * 20 + gm.d * s->y0 * 20 + gm.ty,
                 gm.a * s->x1 * 20 + gm.c * s->y1 * 20 + gm.tx, gm.b * s->x1 * 20 + gm.d * s->y1 * 20 + gm.ty, c);
        }
        write_frame(argv[4], t);
        printf("  knight at %.0f,%.0f body_y %.0f ground %d\n", kx, ky, km.body_y, km.ground);
    }
    level_free(lv);
    swf_free(pl);
    free(lv_block);
    free(pl_block);
    recolour_cache_free(&cache);
    free(cache_mem);
    return 0;
}

// Scripted knight test: walk, light combo, jump + kick, heavy. Writes a frame at each snapshot.
static int player_mode(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s --player LEVEL.swf PLAYER.swf out_prefix\n", argv[0]); return 1; }
    char err[256];
    Level *lv = level_load(argv[2], err, sizeof err);
    Movie *pl = swf_load(argv[3], err, sizeof err);
    if (!lv || !pl) { fprintf(stderr, "%s\n", err); return 1; }
    static Rig rig;
    rig_init(&rig, pl);
    Player p;
    player_init(&p, &rig, lv->spawn_x, lv->spawn_y);
    for (int i = 0; i < A_COUNT; i++) printf("%s %d/%d  ", anim_name(i), rig.anims[i].fin, rig.anims[i].length);
    printf("\n");
    Enemies es;
    enemies_reset(&es);
    int fight = argc > 5 && !strcmp(argv[5], "fight");
    W = SCREEN_W; H = SCREEN_H;
    float k = W / GAME_W;
    Mat screen = {k / 20, 0, 0, k / 20, 0, 0};
    DrawList vb = {0};
    vb.view[2] = W;
    vb.view[3] = H;
    static const char *names[] = {"idle", "walk", "dash", "attack", "jump", "land", "block", "hit"};
    // 60 Hz frames; a script of presses: X = light, Y = heavy, A = jump, R/L = tap right/left
    static const struct { int f; char key; } script[] = {
        {10, 'X'}, {20, 'X'}, {30, 'X'}, {40, 'X'},             // X X X X
        {120, 'X'}, {130, 'Y'},                                  // X Y: uppercut
        {200, 'X'}, {210, 'X'}, {220, 'Y'}, {232, 'Y'},          // X X Y Y
        {320, 'A'}, {330, 'X'}, {338, 'X'}, {346, 'Y'},          // air X X Y
        {420, 'A'}, {432, 'Y'},                                  // air Y: slam down
        {500, 'R'}, {506, 'R'}, {516, 'X'},                      // dash, X: dash punch
    };
    const char *last = "";
    int shot = 0;
    for (int f = 0; f < (fight ? 900 : 580); f++) {
        PlayerInput in = {0};
        char key = 0;
        if (fight) {
            // face the nearest enemy, walk up to it and mash X X Y (or X Y to launch, then air X)
            const Enemy *t = NULL;
            for (int i = 0; i < MAX_ENEMIES; i++)
                if (es.e[i].alive && es.e[i].hp > 0 && (!t || fabsf(es.e[i].mv.x - p.mv.x) < fabsf(t->mv.x - p.mv.x)))
                    t = &es.e[i];
            if (t) {
                float dx = t->mv.x - p.mv.x, dy = t->mv.y - p.mv.y;
                if (fabsf(dx) > 55) in.dx = dx > 0 ? 1 : -1;
                else if (dx * p.face < 0) in.dx = dx > 0 ? 0.6f : -0.6f;
                if (fabsf(dy) > 8) in.dy = dy > 0 ? 1 : -1;
                // X Y to launch, then jump and X X X to juggle
                if (fabsf(dx) < 80 && fabsf(dy) < 15 && f % 8 == 0) {
                    int step = (f / 8) % 6;
                    key = step == 0 ? 'X' : step == 1 ? 'Y' : step == 2 ? 'A' : 'X';
                }
            }
        }
        for (size_t s = 0; !fight && s < sizeof script / sizeof *script; s++)
            if (script[s].f == f) key = script[s].key;
        if (key == 'X') in.light = 1;
        if (key == 'Y') in.heavy = 1;
        if (key == 'A') in.jump = 1;
        // a tap holds the direction for 2 frames; the second tap is held until the dash punch
        if (!fight && ((f >= 500 && f < 502) || (f >= 506 && f < 520))) in.dx = 1;
        player_update(&p, lv, &in, 0.5f);
        if (fight) enemies_update(&es, lv, &rig, &p, 0.5f);
        const char *now = player_move_name(&p);
        int snap = fight ? f % 45 == 0 : strcmp(now, last) && p.state == P_ATTACK;
        if (!fight && strcmp(now, last))
            printf("f%3d %-6s %-10s group %d num %d body_y %.0f x %.0f\n", f, names[p.state], now, p.punch_group,
                   p.punch_num, p.mv.body_y, p.mv.x);
        if (fight)
            for (int i = 0; i < MAX_ENEMIES; i++) {
                static int last_anim[MAX_ENEMIES];
                const Enemy *e = &es.e[i];
                if (e->alive && e->anim != last_anim[i] && (e->anim == A_BOUNCE1 || e->anim == A_BOUNCE2 ||
                                                             e->anim == A_JUGGLE || e->anim == A_HITGROUND))
                    printf("   f%3d enemy %d -> %-10s toss_y %.0f weight %.0f bounces %d hp %d\n", f, i,
                           anim_name(e->anim), e->speed_toss_y, e->weight, e->bounces, e->hp);
                last_anim[i] = e->anim;
            }
        if (fight && f % 30 == 0) {
            printf("f%3d knight %-6s %-10s hp %3d x %.0f | wave %d kills %d |", f, names[p.state], now, p.hp, p.mv.x,
                   es.wave, es.kills);
            for (int i = 0; i < MAX_ENEMIES; i++)
                if (es.e[i].alive)
                    printf(" [%s hp%d st%d x%.0f by%.0f]", anim_name(es.e[i].anim), es.e[i].hp, es.e[i].state,
                           es.e[i].mv.x, es.e[i].mv.body_y);
            printf("\n");
        }
        last = now;
        if (!snap) continue;
        clear(lv->m->bg);
        render_begin(&vb);
        Actor actors[1 + MAX_ENEMIES];
        actors[0] = player_actor(&p);
        int na = 1 + enemies_actors(&es, &rig, actors + 1, MAX_ENEMIES);
        level_render(lv, screen, level_clamp_camera_x(lv, p.mv.x), p.mv.y, 100, f / 2, actors, na, &vb);
        rasterise(&vb);
        write_frame(argv[4], shot++);
    }
    return 0;
}

// Every frame of one clip in a grid (to see which frame is which character part).
static int sprite_mode(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s --sprite file.swf cid out_prefix [cell_px]\n", argv[0]); return 1; }
    char err[256];
    Movie *m = swf_load(argv[2], err, sizeof err);
    if (!m) { fprintf(stderr, "%s\n", err); return 1; }
    int cid = atoi(argv[3]), cell = argc > 5 ? atoi(argv[5]) : 80;
    if (m->chars[cid].type != CHAR_SPRITE) { fprintf(stderr, "not a sprite\n"); return 1; }
    const Sprite *s = m->chars[cid].ptr;
    W = SCREEN_W; H = SCREEN_H;
    static const uint8_t paper[3] = {237, 237, 224};
    clear(paper);
    DrawList vb = {0};
    int cols = W / cell;
    for (int f = 0; f < s->nframes && (f / cols + 1) * cell <= H; f++) {
        render_begin(&vb);
        float k = cell / 200.0f / 20;   // ~200 px of art per cell
        Mat mt = {k, 0, 0, k, (f % cols) * cell + cell / 2.0f, (f / cols) * cell + cell * 0.6f};
        render_display_list(m, &s->frames[f], mt, CX_IDENTITY, 0, &vb);
        rasterise(&vb);
    }
    write_frame(argv[4], 0);
    printf("%d frames\n", s->nframes);
    return 0;
}

// CC_PS_BUTTONS: "dark", "coloured" (the game's balls), or anything else for grey, as the Vita
static int btn_style(void) {
    const char *v = getenv("CC_PS_BUTTONS");
    return !strcmp(v, "dark") ? CC_BTN_STYLE_DARK : !strcmp(v, "coloured") ? CC_BTN_STYLE_COLOURED : CC_BTN_STYLE_GREY;
}

// Several clips' first frames in a row: --clips file.swf cid,cid,... out_prefix [cell_px] [art_px]
static int clips_mode(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s --clips file.swf cid,cid,... out_prefix [cell_px] [art_px]\n", argv[0]); return 1; }
    char err[256];
    Movie *m = swf_load(argv[2], err, sizeof err);
    if (!m) { fprintf(stderr, "%s\n", err); return 1; }
    int cell = argc > 5 ? atoi(argv[5]) : 150;
    float art = argc > 6 ? (float)atof(argv[6]) : 60;
    if (getenv("CC_PS_BUTTONS"))   // "coloured" or "dark"
        printf("PlayStation buttons: %d clips\n",
               cc_buttons_playstation(m, btn_style()));
    W = SCREEN_W; H = cell;
    static const uint8_t paper[3] = {60, 60, 70};
    clear(paper);
    DrawList vb = {0};
    char list[256];
    snprintf(list, sizeof list, "%s", argv[3]);
    int i = 0;
    for (char *t = strtok(list, ","); t; t = strtok(NULL, ","), i++) {
        int cid = atoi(t);
        render_begin(&vb);
        float b[4];
        Character *c = &m->chars[cid];
        if (c->type != CHAR_SPRITE) { printf("%d: not a sprite\n", cid); continue; }
        // fit the clip's first frame into art pixels
        Mat id = {1, 0, 0, 1, 0, 0};
        render_begin(&vb);
        render_char(m, cid, id, CX_IDENTITY, 0, &vb);
        b[0] = b[1] = 1e30f; b[2] = b[3] = -1e30f;
        for (int k = 0; k < vb.n; k++) {
            const float *mb = vb.items[k].mesh->bounds;
            for (int q = 0; q < 4; q++) {
                float x = mb[q & 1 ? 2 : 0], y = mb[q & 2 ? 3 : 1];
                Mat mm = vb.items[k].m;
                float px = mm.a * x + mm.c * y + mm.tx, py = mm.b * x + mm.d * y + mm.ty;
                if (px < b[0]) b[0] = px; if (px > b[2]) b[2] = px;
                if (py < b[1]) b[1] = py; if (py > b[3]) b[3] = py;
            }
        }
        float w = b[2] - b[0], h = b[3] - b[1], s = art / (w > h ? w : h);
        Mat mt = {s, 0, 0, s, i * cell + cell / 2.0f - (b[0] + w / 2) * s, cell / 2.0f - (b[1] + h / 2) * s};
        render_begin(&vb);
        render_char(m, cid, mt, CX_IDENTITY, 0, &vb);
        rasterise(&vb);
        printf("%d: %.0f x %.0f twips\n", cid, w, h);
    }
    write_frame(argv[4], 0);
    return 0;
}

// One character dressed as `who` in every root animation (to spot undressed parts).
static int dress_mode(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: %s --dress PLAYER.swf who weapon out_prefix\n", argv[0]); return 1; }
    char err[256];
    Movie *m = swf_load(argv[2], err, sizeof err);
    if (!m) { fprintf(stderr, "%s\n", err); return 1; }
    static Rig rig;
    rig_init(&rig, m);
    int who = atoi(argv[3]), weapon = atoi(argv[4]);
    W = SCREEN_W; H = SCREEN_H;
    static const uint8_t paper[3] = {237, 237, 224};
    clear(paper);
    DrawList vb = {0};
    int cell = 80, cols = W / cell;
    for (int l = 0; l < m->root.nlabels && (l / cols + 1) * cell <= H; l++) {
        render_begin(&vb);
        Mover mv = {0, 0, 0, 0};
        // borrow the rig_actor dressing for this label's frame
        Actor a = rig_actor(&rig, A_STAND, 4, &mv, 1, who, weapon);
        a.frame = &m->root.frames[m->root.labels[l].frame];
        vb.nparts = a.nparts;
        for (int i = 0; i < a.nparts; i++) { vb.part_count[i] = a.part_count[i]; vb.part_frame[i] = a.part_frame[i]; }
        float k = 0.35f / 20;
        Mat mt = {k, 0, 0, k, (l % cols) * cell + cell / 2.0f, (l / cols) * cell + cell * 0.8f};
        render_display_list(m, a.frame, mt, CX_IDENTITY, 4, &vb);
        rasterise(&vb);
    }
    write_frame(argv[5], 0);
    return 0;
}

// ---- the ActionScript engine on the real game files ----

static char game_dir[512];

// "../player/player.swf" -> GAME/game/PLAYER.swf (or GAME/levels/...), any case.
// Parsed files are kept: the game loads player.swf into every player slot and
// fx.swf into every effect, like the original executable's cache.
static struct { char name[64]; Movie *m; } movie_cache[128];
static int nmovie_cache;
static int load_count;

static Movie *host_loader(AsVm *vm, const char *url) {
    (void)vm;
    const char *base = strrchr(url, '/');
    base = base ? base + 1 : url;
    load_count++;
    for (int i = 0; i < nmovie_cache; i++)
        if (!strcasecmp(movie_cache[i].name, base)) return movie_cache[i].m;
    static const char *dirs[] = {"game", "levels"};
    for (int i = 0; i < 2; i++) {
        char dirpath[600];
        snprintf(dirpath, sizeof dirpath, "%s/%s", game_dir, dirs[i]);
        DIR *d = opendir(dirpath);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d)))
            if (!strcasecmp(de->d_name, base)) {
                char path[1200], err[256];
                snprintf(path, sizeof path, "%s/%s", dirpath, de->d_name);
                closedir(d);
                Movie *m = swf_load(path, err, sizeof err);
                if (m) m->is_level = i == 1;
                int fixed = m ? cc_repair_level(m) : 0;
    if (m && getenv("CC_DUMP_SHAPE") && !strcmp(base, "MENU.swf")) {
        extern void cc_dump_shape(Movie *m, int cid);
        cc_dump_shape(m, atoi(getenv("CC_DUMP_SHAPE")));
    }
    if (m && getenv("CC_PS_BUTTONS")) cc_menu_vita_badge(m);
    if (m && getenv("CC_PS_BUTTONS"))   // as the Vita does for every file it loads
        cc_buttons_playstation(m, btn_style());
                printf("  [load] %s%s", base, m ? "" : " FAILED");
                if (fixed) printf(" (put back %d layer%s the file lost)", fixed, fixed > 1 ? "s" : "");
                printf("\n");
                if (m && nmovie_cache < 128) {
                    snprintf(movie_cache[nmovie_cache].name, sizeof movie_cache[0].name, "%s", base);
                    movie_cache[nmovie_cache++].m = m;
                }
                return m;
            }
        closedir(d);
    }
    printf("  [load] %s -> not found\n", url);
    return NULL;
}

#define MAX_WARN 512
static char *warn_text[MAX_WARN];
static int warn_count[MAX_WARN], nwarn;

static void host_warn(AsVm *vm, const char *msg) {
    (void)vm;
    for (int i = 0; i < nwarn; i++)
        if (!strcmp(warn_text[i], msg)) { warn_count[i]++; return; }
    if (nwarn < MAX_WARN) { warn_text[nwarn] = strdup(msg); warn_count[nwarn++] = 1; }
}

static void host_trace(AsVm *vm, const char *msg) {
    (void)vm;
    printf("  [trace] %s\n", msg);
}

static void host_music(const char *name, int loop) { printf("  [music] %s%s\n", name, loop ? " (loop)" : ""); }
static void host_music_stop(void) { printf("  [music] stop\n"); }
static void host_sound(AsVm *vm, const char *linkage, float vol, float pan, int loops, const void *owner) {
    (void)vm; (void)owner;
    if (getenv("AS_SOUNDS")) printf("  [sound] %s vol %.2f pan %.2f loops %d\n", linkage, vol, pan, loops);
}

// CC_PREFETCH=N: each file's first loadMovie waits N frames, as the Vita's background loads do
static int fake_prefetch(AsVm *vm, const char *url) {
    (void)vm;
    static char done[256][128];
    static int ndone, waited;
    static char cur[128];
    for (int i = 0; i < ndone; i++) if (!strcasecmp(done[i], url)) return 0;
    if (strcasecmp(cur, url)) { snprintf(cur, sizeof cur, "%s", url); waited = 0; }
    if (++waited <= atoi(getenv("CC_PREFETCH"))) return 1;
    if (ndone < 256) snprintf(done[ndone++], sizeof done[0], "%s", url);
    cur[0] = 0;
    return 0;
}

static int as_mode(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s --as GAME_DIR START.swf out_prefix [frames] [snap_every]\n", argv[0]); return 1; }
    snprintf(game_dir, sizeof game_dir, "%s", argv[2]);
    int frames = argc > 5 ? atoi(argv[5]) : 60, every = argc > 6 ? atoi(argv[6]) : 30;
    Movie *start = host_loader(NULL, argv[3]);
    if (!start) return 1;
    printf("root: %d frames, %d scripts:", start->root.nframes, start->root.nactions);
    for (int i = 0; i < start->root.nactions; i++)
        printf(" [frame %d @%u len %u]", start->root.actions[i].frame + 1, start->root.actions[i].off,
               start->root.actions[i].len);
    printf("\n");
    AsVm *vm = as_new(host_loader);
    vm->warn = host_warn;
    vm->trace = host_trace;
    if (getenv("CC_PREFETCH")) vm->prefetch = fake_prefetch;
    extern void cc_host_init(AsVm *vm);
    cc_host_init(vm);
    // CC_SAVE=path: start from a save file (cc_host.c's "CCS2" format), like the Vita's save.dat
    if (getenv("CC_SAVE") && !cc_storage_load(getenv("CC_SAVE")))
        printf("  couldn't read save %s\n", getenv("CC_SAVE"));
    as_render_init(vm);
    if (getenv("CC_STRINGS")) {   // strings_en.txt from extract_strings.py
        extern int strtab_load(const char *path);
        printf("strings: %d loaded\n", strtab_load(getenv("CC_STRINGS")));
    }
    cc_music_play = host_music;
    cc_music_stop = host_music_stop;
    vm->sound_start = host_sound;
    if (getenv("CC_FONT_DIR")) {   // the PC copy's data/fonts
        char fnt[600], dds[600], err[256];
        snprintf(fnt, sizeof fnt, "%s/synjUnicode_20.fnt", getenv("CC_FONT_DIR"));
        snprintf(dds, sizeof dds, "%s/synjUnicode_20_0.dds", getenv("CC_FONT_DIR"));
        HostFont *hf = hostfont_load(fnt, dds, err, sizeof err);
        if (hf) {
            printf("font: size %d, line %d, base %d, atlas %dx%d\n", hf->size, hf->line_height, hf->base, hf->atlas.w, hf->atlas.h);
            as_render_set_font(hf);
        } else {
            printf("font: %s\n", err);
        }
    }
    vm->root = as_mc_new(vm, start, &start->root, NULL, 0);
    if (!strcasecmp(argv[3], "MAIN.swf") && !getenv("CC_NO_LOADING")) cc_loading_attach(vm);
    W = SCREEN_W; H = SCREEN_H;
    if (getenv("CC_SCALE") && atoi(getenv("CC_SCALE")) == 2) { W = MAX_W; H = MAX_H; }
    DrawList vb = {0};
    vb.view[2] = W;
    vb.view[3] = H;
    float k = W / (float)start->stage_w;
    Mat screen = {k / 20, 0, 0, k / 20, 0, 0};
    int shot = 0;
    // CC_KEYS="frame:code[:hold],...": scripted key presses (codes from f_GetButtons; port 1:
    // accept 2, back 3, start 49, d-pad 37-40, punch1 65, punch2 87, jump 83, magic 69)
    const char *keys = getenv("CC_KEYS");
    for (int f = 0; f < frames; f++) {
        if (getenv("CC_RESTART_AT") && f == atoi(getenv("CC_RESTART_AT"))) {   // the Vita's Exit to Main Menu
            as_free(vm);
            Movie *first = start;
            if (getenv("CC_RESTART_MENU")) {   // as the Vita does: every file but MAIN goes, then the title
                int n = 0;
                for (int i = 0; i < nmovie_cache; i++) {
                    if (movie_cache[i].m == start) { movie_cache[n++] = movie_cache[i]; continue; }
                    printf("  [free] %s\n", movie_cache[i].name);
                    as_forget_movie(movie_cache[i].m);
                    swf_free(movie_cache[i].m);
                }
                nmovie_cache = n;
                first = host_loader(NULL, "MENU.swf");
            }
            vm = as_new(host_loader);
            vm->warn = host_warn;
            vm->trace = host_trace;
            vm->sound_start = host_sound;
            cc_host_init(vm);
            as_render_init(vm);
            vm->root = as_mc_new(vm, first, &first->root, NULL, 0);
            if (first != start) cc_native_root_functions(vm, &vm->root->obj);
            else if (!strcasecmp(argv[3], "MAIN.swf") && !getenv("CC_NO_LOADING")) cc_loading_attach(vm);
            render_begin(&vb);
            printf("  [restart at %d]\n", f);
        }
        memset(cc_keys, 0, sizeof cc_keys);
        for (const char *p = keys; p && *p;) {
            int kf = 0, code = 0, hold = 2;
            int n = sscanf(p, "%d:%d:%d", &kf, &code, &hold);
            if (n >= 2 && f >= kf && f < kf + (n == 3 ? hold : 2) && code > 0 && code < 256) cc_keys[code] = 1;
            p = strchr(p, ',');
            if (p) p++;
        }
        // CC_SET="frame:object_path.name=number,...": set a script variable (e.g. move a player)
        for (const char *p = getenv("CC_SET"); p && *p;) {
            char lhs[160] = "";
            double val = 0;
            int sf = -1;
            if (sscanf(p, "%d:%159[^=]=%lf", &sf, lhs, &val) == 3 && sf == f) {
                char *dot = strrchr(lhs, '.');
                if (dot) {
                    *dot = 0;
                    int found;
                    AsVal o = as_resolve_path(vm, vm->root, lhs, &found);
                    if (found) as_set(vm, o, as_atom(vm, dot + 1), as_num(val));
                    printf("  [set] %s.%s = %g at frame %d%s\n", lhs, dot + 1, val, f, found ? "" : " (no such object)");
                }
            }
            p = strchr(p, ',');
            if (p) p++;
        }
        // CC_CALL="frame:function_path[:arg_path],...": call a script function (e.g. a portal's exit)
        for (const char *p = getenv("CC_CALL"); p && *p;) {
            char fn[128] = "", arg[128] = "";
            int cf = -1;
            sscanf(p, "%d:%127[^:,]:%127[^,]", &cf, fn, arg);
            if (cf == f) {
                int found;
                AsVal fv = as_resolve_path(vm, vm->root, fn, &found);
                // arguments separated by |: 'text is a string, #12.5 a number, anything else a path
                AsVal av[8];
                int nav = 0;
                for (char *a = strtok(arg, "|"); a && nav < 8; a = strtok(NULL, "|"))
                    av[nav++] = a[0] == '\'' ? as_strv(as_str(vm, a + 1))
                              : a[0] == '#' ? as_num(atof(a + 1)) : as_resolve_path(vm, vm->root, a, &found);
                printf("  [call] %s (%d args) at frame %d%s\n", fn, nav, f, fv.t == V_OBJ ? "" : " - not a function");
                // a.b.method: called on a.b (clip methods such as gotoAndStop need their clip)
                AsVal self = as_objv(&vm->root->obj);
                char *dot = strrchr(fn, '.');
                if (dot) {
                    *dot = 0;
                    AsVal o = as_resolve_path(vm, vm->root, fn, &found);
                    if (found && o.t == V_OBJ) self = o;
                }
                if (fv.t == V_OBJ) as_call(vm, fv, self, nav, av);
            }
            p = strchr(p, ',');
            if (p) p++;
        }
        static int prof_from = -1;
        static double prof_ms, prof_max;
        static long long prof_ops;
        if (getenv("AS_PROF") && f == atoi(getenv("AS_PROF"))) { as_prof_enable(1); prof_from = f; }
        // CC_SAVE_AT=frame: read CC_SAVE again then (after a scripted jump wrote over it)
        if (getenv("CC_SAVE_AT") && getenv("CC_SAVE") && f == atoi(getenv("CC_SAVE_AT")))
            cc_storage_load(getenv("CC_SAVE"));
        extern double as_time_ms(void);
        double t0 = as_time_ms();
        long long ops0 = vm->ops;
        as_tick(vm);
        double tg = as_time_ms();
        size_t heap0 = vm->heap_count;
        as_gc(vm);
        // CC_FREE_UNUSED=1: free the files nothing uses, as the Vita build does every 90 ticks
        if (getenv("CC_FREE_UNUSED") && f % 90 == 89) {
            Movie *ms[128];
            uint8_t used[128];
            for (int i = 0; i < nmovie_cache; i++) ms[i] = movie_cache[i].m;
            if (as_movies_used(vm, ms, nmovie_cache, used)) {
                int n = 0;
                for (int i = 0; i < nmovie_cache; i++) {
                    if (used[i] || ms[i] == start) { movie_cache[n++] = movie_cache[i]; continue; }
                    printf("  [free] %s\n", movie_cache[i].name);
                    as_forget_movie(ms[i]);
                    swf_free(ms[i]);
                }
                nmovie_cache = n;
            }
        }
        double gms = as_time_ms() - tg;
        double ms = as_time_ms() - t0;
        if (getenv("AS_SLOW") && ms > atof(getenv("AS_SLOW")))
            printf("  [slow] tick %d: %.2f ms (gc %.2f), %lld actions\n", f, ms, gms, vm->ops - ops0);
        static double gc_total, gc_worst;
        static int gc_runs;
        if (prof_from >= 0 && vm->heap_count != heap0) { gc_total += gms; gc_runs++; if (gms > gc_worst) gc_worst = gms; }
        if (prof_from >= 0 && f == frames - 1)
            printf("gc: %d collections in %d ticks, %.3f ms each on average, worst %.3f, live after %u\n", gc_runs,
                   f - prof_from + 1, gc_runs ? gc_total / gc_runs : 0, gc_worst, (unsigned)vm->heap_count);
        if (getenv("AS_TRACE_EVERY") && getenv("AS_PRINT") && f % atoi(getenv("AS_TRACE_EVERY")) == 0) {
            char buf[1024];   // AS_PRINT's values every N frames, on one line
            snprintf(buf, sizeof buf, "%s", getenv("AS_PRINT"));
            printf("  [trace %d]", f);
            for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
                int found;
                AsVal v = as_resolve_path(vm, vm->root, t, &found);
                printf(" %s", found ? as_tostr(vm, v)->s : "-");
            }
            printf("\n");
        }
        if (prof_from >= 0) {
            prof_ms += ms;
            prof_ops += vm->ops - ops0;
            if (ms > prof_max) prof_max = ms;
            if (f == frames - 1)
                printf("ticks %d..%d: average %.2f ms (worst %.2f), %lld actions a tick\n", prof_from, f,
                       prof_ms / (f - prof_from + 1), prof_max, prof_ops / (f - prof_from + 1));
        }
        // CC_SNAP_FROM=frame: no snapshots before that frame
        if ((f % every == 0 && (!getenv("CC_SNAP_FROM") || f >= atoi(getenv("CC_SNAP_FROM")))) || f == frames - 1) {
            static AsMc *panel;
            if (getenv("CC_MENU") && f == atoi(getenv("CC_MENU"))) {   // CC_MENU=frame: the Vita's title menu
                if (!panel) {
                    cc_native_root_functions(vm, &vm->root->obj);
                    panel = cc_menu_attach(vm, vm->root->movie->stage_w * 0.226f, vm->root->movie->stage_h * 0.635f);
                    if (panel) cc_menu_highlight(vm, panel, getenv("CC_MENU_ITEM") ? atoi(getenv("CC_MENU_ITEM")) : 0);
                }
            }
            if (getenv("AS_HIDE_SHAPES")) {   // AS_HIDE_SHAPES="cid,cid": hide shapes placed on timelines
                void hide_shapes(AsMc *mc) {
                    for (int i = 0; i < mc->ndisp; i++) {
                        AsDisp *e = &mc->disp[i];
                        char list[256], key[16];
                        snprintf(list, sizeof list, ",%s,", getenv("AS_HIDE_SHAPES"));
                        snprintf(key, sizeof key, ",%d,", e->cid);
                        if (e->kind == DISP_SHAPE && strstr(list, key)) e->m.a = e->m.b = e->m.c = e->m.d = 0;
                        if (e->kind == DISP_MC && e->mc) hide_shapes(e->mc);
                    }
                }
                hide_shapes(vm->root);
            }
            if (getenv("AS_HIDE_CLIPS")) {   // AS_HIDE_CLIPS=1: hide every clip, so only shapes show
                for (int i = 0; i < vm->root->ndisp; i++)
                    if (vm->root->disp[i].kind == DISP_MC && vm->root->disp[i].mc) {
                        AsMc *mc = vm->root->disp[i].mc;
                        for (int j = 0; j < mc->ndisp; j++)
                            if (mc->disp[j].kind == DISP_MC && mc->disp[j].mc) mc->disp[j].mc->visible = 0;
                    }
            }
            if (getenv("AS_HIDE_CHAR")) {   // AS_HIDE_CHAR=n: hide every clip of sprite n (finding art)
                int cid = atoi(getenv("AS_HIDE_CHAR"));
                void hide(AsMc *mc) {
                    for (int i = 0; i < mc->ndisp; i++)
                        if (mc->disp[i].kind == DISP_MC && mc->disp[i].mc) {
                            if (mc->disp[i].cid == cid) mc->disp[i].mc->visible = 0;
                            hide(mc->disp[i].mc);
                        }
                }
                hide(vm->root);
            }
            if (getenv("CC_VIEW")) {   // CC_VIEW="x,y": snapshots with the level scrolled to put x,y top left
                float vx = 0, vy = 0;
                sscanf(getenv("CC_VIEW"), "%f,%f", &vx, &vy);
                int found;
                AsVal v = as_resolve_path(vm, vm->root, "loader.game.game", &found);
                if (v.t == V_OBJ && v.o->type == OBJ_MC) { ((AsMc *)v.o)->m.tx = -vx * 20; ((AsMc *)v.o)->m.ty = -vy * 20; }
            }
            if (getenv("AS_HIDE")) {   // AS_HIDE="path,path": hide clips in the snapshots (finding what draws what)
                char buf[1024];
                snprintf(buf, sizeof buf, "%s", getenv("AS_HIDE"));
                for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
                    int found;
                    AsVal v = as_resolve_path(vm, vm->root, t, &found);
                    if (v.t == V_OBJ && v.o->type == OBJ_MC) ((AsMc *)v.o)->visible = 0;
                }
            }
            uint8_t bgc[3] = {start->bg[0], start->bg[1], start->bg[2]};
            if (getenv("CC_BG")) sscanf(getenv("CC_BG"), "%hhu,%hhu,%hhu", &bgc[0], &bgc[1], &bgc[2]);   // "r,g,b"
            clear(bgc);
            render_begin(&vb);
            as_render_mc(vm->root, screen, CX_IDENTITY, &vb);
            if (vb.heat_at >= 0)   // the Vita draws a heat shimmer behind this
                printf("  [heat] items 0..%d shimmer in %.0f,%.0f - %.0f,%.0f\n", vb.heat_at, vb.heat[0], vb.heat[1],
                       vb.heat[2], vb.heat[3]);
            rasterise(&vb);
            if (panel) {   // the title's power button, drawn over the frame as the Vita does
                static CcVert t[4096];
                int n = cc_menu_power_button(panel, screen, t, 4096);
                for (int i = 0; i + 2 < n; i += 3) {
                    const CcVert *a = &t[i], *b = &t[i + 1], *c = &t[i + 2];
                    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
                    if (area == 0) continue;
                    int x0 = (int)fminf(a->x, fminf(b->x, c->x)), x1 = (int)fmaxf(a->x, fmaxf(b->x, c->x)) + 1;
                    int y0 = (int)fminf(a->y, fminf(b->y, c->y)), y1 = (int)fmaxf(a->y, fmaxf(b->y, c->y)) + 1;
                    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < H; y++)
                        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < W; x++) {
                            float px_ = x + 0.5f, py_ = y + 0.5f;
                            float w0 = ((b->x - px_) * (c->y - py_) - (b->y - py_) * (c->x - px_)) / area;
                            float w1 = ((c->x - px_) * (a->y - py_) - (c->y - py_) * (a->x - px_)) / area;
                            if (w0 < 0 || w1 < 0 || w0 + w1 > 1) continue;
                            uint8_t *d = px + (y * W + x) * 3;
                            for (int k = 0; k < 3; k++) d[k] = (a->rgba[k] * a->rgba[3] + d[k] * (255 - a->rgba[3])) / 255;
                        }
                }
                Mat hm;
                AsMc *hl = cc_menu_highlight_clip(panel, &hm);   // the chosen button's frame, over all that
                if (hl) {
                    DrawList hvb = {0};
                    render_begin(&hvb);
                    hvb.view[2] = W;
                    hvb.view[3] = H;
                    hl->visible = 1;
                    as_render_mc(hl, mat_mul(screen, mat_mul(vm->root->m, mat_mul(panel->m, hm))), CX_IDENTITY, &hvb);
                    hl->visible = 0;
                    rasterise(&hvb);
                }
            }
            write_frame(argv[4], shot++);
            printf("tick %d: %.1f ms, %lld actions, heap %zu objects, root frame %d\n", f, ms, vm->ops - ops0,
                   vm->heap_count, vm->root->frame + 1);
        }
    }
    if (getenv("AS_PRINT")) {   // AS_PRINT="main.n_state,loader.b_loaded": values of script variables
        char buf[1024];
        snprintf(buf, sizeof buf, "%s", getenv("AS_PRINT"));
        for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
            int found;
            AsVal v = as_resolve_path(vm, vm->root, t, &found);
            printf("  %s = %s%s\n", t, found ? as_tostr(vm, v)->s : "(not found)",
                   v.t == V_UNDEF ? " (undefined)" : "");
            if (v.t == V_OBJ && v.o->type == OBJ_FUNC)
                printf("    (function %s)\n", v.o->fname ? v.o->fname->s : "anonymous");
        }
    }
    if (getenv("AS_DUMP")) {   // AS_DUMP="p_game.p1": every variable of an object
        int found;
        AsVal v = as_resolve_path(vm, vm->root, getenv("AS_DUMP"), &found);
        if (v.t == V_OBJ) {
            AsProps *ps = &v.o->props;
            for (int i = 0; i < v.o->nitems; i++)   // an array's items
                printf("  .%d = %s\n", i, as_tostr(vm, v.o->items[i])->s);
            for (int i = 0; i < ps->cap; i++) {
                AsProp *p = &ps->slots[i];
                if (!p->key || p->key == (AsStr *)1) continue;
                AsVal x = p->v;
                printf("  .%s = %s%s\n", p->key->s,
                       x.t == V_OBJ && x.o->type == OBJ_FUNC ? (x.o->fname ? x.o->fname->s : "(function)")
                                                            : as_tostr(vm, x)->s,
                       x.t == V_OBJ && x.o->type == OBJ_FUNC ? "()" : "");
            }
        }
    }
    if (getenv("AS_TREE")) {
        int maxd = atoi(getenv("AS_TREE"));
        void dump(AsMc *mc, int d) {
            for (int i = 0; i < mc->ndisp; i++) {
                AsDisp *e = &mc->disp[i];
                if (e->kind == DISP_SHAPE) {   // AS_TREE_SHAPES: list shapes too
                    if (getenv("AS_TREE_SHAPES"))
                        printf("%*sshape [cid %d depth %d] x %.0f y %.0f\n", d * 2, "", e->cid, e->depth, e->m.tx / 20, e->m.ty / 20);
                    continue;
                }
                AsMc *c = e->mc;
                if (!c) continue;
                printf("%*s%s [cid %d depth %d%s] frame %d/%d x %.0f y %.0f%s%s%s\n", d * 2, "",
                       c->name ? c->name->s : "(unnamed)", e->cid, e->depth, c->text ? " text" : "", c->frame + 1,
                       c->def ? c->def->nframes : 0, c->m.tx / 20, c->m.ty / 20, c->visible ? "" : " hidden",
                       c->alpha < 100 ? " translucent" : "", c->events_len ? " events" : "");
                if (d < maxd) dump(c, d + 1);
            }
        }
        AsMc *top = vm->root;
        if (getenv("AS_TREE_AT")) {   // start the tree at a path
            int found;
            AsVal v = as_resolve_path(vm, vm->root, getenv("AS_TREE_AT"), &found);
            if (v.t == V_OBJ && v.o->type == OBJ_MC) top = (AsMc *)v.o;
            printf("%s: frame %d/%d%s\n", getenv("AS_TREE_AT"), top->frame + 1,
                   top->def ? top->def->nframes : 0, top->playing ? " playing" : "");
        }
        dump(top, 0);
    }
    if (getenv("CC_STORAGE_DUMP")) {   // the non-zero save slots of port 1
        double *s = cc_storage(0);
        int shown = 0;
        printf("storage port 0:");
        for (int a = 0; a < 65536 && shown < 120; a++)
            if (s[a] != 0) { printf(" [%d]=%g", a, s[a]); shown++; }
        printf("\n");
    }
    if (getenv("AS_PROF")) {
        void line(const char *s) { printf("  %s\n", s); }
        printf("\nprofile (from frame %s):\n", getenv("AS_PROF"));
        as_prof_report(40, line);
    }
    printf("\n%d loadMovie calls, %d files parsed\n", load_count, nmovie_cache);
    printf("\n%d distinct warnings:\n", nwarn);
    for (int i = 0; i < nwarn; i++) printf("  %6d x %s\n", warn_count[i], warn_text[i]);
    return 0;
}

// The pause menu's pages: --pause PAUSE.swf out_prefix [backdrop.ppm 960x544]
static int pause_mode(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s --pause PAUSE.swf out_prefix [backdrop.ppm]\n", argv[0]); return 1; }
    char err[256];
    Movie *m = swf_load(argv[2], err, sizeof err);
    if (!m) { fprintf(stderr, "%s\n", err); return 1; }
    cc_buttons_playstation(m, CC_BTN_STYLE_GREY);
    if (getenv("CC_FONT_DIR")) {
        char fnt[600], dds[600];
        snprintf(fnt, sizeof fnt, "%s/synjUnicode_20.fnt", getenv("CC_FONT_DIR"));
        snprintf(dds, sizeof dds, "%s/synjUnicode_20_0.dds", getenv("CC_FONT_DIR"));
        HostFont *hf = hostfont_load(fnt, dds, err, sizeof err);
        if (hf) as_render_set_font(hf);
        else printf("font: %s\n", err);
    }
    W = 960; H = 544;
    static uint8_t back[960 * 544 * 3];
    FILE *bf = argc > 4 ? fopen(argv[4], "rb") : NULL;
    int have_back = 0;
    if (bf) {
        int w, h;
        if (fscanf(bf, "P6 %d %d 255", &w, &h) == 2 && w == 960 && h == 544 && fgetc(bf) != EOF)
            have_back = fread(back, 1, sizeof back, bf) == sizeof back;
        fclose(bf);
    }
    PauseMenu pm = {0};
    pause_menu_open(&pm, getenv("CC_PAUSE_MAP") != NULL);
    // each step: buttons, then seconds to let pass before the picture
    static const struct { unsigned b; float wait; const char *what; } steps[] = {
        {0, 0.5f, "main"}, {PM_DOWN, 0.1f, "main, Help & Options chosen"}, {PM_DOWN, 0.05f, NULL},
        {PM_ACCEPT, 0.08f, "Information flashing"}, {0, 0.6f, "information"}, {PM_BACK, 0.6f, "main again"},
        {PM_DOWN, 0.05f, NULL}, {PM_ACCEPT, 0.7f, "exit to map: are you sure"}, {PM_BACK, 0.6f, NULL},
        {PM_DOWN, 0.05f, NULL}, {PM_ACCEPT, 0.7f, "exit to main menu: are you sure"},
        {PM_DOWN, 0.1f, "Yes chosen"},
    };
    DrawList vb = {0};
    for (size_t i = 0; i < sizeof steps / sizeof *steps; i++) {
        int sound, r = pause_menu_update(&pm, steps[i].b, 1 / 60.0f, &sound);
        for (float t = 0; t < steps[i].wait; t += 1 / 60.0f) r = pause_menu_update(&pm, 0, 1 / 60.0f, &sound);
        if (r != PM_OPEN) printf("step %zu: closes with %d\n", i, r);
        if (!steps[i].what) continue;
        if (have_back) memcpy(px, back, sizeof back), memset(stencil, 0, (size_t)W * H);
        else clear((const uint8_t[3]){90, 130, 70});
        render_begin(&vb);
        vb.view[2] = W;
        vb.view[3] = H;
        pause_menu_build(&pm, m, W / 848.0f, &vb);
        rasterise(&vb);
        printf("%zu: %s (page %d, line %d)\n", i, steps[i].what, pm.page, pm.sel);
        write_frame(argv[3], (int)i);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--as")) return as_mode(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--dress")) return dress_mode(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--sprite")) return sprite_mode(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--clips")) return clips_mode(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--pause")) return pause_mode(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--level")) return level_mode(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--player")) return player_mode(argc, argv);
    if (argc < 4) { fprintf(stderr, "usage: %s file.swf label out_prefix [frames]\n", argv[0]); return 1; }
    int frames = argc > 4 ? atoi(argv[4]) : 4;
    char err[256];
    clock_t t0 = clock();
    Movie *m = swf_load(argv[1], err, sizeof err);
    if (!m) { fprintf(stderr, "%s\n", err); return 1; }
    printf("loaded in %.2fs, truncated=%d\n", (double)(clock() - t0) / CLOCKS_PER_SEC, m->truncated);
    t0 = clock();
    render_tessellate_all(m);
    printf("tessellated %d shapes in %.2fs\n", render_stats.shapes_tessellated, (double)(clock() - t0) / CLOCKS_PER_SEC);
    int fi = sprite_find_label(&m->root, argv[2]);
    if (fi < 0) { fprintf(stderr, "no label %s\n", argv[2]); return 1; }
    DrawList vb = {0};
    static const uint8_t paper[3] = {237, 237, 224};
    for (int t = 0; t < frames; t++) {
        clear(paper);
        render_begin(&vb);
        Mat root = {2.0f / 20, 0, 0, 2.0f / 20, W / 2.0f, H * 0.8f};
        render_display_list(m, &m->root.frames[fi], root, CX_IDENTITY, t, &vb);
        rasterise(&vb);
        write_frame(argv[3], t);
    }
    swf_free(m);
    return 0;
}
