// Castle Crashers Vita: runs the game's own ActionScript (MAIN.swf and the files it
// loads) from the user's copy of the game in ux0:data/CastleCrashers/game and
// .../levels. The game logic runs at its 30 frames a second; drawing is the
// GPU shape renderer.
//
// Controls (player 1): stick/D-pad move, SQUARE light attack, TRIANGLE heavy,
// CROSS jump/accept, CIRCLE projectile/back, R block, L magic, START pause.
// SELECT+UP shows the stats overlay, SELECT+START quits.
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/power.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <vita2d.h>

#include "as.h"
#include "as_render.h"
#include "audio.h"
#include "cc_host.h"
#include "hostfont.h"
#include "render.h"

// parsed SWFs and the script heap (meshes live in GPU memory blocks outside it)
int _newlib_heap_size_user = 160 * 1024 * 1024;

#define DATA_DIR "ux0:data/CastleCrashers/"
#define SAVE_PATH DATA_DIR "save.dat"   // the game's storage (profiles, levels, unlocks)
#define MAX_BATCH 65535   // u16 indices; a multiple of 3
#define SCREEN_W 960.0f
#define SCREEN_H 544.0f

static void set_stencil(SceGxmStencilFunc func, int ref, SceGxmStencilOp op) {
    // Triangle winding varies with flips, so set both faces.
    SceGxmContext *ctx = vita2d_get_context();
    sceGxmSetFrontStencilRef(ctx, ref);
    sceGxmSetBackStencilRef(ctx, ref);
    sceGxmSetFrontStencilFunc(ctx, func, SCE_GXM_STENCIL_OP_KEEP, op, op, 0xFF, 0xFF);
    sceGxmSetBackStencilFunc(ctx, func, SCE_GXM_STENCIL_OP_KEEP, op, op, 0xFF, 0xFF);
}

// ---- GPU memory -------------------------------------------------------------------

extern SceGxmVertexProgram *_vita2d_colorVertexProgram;
extern SceGxmFragmentProgram *_vita2d_colorFragmentProgram;
extern const SceGxmProgramParameter *_vita2d_colorWvpParam;
extern SceGxmVertexProgram *_vita2d_textureVertexProgram;
extern SceGxmFragmentProgram *_vita2d_textureTintFragmentProgram;
extern const SceGxmProgramParameter *_vita2d_textureWvpParam;
extern SceGxmProgramParameter *_vita2d_textureTintColorParam;
extern float _vita2d_ortho_matrix[16];

typedef struct {
    SceUID uid;
    void *addr;
} GpuBlock;

static size_t g_gpu_bytes;   // for the overlay

// Uncached main memory the GPU can read (for data written every frame or in place).
static void *gpu_alloc(size_t bytes, void *ctx) {
    GpuBlock *blk = ctx;
    size_t size = (bytes + 4095) & ~(size_t)4095;
    blk->uid = sceKernelAllocMemBlock("cc_gpu", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL);
    if (blk->uid < 0) return NULL;
    sceKernelGetMemBlockBase(blk->uid, &blk->addr);
    if (sceGxmMapMemory(blk->addr, size, SCE_GXM_MEMORY_ATTRIB_READ) < 0) {
        sceKernelFreeMemBlock(blk->uid);
        blk->uid = -1;
        return NULL;
    }
    g_gpu_bytes += size;
    return blk->addr;
}

// Meshes: video memory when there's room (fast for the GPU, written once), else main memory.
static void *mesh_alloc(size_t bytes, void *ctx) {
    GpuBlock *blk = ctx;
    size_t size = (bytes + 256 * 1024 - 1) & ~(size_t)(256 * 1024 - 1);
    blk->uid = sceKernelAllocMemBlock("cc_mesh", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, NULL);
    if (blk->uid >= 0) {
        sceKernelGetMemBlockBase(blk->uid, &blk->addr);
        if (sceGxmMapMemory(blk->addr, size, SCE_GXM_MEMORY_ATTRIB_READ) >= 0) {
            g_gpu_bytes += size;
            return blk->addr;
        }
        sceKernelFreeMemBlock(blk->uid);
    }
    return gpu_alloc(bytes, ctx);
}

static void gpu_free(GpuBlock *blk) {
    if (blk->uid < 0) return;
    sceGxmUnmapMemory(blk->addr);
    sceKernelFreeMemBlock(blk->uid);
    blk->uid = -1;
}

