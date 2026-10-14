// Castle Crashers Vita: renderer test.
// Loads PLAYER.swf and the LEVELn.swf files from the memory card (from the
// user's own copy of the game). Two modes, switched with TRIANGLE:
//   level viewer:     stick/D-pad walks the knight, L/R changes level
//   animation viewer: Left/Right changes animation, L/R zooms
// SELECT toggles the 60 fps cap, START quits.
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/power.h>
#include <stdio.h>
#include <string.h>
#include <vita2d.h>

#include "enemy.h"

#define DATA_DIR "ux0:data/CastleCrashers/"
#define MAX_BATCH 65535   // u16 indices; a multiple of 3
#define MAX_LEVEL 60
#define SCREEN_W 960.0f
#define SCREEN_H 544.0f

static void set_stencil(SceGxmStencilFunc func, int ref, SceGxmStencilOp op) {
    // Triangle winding varies with flips, so set both faces. Depth-fail uses the
    // same op in case depth testing rejects anything.
    SceGxmContext *ctx = vita2d_get_context();
    sceGxmSetFrontStencilRef(ctx, ref);
    sceGxmSetBackStencilRef(ctx, ref);
    sceGxmSetFrontStencilFunc(ctx, func, SCE_GXM_STENCIL_OP_KEEP, op, op, 0xFF, 0xFF);
    sceGxmSetBackStencilFunc(ctx, func, SCE_GXM_STENCIL_OP_KEEP, op, op, 0xFF, 0xFF);
}

// ---- GPU memory -------------------------------------------------------------------

// vita2d's colour shader and projection. We draw with them directly so each shape
// gets its own matrix and the GPU does the vertex transform.
extern SceGxmVertexProgram *_vita2d_colorVertexProgram;
extern SceGxmFragmentProgram *_vita2d_colorFragmentProgram;
extern const SceGxmProgramParameter *_vita2d_colorWvpParam;
extern float _vita2d_ortho_matrix[16];

typedef struct {
    SceUID uid;
    void *addr;
} GpuBlock;

static void *gpu_alloc(size_t bytes, void *ctx) {
    GpuBlock *blk = ctx;
    size_t size = (bytes + 4095) & ~(size_t)4095;
    blk->uid = sceKernelAllocMemBlock("cc_mesh", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL);
    if (blk->uid < 0) return NULL;
    sceKernelGetMemBlockBase(blk->uid, &blk->addr);
    if (sceGxmMapMemory(blk->addr, size, SCE_GXM_MEMORY_ATTRIB_READ) < 0) {
        sceKernelFreeMemBlock(blk->uid);
        blk->uid = -1;
        return NULL;
    }
    return blk->addr;
}

static void gpu_free(GpuBlock *blk) {
    if (blk->uid < 0) return;
    sceGxmUnmapMemory(blk->addr);
    sceKernelFreeMemBlock(blk->uid);
    blk->uid = -1;
}

// Tessellates and uploads a movie's shapes; blk keeps the memory for gpu_free.
static void upload_movie(Movie *m, GpuBlock *blk) {
    blk->uid = -1;
    render_tessellate_all(m);
    render_upload(m, gpu_alloc, blk);
}

static uint16_t *g_indices;   // 0, 1, 2, ... for sceGxmDraw
static GpuBlock g_index_block;

static void init_indices(void) {
    g_indices = gpu_alloc(sizeof(uint16_t) * MAX_BATCH, &g_index_block);
    for (int i = 0; g_indices && i < MAX_BATCH; i++) g_indices[i] = i;
}

// ---- drawing ----------------------------------------------------------------------

// wvp = projection * item matrix. vita2d's projection keeps its translation at
// [12],[13] (or [3],[7] if transposed); write the 2D affine into the same slots.
static void item_wvp(const Mat *m, float out[16]) {
    const float *o = _vita2d_ortho_matrix;
    memcpy(out, o, sizeof(float) * 16);
    int t = o[12] != 0 || o[13] != 0;
    float ox = o[0], oy = o[5], otx = t ? o[12] : o[3], oty = t ? o[13] : o[7];
    out[0] = ox * m->a;
    out[5] = oy * m->d;
    out[t ? 4 : 1] = ox * m->c;   // y's contribution to x
    out[t ? 1 : 4] = oy * m->b;   // x's contribution to y
    out[t ? 12 : 3] = ox * m->tx + otx;
    out[t ? 13 : 7] = oy * m->ty + oty;
}