static uint16_t *g_indices;   // 0, 1, 2, ... for sceGxmDraw
static GpuBlock g_index_block;

// ---- drawing ----------------------------------------------------------------------

// wvp = projection * item matrix (vita2d's projection keeps its translation at [12],[13]
// or, transposed, at [3],[7]).
static void item_wvp(const Mat *m, float out[16]) {
    const float *o = _vita2d_ortho_matrix;
    memcpy(out, o, sizeof(float) * 16);
    int t = o[12] != 0 || o[13] != 0;
    float ox = o[0], oy = o[5], otx = t ? o[12] : o[3], oty = t ? o[13] : o[7];
    out[0] = ox * m->a;
    out[5] = oy * m->d;
    out[t ? 4 : 1] = ox * m->c;
    out[t ? 1 : 4] = oy * m->b;
    out[t ? 12 : 3] = ox * m->tx + otx;
    out[t ? 13 : 7] = oy * m->ty + oty;
}

static void draw_list(const DrawList *dl) {
    static const SceGxmStencilOp ops[] = {SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_INCR, SCE_GXM_STENCIL_OP_DECR};
    if (!g_indices) return;
    SceGxmContext *ctx = vita2d_get_context();
    Vertex *cv = NULL;
    if (dl->ncv) {
        cv = vita2d_pool_memalign(dl->ncv * sizeof(Vertex), sizeof(Vertex));
        if (!cv) return;   // temp pool exhausted; skip rather than crash
        memcpy(cv, dl->cv, dl->ncv * sizeof(Vertex));
    }
    int textured = -1;   // which program pair is set
    for (int c = 0; c < dl->ncmd; c++) {
        const DrawCmd *cmd = &dl->cmd[c];
        if (!cmd->count) continue;
        set_stencil(SCE_GXM_STENCIL_FUNC_EQUAL, cmd->ref, ops[cmd->op]);
        for (int k = cmd->start; k < cmd->start + cmd->count; k++) {
            const DrawItem *it = &dl->items[k];
            float wvp[16];
            item_wvp(&it->m, wvp);
            if (it->bf) {   // bitmap fill: textured, tinted by the colour transform
                vita2d_texture *tex = it->bf->bitmap ? it->bf->bitmap->tex : NULL;
                if (!tex) continue;
                const TexVertex *tv = it->mesh->gpu;
                if (!tv) {   // not uploaded: this frame's copy
                    TexVertex *t = vita2d_pool_memalign(it->mesh->nverts * sizeof(TexVertex), sizeof(TexVertex));
                    if (!t || !it->mesh->xy) continue;
                    for (int i = 0; i < it->mesh->nverts; i++)
                        t[i] = (TexVertex){it->mesh->xy[i * 2], it->mesh->xy[i * 2 + 1], 0.5f, it->mesh->uv[i * 2],
                                           it->mesh->uv[i * 2 + 1]};
                    tv = t;
                }
                if (textured != 1) {
                    sceGxmSetVertexProgram(ctx, _vita2d_textureVertexProgram);
                    sceGxmSetFragmentProgram(ctx, _vita2d_textureTintFragmentProgram);
                    textured = 1;
                }
                SceGxmTextureFilter filt = it->bf->smooth ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT;
                vita2d_texture_set_filters(tex, filt, filt);
                SceGxmTextureAddrMode am = it->bf->clip ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT;
                sceGxmTextureSetUAddrMode(&tex->gxm_tex, am);
                sceGxmTextureSetVAddrMode(&tex->gxm_tex, am);
                sceGxmSetFragmentTexture(ctx, 0, &tex->gxm_tex);
                void *fub;
                sceGxmReserveFragmentDefaultUniformBuffer(ctx, &fub);
                sceGxmSetUniformDataF(fub, _vita2d_textureTintColorParam, 0, 4, it->tint);
                for (int off = 0; off < it->mesh->nverts; off += MAX_BATCH) {
                    int count = it->mesh->nverts - off < MAX_BATCH ? it->mesh->nverts - off : MAX_BATCH;
                    void *ub;
                    sceGxmReserveVertexDefaultUniformBuffer(ctx, &ub);
                    sceGxmSetUniformDataF(ub, _vita2d_textureWvpParam, 0, 16, wvp);
                    sceGxmSetVertexStream(ctx, 0, tv + off);
                    sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g_indices, count);
                }
                continue;
            }
            if (textured != 0) {
                sceGxmSetVertexProgram(ctx, _vita2d_colorVertexProgram);
                sceGxmSetFragmentProgram(ctx, _vita2d_colorFragmentProgram);
                textured = 0;
            }
            const Vertex *v = it->vp ? it->vp : cv + it->cv;
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
    set_stencil(SCE_GXM_STENCIL_FUNC_ALWAYS, 1, SCE_GXM_STENCIL_OP_KEEP);
}

static vita2d_pgf *g_font;

static void show_message(const char *line1, const char *line2) {
    vita2d_set_clear_color(RGBA8(20, 20, 24, 255));
    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_pgf_draw_text(g_font, 40, 240, RGBA8(240, 240, 240, 255), 1.0f, line1);
    if (line2) vita2d_pgf_draw_text(g_font, 40, 280, RGBA8(170, 170, 170, 255), 0.8f, line2);
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

// ---- loading the game's files -------------------------------------------------------

// "../player/player.swf" -> ux0:data/CastleCrashers/game/PLAYER.swf (or levels/).
// Each file is parsed and uploaded once and kept: the game loads player.swf into
// every player slot and fx.swf into every effect.
#define MAX_MOVIES 160
static struct { char name[64]; Movie *m; GpuBlock gpu; } g_movies[MAX_MOVIES];
static int g_nmovies;
static char g_last_error[256];

static Movie *vita_loader(AsVm *vm, const char *url) {
    (void)vm;
    const char *base = strrchr(url, '/');
    base = base ? base + 1 : url;
    for (int i = 0; i < g_nmovies; i++)
        if (!strcasecmp(g_movies[i].name, base)) return g_movies[i].m;
    if (g_nmovies == MAX_MOVIES) return NULL;
    static const char *dirs[] = {"game", "levels"};
    for (int i = 0; i < 2; i++) {
        char path[256], err[256];
        snprintf(path, sizeof path, DATA_DIR "%s/%s", dirs[i], base);
        SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
        if (fd < 0) continue;
        sceIoClose(fd);
        char msg[128];
        snprintf(msg, sizeof msg, "Loading %s...", base);
        show_message(msg, NULL);
        Movie *m = swf_load(path, err, sizeof err);
        if (!m) {
            snprintf(g_last_error, sizeof g_last_error, "%s: %s", base, err);
            return NULL;
        }
        // bitmaps become textures; the pixels are then only on the GPU side
        for (int c = 0; c < 65536; c++) {
            if (m->chars[c].type != CHAR_BITMAP) continue;
            Bitmap *bm = m->chars[c].ptr;
            vita2d_texture *tex = vita2d_create_empty_texture_format(bm->w, bm->h, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
            if (!tex) continue;
            uint8_t *dst = vita2d_texture_get_datap(tex);
            unsigned stride = vita2d_texture_get_stride(tex);
            for (int y = 0; y < bm->h; y++) memcpy(dst + y * stride, bm->rgba + (size_t)y * bm->w * 4, (size_t)bm->w * 4);
            bm->tex = tex;
            free(bm->rgba);
            bm->rgba = NULL;
        }
        GpuBlock *blk = &g_movies[g_nmovies].gpu;
        blk->uid = -1;
        render_tessellate_all(m);
        if (render_upload(m, mesh_alloc, blk)) render_release_cpu(m);
        snprintf(g_movies[g_nmovies].name, sizeof g_movies[0].name, "%s", base);
        g_movies[g_nmovies++].m = m;
        return m;
    }
    snprintf(g_last_error, sizeof g_last_error, "missing %s", base);
    return NULL;
}

// ---- controls -> the key codes f_GetButtons gives port 1 ---------------------------

enum {
    K_ACCEPT = 2, K_BACK = 3, K_LEFT = 37, K_UP = 38, K_RIGHT = 39, K_DOWN = 40,
    K_START = 49, K_SELECT = 53, K_PUNCH1 = 65, K_R2 = 66, K_L2 = 67, K_PROJECTILE = 68,
    K_MAGIC = 69, K_SHIELD = 81, K_JUMP = 83, K_PUNCH2 = 87,
};

static void map_pad(const SceCtrlData *pad, int debug) {
    memset(cc_keys, 0, sizeof cc_keys);
    if (debug) return;
    unsigned b = pad->buttons;
    int lx = pad->lx - 128, ly = pad->ly - 128;
    if ((b & SCE_CTRL_LEFT) || lx < -48) cc_keys[K_LEFT] = 1;
    if ((b & SCE_CTRL_RIGHT) || lx > 48) cc_keys[K_RIGHT] = 1;
    if ((b & SCE_CTRL_UP) || ly < -48) cc_keys[K_UP] = 1;
    if ((b & SCE_CTRL_DOWN) || ly > 48) cc_keys[K_DOWN] = 1;
    if (b & SCE_CTRL_CROSS) cc_keys[K_ACCEPT] = cc_keys[K_JUMP] = 1;
    if (b & SCE_CTRL_CIRCLE) cc_keys[K_BACK] = cc_keys[K_PROJECTILE] = 1;
    if (b & SCE_CTRL_SQUARE) cc_keys[K_PUNCH1] = 1;
    if (b & SCE_CTRL_TRIANGLE) cc_keys[K_PUNCH2] = 1;
    if (b & SCE_CTRL_RTRIGGER) cc_keys[K_SHIELD] = 1;
    if (b & SCE_CTRL_LTRIGGER) cc_keys[K_MAGIC] = 1;
    if (b & SCE_CTRL_START) cc_keys[K_START] = 1;
}

static void vita_sound_start(AsVm *vm, const char *linkage, float volume, float pan, int loops, const void *owner) {
    (void)vm;
    audio_sfx_play(linkage, volume, pan, loops, owner);
}

static void vita_sound_stop(AsVm *vm, const void *owner) {
    (void)vm;
    audio_sfx_stop(owner);
}

static void vita_warn(AsVm *vm, const char *msg) {
    (void)vm;
    snprintf(g_last_error, sizeof g_last_error, "%s", msg);
}

int main(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    vita2d_init_advanced_with_msaa(16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
    vita2d_set_vblank_wait(1);
    g_font = vita2d_load_default_pgf();
    g_indices = gpu_alloc(sizeof(uint16_t) * MAX_BATCH, &g_index_block);
    for (int i = 0; g_indices && i < MAX_BATCH; i++) g_indices[i] = i;
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    Movie *start = vita_loader(NULL, "MAIN.swf");
    if (!start) {
        SceCtrlData pad;
        for (;;) {
            show_message(g_last_error, "Copy the game and levels folders from your own game files to " DATA_DIR
                                       "   (START: quit)");
            sceCtrlPeekBufferPositive(0, &pad, 1);
            if (pad.buttons & SCE_CTRL_START) break;
        }
        vita2d_fini();
        sceKernelExitProcess(0);
        return 0;
    }

    cc_storage_load(SAVE_PATH);
    audio_init(DATA_DIR);
    cc_music_play = audio_music_play;
    cc_music_stop = audio_music_stop;
    AsVm *vm = as_new(vita_loader);
    vm->sound_start = vita_sound_start;
    vm->sound_stop = vita_sound_stop;
    vm->warn = vita_warn;
    cc_host_init(vm);
    as_render_init(vm);
    // the game's own text font (from the PC copy's data/fonts); text is left out without it
    char ferr[256];
    HostFont *font = hostfont_load(DATA_DIR "fonts/synjUnicode_20.fnt", DATA_DIR "fonts/synjUnicode_20_0.dds", ferr,
                                   sizeof ferr);
    if (font) {
        Bitmap *a = &font->atlas;
        vita2d_texture *tex = vita2d_create_empty_texture_format(a->w, a->h, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
        if (tex) {
            uint8_t *dst = vita2d_texture_get_datap(tex);
            unsigned stride = vita2d_texture_get_stride(tex);
            for (int y = 0; y < a->h; y++) memcpy(dst + y * stride, a->rgba + (size_t)y * a->w * 4, (size_t)a->w * 4);
            a->tex = tex;
            as_render_set_font(font);
        }
    } else {
        snprintf(g_last_error, sizeof g_last_error, "%s", ferr);
    }
    vm->root = as_mc_new(vm, start, &start->root, NULL, 0);

    DrawList vb = {0};
    vb.view[2] = SCREEN_W;
    vb.view[3] = SCREEN_H;
    // recoloured meshes (colour transforms, masks) kept across frames
    GpuBlock cache_gpu = {-1, NULL};
    const size_t cache_bytes = 16 * 1024 * 1024;
    RecolourCache cache;
    void *cache_mem = gpu_alloc(cache_bytes, &cache_gpu);
    recolour_cache_init(&cache, cache_mem, cache_bytes);
    vb.cache = &cache;

    SceCtrlData pad, old = {0};
    int overlay = 0;
    SceUInt64 last = sceKernelGetProcessTimeWide(), acc = 0, prev = last;
    float fps = 0, tick_ms = 0, build_ms = 0, shown_tick = 0, shown_build = 0;
    int frames = 0, ticks = 0;
    const SceUInt64 tick_us = 1000000 / 30;   // the game's frame rate

    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pressed = pad.buttons & ~old.buttons;
        old = pad;
        int debug = pad.buttons & SCE_CTRL_SELECT;
        if (debug && (pressed & SCE_CTRL_START)) break;
        if (debug && (pressed & SCE_CTRL_UP)) overlay = !overlay;
        map_pad(&pad, debug);

        // run the game's frames that are due (at most two, so a slow frame doesn't snowball)
        SceUInt64 now = sceKernelGetProcessTimeWide();
        acc += now - prev;
        prev = now;
        if (acc > 2 * tick_us) acc = 2 * tick_us;
        int ticked = 0;
        while (acc >= tick_us) {
            SceUInt64 t0 = sceKernelGetProcessTimeWide();
            as_tick(vm);
            as_gc(vm);
            tick_ms += (sceKernelGetProcessTimeWide() - t0) / 1000.0f;
            ticks++;
            acc -= tick_us;
            ticked = 1;
        }
        if (cc_save_requested) {
            cc_save_requested = 0;
            if (!cc_storage_save(SAVE_PATH)) snprintf(g_last_error, sizeof g_last_error, "couldn't write " SAVE_PATH);
        }

        if (ticked) {
            // a full recolour cache stops helping: start it over (after the GPU is done with it)
            if (cache.used > cache.cap * 9 / 10 || cache.entries * 4 >= cache.table_size * 3) {
                vita2d_wait_rendering_done();
                recolour_cache_free(&cache);
                recolour_cache_init(&cache, cache_mem, cache_bytes);
            }
            SceUInt64 t0 = sceKernelGetProcessTimeWide();
            float k = SCREEN_W / start->stage_w;
            Mat screen = {k / 20, 0, 0, k / 20, 0, 0};
            render_begin(&vb);
            as_render_mc(vm->root, screen, CX_IDENTITY, &vb);
            build_ms += (sceKernelGetProcessTimeWide() - t0) / 1000.0f;
        }

        vita2d_set_clear_color(RGBA8(start->bg[0], start->bg[1], start->bg[2], 255));
        vita2d_start_drawing();
        vita2d_clear_screen();
        draw_list(&vb);
        if (overlay) {
            unsigned ink = RGBA8(255, 255, 255, 255);
            vita2d_draw_rectangle(0, 0, SCREEN_W, 64, RGBA8(0, 0, 0, 150));
            vita2d_pgf_draw_textf(g_font, 12, 24, ink, 0.8f,
                                  "%.0f fps   script %.1f ms   build %.1f ms   %d tris  %d draws  cpu-coloured %d   heap %zu objs",
                                  fps, shown_tick, shown_build, render_stats.triangles, render_stats.draws,
                                  render_stats.cx_tris, vm->heap_count);
            vita2d_pgf_draw_textf(g_font, 12, 50, ink, 0.8f, "%d files, %.1f MB GPU   %s", g_nmovies,
                                  g_gpu_bytes / 1048576.0, g_last_error);
        }
        vita2d_end_drawing();
        vita2d_swap_buffers();

        frames++;
        now = sceKernelGetProcessTimeWide();
        if (now - last >= 1000000) {
            fps = frames * 1000000.0f / (now - last);
            shown_tick = ticks ? tick_ms / ticks : 0;
            shown_build = ticks ? build_ms / ticks : 0;
            tick_ms = build_ms = 0;
            frames = ticks = 0;
            last = now;
        }
    }

    vita2d_wait_rendering_done();
    audio_shutdown();
    as_free(vm);
    for (int i = 0; i < g_nmovies; i++) {
        for (int c = 0; c < 65536; c++)
            if (g_movies[i].m->chars[c].type == CHAR_BITMAP && ((Bitmap *)g_movies[i].m->chars[c].ptr)->tex)
                vita2d_free_texture(((Bitmap *)g_movies[i].m->chars[c].ptr)->tex);
        swf_free(g_movies[i].m);
        gpu_free(&g_movies[i].gpu);
    }
    recolour_cache_free(&cache);
    gpu_free(&cache_gpu);
    gpu_free(&g_index_block);
    vita2d_free_pgf(g_font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