static void draw_list(const DrawList *dl) {
    static const SceGxmStencilOp ops[] = {SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_INCR, SCE_GXM_STENCIL_OP_DECR};
    if (!g_indices) return;
    SceGxmContext *ctx = vita2d_get_context();
    sceGxmSetVertexProgram(ctx, _vita2d_colorVertexProgram);
    sceGxmSetFragmentProgram(ctx, _vita2d_colorFragmentProgram);
    for (int c = 0; c < dl->ncmd; c++) {
        const DrawCmd *cmd = &dl->cmd[c];
        if (!cmd->count) continue;
        set_stencil(SCE_GXM_STENCIL_FUNC_EQUAL, cmd->ref, ops[cmd->op]);
        for (int k = cmd->start; k < cmd->start + cmd->count; k++) {
            const DrawItem *it = &dl->items[k];
            if (it->bf) continue;   // bitmap fills: only the game build draws them
            const Vertex *v = it->vp;
            if (!v || it->col || it->cv >= 0) {   // recoloured: this frame's whole vertices (mesh-space positions)
                Vertex *t = vita2d_pool_memalign(it->mesh->nverts * sizeof(Vertex), sizeof(Vertex));
                if (!t) continue;
                for (int i = 0; i < it->mesh->nverts; i++) {
                    Vertex s = render_item_vertex(dl, it, i);   // for its colour
                    t[i] = (Vertex){it->mesh->xy[i * 2], it->mesh->xy[i * 2 + 1], 0.5f, s.color};
                }
                v = t;
            }
            float wvp[16];
            item_wvp(&it->m, wvp);
            for (int off = 0; off < it->mesh->nverts; off += MAX_BATCH) {
                int count = it->mesh->nverts - off < MAX_BATCH ? it->mesh->nverts - off : MAX_BATCH;
                void *ub;
                sceGxmReserveVertexDefaultUniformBuffer(ctx, &ub);
                sceGxmSetUniformDataF(ub, _vita2d_colorWvpParam, 0, 16, wvp);
                sceGxmSetVertexStream(ctx, 0, v + off);
                sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g_indices, count);
            }
        }
    }
    // back to vita2d's defaults (ref 1 is what its clip rectangles expect)
    set_stencil(SCE_GXM_STENCIL_FUNC_ALWAYS, 1, SCE_GXM_STENCIL_OP_KEEP);
}

static void show_message(vita2d_pgf *font, const char *line1, const char *line2) {
    vita2d_set_clear_color(RGBA8(237, 237, 224, 255));
    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_pgf_draw_text(font, 40, 240, RGBA8(40, 40, 40, 255), 1.0f, line1);
    if (line2) vita2d_pgf_draw_text(font, 40, 280, RGBA8(90, 90, 90, 255), 0.8f, line2);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

// Loads the next level that exists, starting at n and stepping by dir (+1/-1),
// and uploads its meshes into blk.
static Level *load_level(vita2d_pgf *font, int *n, int dir, GpuBlock *blk, char *err, int errlen) {
    for (int tries = 0; tries < MAX_LEVEL; tries++) {
        char path[128], msg[64];
        snprintf(path, sizeof path, DATA_DIR "levels/LEVEL%d.swf", *n);
        snprintf(msg, sizeof msg, "Loading LEVEL%d...", *n);
        show_message(font, msg, NULL);
        Level *lv = level_load(path, err, errlen);
        if (lv && lv->nlayers) {
            upload_movie(lv->m, blk);   // up front, so walking around doesn't stall
            return lv;
        }
        level_free(lv);
        *n = (*n - 1 + dir + MAX_LEVEL) % MAX_LEVEL + 1;
    }
    return NULL;
}

int main(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetGpuClockFrequency(222);
    // the per-frame pool holds recoloured shapes (colour transforms, masks)
    vita2d_init_advanced_with_msaa(16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
    vita2d_set_vblank_wait(1);
    vita2d_pgf *font = vita2d_load_default_pgf();
    init_indices();

    char err[256] = "";
    GpuBlock player_gpu = {-1, NULL}, level_gpu = {-1, NULL};
    Movie *m = swf_load(DATA_DIR "PLAYER.swf", err, sizeof err);
    if (m) upload_movie(m, &player_gpu);
    int label = 0;
    int walk = m ? sprite_find_label(&m->root, "walk") : -1;
    if (m)
        for (int i = 0; i < m->root.nlabels; i++)
            if (m->root.labels[i].frame == walk) label = i;

    int level_mode = 1, level_num = 3, show_collision = 0;
    char level_err[256] = "";
    Level *lv = m ? load_level(font, &level_num, 1, &level_gpu, level_err, sizeof level_err) : NULL;
    static Rig rig;
    Player knight;
    static Enemies enemies;
    if (m) {
        rig_init(&rig, m);
        player_init(&knight, &rig, lv ? lv->spawn_x : 0, lv ? lv->spawn_y : 0);
    }
    enemies_reset(&enemies);

    DrawList vb = {0};
    // recoloured meshes (colour transforms, masks) live here across frames
    GpuBlock cache_gpu = {-1, NULL};
    const size_t cache_bytes = 8 * 1024 * 1024;
    RecolourCache cache;
    recolour_cache_init(&cache, gpu_alloc(cache_bytes, &cache_gpu), cache_bytes);
    vb.cache = &cache;
    SceCtrlData pad, old = {0};
    float zoom = 2.5f;
    int vsync = 0, tick = 0;
    SceUInt64 last = sceKernelGetProcessTimeWide();
    float fps = 0;
    int frames_counted = 0;
    // per-second accumulators; the overlay shows the previous second's values so it is readable
    SceUInt64 build_sum = 0, build_max = 0;
    int tris_max = 0;
    float shown_avg_ms = 0, shown_max_ms = 0;
    int shown_tris = 0, vblank = 1;

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int pressed = pad.buttons & ~old.buttons;
        old = pad;
        if (pressed & SCE_CTRL_START) break;
        // SELECT + button: debug functions, so the face buttons are free for the game
        int debug = pad.buttons & SCE_CTRL_SELECT;
        if (debug && (pressed & SCE_CTRL_UP)) { vblank = !vblank; vita2d_set_vblank_wait(vblank); }
        if (debug && (pressed & SCE_CTRL_TRIANGLE)) level_mode = !level_mode;
        if (debug && (pressed & SCE_CTRL_DOWN)) show_collision = !show_collision;

        if (level_mode && m) {
            if (debug && (pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))) {
                int dir = pressed & SCE_CTRL_RIGHT ? 1 : -1;
                vita2d_wait_rendering_done();   // the GPU may still be reading the old meshes
                level_free(lv);
                gpu_free(&level_gpu);
                void *cache_mem = cache.mem;   // entries point at the old level's meshes
                recolour_cache_free(&cache);
                recolour_cache_init(&cache, cache_mem, cache_bytes);
                level_num = (level_num - 1 + dir + MAX_LEVEL) % MAX_LEVEL + 1;
                lv = load_level(font, &level_num, dir, &level_gpu, level_err, sizeof level_err);
                if (lv) player_init(&knight, &rig, lv->spawn_x, lv->spawn_y);
                enemies_reset(&enemies);
                last = sceKernelGetProcessTimeWide();
                frames_counted = 0;
            }
            // analog stick with a dead zone, or the D-pad (not while SELECT is held)
            PlayerInput in = {0};
            if (!debug) {
                in.dx = (pad.lx - 128) / 128.0f;
                in.dy = (pad.ly - 128) / 128.0f;
                if (in.dx > -0.25f && in.dx < 0.25f) in.dx = 0;
                if (in.dy > -0.25f && in.dy < 0.25f) in.dy = 0;
                if (pad.buttons & SCE_CTRL_LEFT) in.dx = -1;
                if (pad.buttons & SCE_CTRL_RIGHT) in.dx = 1;
                if (pad.buttons & SCE_CTRL_UP) in.dy = -1;
                if (pad.buttons & SCE_CTRL_DOWN) in.dy = 1;
                in.light = (pressed & SCE_CTRL_SQUARE) != 0;
                in.heavy = (pressed & SCE_CTRL_TRIANGLE) != 0;
                in.jump = (pressed & SCE_CTRL_CROSS) != 0;
                in.block = (pad.buttons & SCE_CTRL_RTRIGGER) != 0;
            }
            player_update(&knight, lv, &in, 0.5f);   // the game runs at 30 frames a second
            if (lv && lv->nsegs) enemies_update(&enemies, lv, &rig, &knight, 0.5f);   // not in walk-only scenes
            if (knight.hp <= 0) {   // knocked out: back to the start of the level, fresh
                player_init(&knight, &rig, lv ? lv->spawn_x : 0, lv ? lv->spawn_y : 0);
                enemies_reset(&enemies);
            }
            if (lv && lv->edge_right > lv->edge_left) {
                if (knight.mv.x < lv->edge_left + 30) knight.mv.x = lv->edge_left + 30;
                if (knight.mv.x > lv->edge_right - 30) knight.mv.x = lv->edge_right - 30;
            }
        } else if (m && m->root.nlabels) {
            if (!debug && (pressed & SCE_CTRL_RIGHT)) { label = (label + 1) % m->root.nlabels; tick = 0; }
            if (!debug && (pressed & SCE_CTRL_LEFT)) { label = (label + m->root.nlabels - 1) % m->root.nlabels; tick = 0; }
            if (pad.buttons & SCE_CTRL_RTRIGGER) zoom *= 1.02f;
            if (pad.buttons & SCE_CTRL_LTRIGGER) zoom /= 1.02f;
        }

        SceUInt64 t0 = sceKernelGetProcessTimeWide();
        render_begin(&vb);
        if (level_mode && lv) {
            vb.view[0] = vb.view[1] = 0;
            vb.view[2] = SCREEN_W;
            vb.view[3] = SCREEN_H;
            float k = SCREEN_W / GAME_W;
            Mat screen = {k / 20, 0, 0, k / 20, 0, 0};
            Actor actors[1 + MAX_ENEMIES];
            actors[0] = player_actor(&knight);
            int nactors = 1 + enemies_actors(&enemies, &rig, actors + 1, MAX_ENEMIES);
            level_render(lv, screen, level_clamp_camera_x(lv, knight.mv.x), knight.mv.y, 100, tick, actors, nactors, &vb);
        } else if (!level_mode && m && m->root.nlabels) {
            vb.view[2] = 0;   // no culling: the zoom can push parts off screen and back
            Mat root = {zoom / 20.0f, 0, 0, zoom / 20.0f, 480.0f, 420.0f};
            int fi = m->root.labels[label].frame;
            render_display_list(m, &m->root.frames[fi], root, CX_IDENTITY, tick, &vb);
        }
        SceUInt64 build = sceKernelGetProcessTimeWide() - t0;
        build_sum += build;
        if (build > build_max) build_max = build;
        if (render_stats.triangles > tris_max) tris_max = render_stats.triangles;

        if (level_mode && lv) vita2d_set_clear_color(RGBA8(lv->m->bg[0], lv->m->bg[1], lv->m->bg[2], 255));
        else vita2d_set_clear_color(RGBA8(237, 237, 224, 255));
        vita2d_start_drawing();
        vita2d_clear_screen();
        unsigned int ink = level_mode ? RGBA8(255, 255, 255, 255) : RGBA8(40, 40, 40, 255);
        if (m && (lv || !level_mode)) {
            draw_list(&vb);
            if (level_mode && show_collision) {
                // red: walls (types 1-4), blue: stairs (100s), green: other types
                float k = SCREEN_W / GAME_W;
                Mat screen = {k / 20, 0, 0, k / 20, 0, 0};
                Mat gm = level_game_matrix(lv, screen, level_clamp_camera_x(lv, knight.mv.x), knight.mv.y, 100);
                for (int i = 0; i < lv->nsegs; i++) {
                    const Segment *s = &lv->segs[i];
                    unsigned int col = s->type <= 4 ? RGBA8(255, 0, 0, 255)
                                     : s->type < 200 ? RGBA8(0, 80, 255, 255) : RGBA8(0, 200, 0, 255);
                    vita2d_draw_line(gm.a * s->x0 * 20 + gm.c * s->y0 * 20 + gm.tx, gm.b * s->x0 * 20 + gm.d * s->y0 * 20 + gm.ty,
                                     gm.a * s->x1 * 20 + gm.c * s->y1 * 20 + gm.tx, gm.b * s->x1 * 20 + gm.d * s->y1 * 20 + gm.ty, col);
                }
            }
            if (level_mode) {
                vita2d_pgf_draw_textf(font, 16, 28, ink, 1.0f, "LEVEL%d   %s   wave %d   KOs %d", level_num,
                                      player_move_name(&knight), enemies.wave, enemies.kills);
                // health bar, top right
                float w = 200.0f * knight.hp / knight.hp_max;
                vita2d_draw_rectangle(SCREEN_W - 216, 12, 204, 18, RGBA8(0, 0, 0, 160));
                vita2d_draw_rectangle(SCREEN_W - 214, 14, w, 14, RGBA8(220, 40, 40, 255));
            }
            else
                vita2d_pgf_draw_textf(font, 16, 28, ink, 1.0f, "%s  (%d/%d)",
                                      m->root.nlabels ? m->root.labels[label].name : "-", label + 1, m->root.nlabels);
            vita2d_pgf_draw_textf(font, 16, 52, ink, 0.8f,
                                  "%.0f fps%s   peak %d tris   build avg %.2f / peak %.2f ms   draws %d   cpu-coloured %d   cached %d",
                                  fps, vblank ? "" : " (uncapped)", shown_tris, shown_avg_ms, shown_max_ms,
                                  render_stats.draws, render_stats.cx_tris, render_stats.cached_tris);
            vita2d_pgf_draw_text(font, 16, 528, ink, 0.8f,
                                 level_mode ? "SQUARE light  TRIANGLE heavy  CROSS jump  R block   SELECT+: Left/Right level, Down collision, Up fps cap, TRIANGLE anims"
                                            : "Left/Right: animation   L/R: zoom   SELECT+TRIANGLE: levels   SELECT+Up: fps cap   START: quit");
        } else {
            vita2d_pgf_draw_text(font, 40, 240, RGBA8(160, 30, 30, 255), 1.0f, m ? level_err : err);
            vita2d_pgf_draw_text(font, 40, 280, RGBA8(40, 40, 40, 255), 1.0f,
                                 m ? "Copy the levels folder from your own game files to " DATA_DIR "levels/"
                                   : "Copy PLAYER.swf from your own game files to " DATA_DIR);
            vita2d_pgf_draw_text(font, 40, 320, RGBA8(40, 40, 40, 255), 1.0f,
                                 m ? "TRIANGLE: animations   START: quit" : "Press START to quit.");
        }
        vita2d_end_drawing();
        vita2d_swap_buffers();

        if (++vsync % 2 == 0) tick++;   // game animates at 30 fps
        frames_counted++;
        SceUInt64 now = sceKernelGetProcessTimeWide();
        if (now - last >= 1000000) {
            fps = frames_counted * 1000000.0f / (now - last);
            shown_avg_ms = build_sum / 1000.0f / frames_counted;
            shown_max_ms = build_max / 1000.0f;
            shown_tris = tris_max;
            build_sum = build_max = 0;
            tris_max = 0;
            frames_counted = 0;
            last = now;
        }
    }

    vita2d_wait_rendering_done();
    level_free(lv);
    swf_free(m);
    gpu_free(&level_gpu);
    gpu_free(&player_gpu);
    gpu_free(&g_index_block);
    recolour_cache_free(&cache);
    gpu_free(&cache_gpu);
    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
