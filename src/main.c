// Castle Crashers Vita: runs the game's own ActionScript (MAIN.swf and the files it
// loads) from the user's copy of the game in ux0:data/CastleCrashers/game and
// .../levels. The game logic runs at its 30 frames a second; drawing is the
// GPU shape renderer.
//
// Controls (player 1): left stick move (light tilt walks, full tilt runs, as on the
// original consoles), D-pad run, SQUARE light attack, TRIANGLE heavy, CROSS
// jump/accept, CIRCLE projectile/item/back, L block, R magic, START pause,
// right stick flicked left/right: previous/next item.
// SELECT+UP shows the stats overlay, SELECT+START quits.
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/ime_dialog.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/power.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <vita2d.h>

#include "as.h"
#include "as_render.h"
#include "audio.h"
#include "cc_buttons.h"
#include "cc_host.h"
#include "custom_animal.h"
#include "weapon_look.h"
#include "help_book.h"
#include "custom_char.h"
#include "paint_jr.h"
#include "hostfont.h"
#include "pause_menu.h"
#include "render.h"
#include "strtab.h"
#ifdef CC_TROPHIES
#include "trophies.h"
#endif
#ifdef CC_NET
#include "lobby.h"
#include "netplay.h"
#endif

// parsed SWFs and the script heap (meshes live in GPU memory blocks outside it)
int _newlib_heap_size_user = 160 * 1024 * 1024;

#define DATA_DIR "ux0:data/CastleCrashers/"
#define SAVE_PATH DATA_DIR "save.dat"   // the game's storage (profiles, levels, unlocks)
#define ACH_PATH DATA_DIR "achievements.dat"
#define SETTINGS_PATH DATA_DIR "settings.txt"   // Help & Options' Settings: volumes, Auto Potion
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
    size_t size;
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
    g_gpu_bytes += blk->size = size;
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
            g_gpu_bytes += blk->size = size;
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
    g_gpu_bytes -= blk->size;
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

// vita2d's colour shader again, but reading colours from a second stream: positions come
// from the uploaded mesh (stream 0) and colours from a recoloured array (stream 1), so a
// colour transform only rewrites 4 bytes a vertex and never reads video memory back.
extern const SceGxmProgram color_v_gxp_start;
static SceGxmVertexProgram *g_split_vp;

static void make_split_program(void) {
    SceGxmShaderPatcher *sp = vita2d_get_shader_patcher();
    const SceGxmProgram *prog = &color_v_gxp_start;
    SceGxmShaderPatcherId id;
    if (sceGxmShaderPatcherRegisterProgram(sp, prog, &id) < 0) return;
    const SceGxmProgramParameter *pos = sceGxmProgramFindParameterByName(prog, "aPosition");
    const SceGxmProgramParameter *col = sceGxmProgramFindParameterByName(prog, "aColor");
    if (!pos || !col) return;
    SceGxmVertexAttribute attr[2];
    attr[0].streamIndex = 0;
    attr[0].offset = 0;
    attr[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attr[0].componentCount = 3;
    attr[0].regIndex = sceGxmProgramParameterGetResourceIndex(pos);
    attr[1].streamIndex = 1;
    attr[1].offset = 0;
    attr[1].format = SCE_GXM_ATTRIBUTE_FORMAT_U8N;
    attr[1].componentCount = 4;
    attr[1].regIndex = sceGxmProgramParameterGetResourceIndex(col);
    SceGxmVertexStream st[2] = {{sizeof(Vertex), SCE_GXM_INDEX_SOURCE_INDEX_16BIT},
                                {sizeof(uint32_t), SCE_GXM_INDEX_SOURCE_INDEX_16BIT}};
    if (sceGxmShaderPatcherCreateVertexProgram(sp, id, attr, 2, st, 2, &g_split_vp) < 0) g_split_vp = NULL;
}

// Draws the items from..to-1 of a frame's list (all of it: draw_list).
static void draw_list_range(const DrawList *dl, int from, int to) {
    static const SceGxmStencilOp ops[] = {SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_INCR, SCE_GXM_STENCIL_OP_DECR};
    if (!g_indices) return;
    SceGxmContext *ctx = vita2d_get_context();
    uint32_t *cv = NULL;   // this frame's recoloured colours, where the GPU can read them
    if (dl->ncv) {
        cv = vita2d_pool_memalign(dl->ncv * sizeof(uint32_t), sizeof(uint32_t));
        if (!cv) return;   // temp pool exhausted; skip rather than crash
        memcpy(cv, dl->cv, dl->ncv * sizeof(uint32_t));
    }
    int textured = -1;   // which program pair is set: 0 colour, 1 texture, 2 colour with its own colour stream
    for (int c = 0; c < dl->ncmd; c++) {
        const DrawCmd *cmd = &dl->cmd[c];
        if (!cmd->count) continue;
        int k0 = cmd->start < from ? from : cmd->start, k1 = cmd->start + cmd->count > to ? to : cmd->start + cmd->count;
        if (k0 >= k1) continue;
        set_stencil(SCE_GXM_STENCIL_FUNC_EQUAL, cmd->ref, ops[cmd->op]);
        for (int k = k0; k < k1; k++) {
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
            const Mesh *mesh = it->mesh;
            const uint32_t *colours = it->col ? it->col : it->cv >= 0 && cv ? cv + it->cv : NULL;
            const Vertex *v = it->vp;
            if (!v || (colours && !g_split_vp)) {
                // not uploaded (or no split program): whole vertices for this frame
                Vertex *t = vita2d_pool_memalign(mesh->nverts * sizeof(Vertex), sizeof(Vertex));
                if (!t) continue;
                for (int i = 0; i < mesh->nverts; i++) {
                    if (mesh->xy) { t[i].x = mesh->xy[i * 2]; t[i].y = mesh->xy[i * 2 + 1]; }
                    else { t[i].x = it->vp[i].x; t[i].y = it->vp[i].y; }
                    t[i].z = 0.5f;
                    if (colours) t[i].color = colours[i];
                    else memcpy(&t[i].color, mesh->rgba + i * 4, 4);
                }
                v = t;
                colours = NULL;
            }
            int want = colours ? 2 : 0;
            if (textured != want) {
                sceGxmSetVertexProgram(ctx, want == 2 ? g_split_vp : _vita2d_colorVertexProgram);
                sceGxmSetFragmentProgram(ctx, _vita2d_colorFragmentProgram);
                textured = want;
            }
            for (int off = 0; off < mesh->nverts; off += MAX_BATCH) {
                int count = mesh->nverts - off < MAX_BATCH ? mesh->nverts - off : MAX_BATCH;
                void *ub;
                sceGxmReserveVertexDefaultUniformBuffer(ctx, &ub);
                sceGxmSetUniformDataF(ub, _vita2d_colorWvpParam, 0, 16, wvp);
                sceGxmSetVertexStream(ctx, 0, v + off);
                if (colours) sceGxmSetVertexStream(ctx, 1, colours + off);
                sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g_indices, count);
            }
        }
    }
    set_stencil(SCE_GXM_STENCIL_FUNC_ALWAYS, 1, SCE_GXM_STENCIL_OP_KEEP);
}

static void draw_list(const DrawList *dl) { draw_list_range(dl, 0, dl->n); }

static vita2d_pgf *g_font;

// ---- heat haze --------------------------------------------------------------------------
// A clip the game gave AddShader("Heat") (lava world's backdrop) shimmers. Its own items go
// into a screen-sized texture first; then the screen gets what's before it, that texture in
// thin rows each shifted sideways by a moving wave, and the rest of the frame on top.
// Starts the screen's drawing either way.

static vita2d_texture *g_heat_rt;

static void draw_frame(const DrawList *vb, unsigned clock) {
    if (vb->heat_at < 0 || vb->heat_end <= vb->heat_at ||
        (!g_heat_rt && !(g_heat_rt = vita2d_create_empty_texture_rendertarget(SCREEN_W, SCREEN_H,
                                                                                SCE_GXM_TEXTURE_FORMAT_A8B8G8R8)))) {
        vita2d_start_drawing();
        vita2d_clear_screen();
        draw_list(vb);
        return;
    }
    unsigned clear = 0;
    vita2d_start_drawing_advanced(g_heat_rt, 0);
    vita2d_set_clear_color(RGBA8(0, 0, 0, 0));
    vita2d_clear_screen();
    draw_list_range(vb, vb->heat_at, vb->heat_end);
    vita2d_end_drawing();
    vita2d_wait_rendering_done();   // the texture is read by the next scene
    (void)clear;
    vita2d_start_drawing();
    vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
    vita2d_clear_screen();
    draw_list_range(vb, 0, vb->heat_at);
    int x0 = vb->heat[0] < 0 ? 0 : (int)vb->heat[0], x1 = vb->heat[2] > SCREEN_W ? SCREEN_W : (int)vb->heat[2];
    int y0 = vb->heat[1] < 0 ? 0 : (int)vb->heat[1], y1 = vb->heat[3] > SCREEN_H ? SCREEN_H : (int)vb->heat[3];
    for (int y = y0; y < y1; y += 4) {
        float dx = 2.5f * sinf(clock * 0.21f + y * 0.09f) + 1.5f * sinf(clock * 0.13f + y * 0.023f);
        int h = y + 4 > y1 ? y1 - y : 4;
        vita2d_draw_texture_part(g_heat_rt, x0 + dx, y, x0, y, x1 - x0, h);
    }
    draw_list_range(vb, vb->heat_end, vb->n);
}

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
// Each file is parsed and uploaded once and kept while it's in use: the game loads
// player.swf into every player slot and fx.swf into every effect. Files nothing uses any
// more (levels left behind, the title's menu) are freed by free_unused_movies.
#define MAX_MOVIES 160
static struct { char name[64]; Movie *m; GpuBlock gpu; int keep; } g_movies[MAX_MOVIES];   // keep: 1 always, 2 until first used (preloaded)
static int g_nmovies;
static char g_last_error[256];

// ---- the log file -------------------------------------------------------------------
// ux0:data/CastleCrashers/log.txt: each distinct warning once, the files loaded and freed,
// and slow frames, for bugs that only show on the Vita. The run
// before is kept as log_prev.txt. Lines collect in memory and a thread of their own writes them
// to the card (log_flush hands them over): a write from the game's thread cost frames, and
// while the card is busy (a level's sounds loading) one took 370 ms every flush.
#define LOG_PATH DATA_DIR "log.txt"
#define LOG_MAX (4 << 20)
static SceUID g_log = -1, g_log_sema = -1;
static int g_log_bytes, g_log_used;
static char g_log_buf[16384], g_log_out[16384];
static volatile int g_log_out_n;   // bytes handed to the writer; 0 once written
static uint32_t g_log_seen[2048];
static int g_log_nseen;

static int log_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    for (;;) {
        sceKernelWaitSema(g_log_sema, 1, NULL);
        if (g_log_out_n) sceIoWrite(g_log, g_log_out, g_log_out_n);
        __atomic_store_n(&g_log_out_n, 0, __ATOMIC_SEQ_CST);
    }
    return 0;
}

static void log_open(void) {
    sceIoRemove(DATA_DIR "log_prev.txt");
    sceIoRename(LOG_PATH, DATA_DIR "log_prev.txt");
    g_log = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    g_log_sema = sceKernelCreateSema("cc_log", 0, 0, 1, NULL);
    SceUID t = sceKernelCreateThread("cc_log", log_thread, 0x10000100 + 20, 0x4000, 0, 0, NULL);
    if (g_log_sema < 0 || t < 0 || sceKernelStartThread(t, 0, NULL) < 0) g_log_sema = -1;
}

// Hands what has collected to the writer. While it's still writing the last lot, lines keep
// collecting (it waits only when the buffer is nearly full).
static void log_flush(void) {
    if (g_log < 0 || !g_log_used) return;
    if (g_log_sema < 0) {   // no thread: write here
        sceIoWrite(g_log, g_log_buf, g_log_used);
        g_log_used = 0;
        return;
    }
    if (__atomic_load_n(&g_log_out_n, __ATOMIC_SEQ_CST)) {
        if (g_log_used < (int)sizeof g_log_buf - 640) return;
        while (__atomic_load_n(&g_log_out_n, __ATOMIC_SEQ_CST)) sceKernelDelayThread(1000);
    }
    memcpy(g_log_out, g_log_buf, g_log_used);
    __atomic_store_n(&g_log_out_n, g_log_used, __ATOMIC_SEQ_CST);
    g_log_used = 0;
    sceKernelSignalSema(g_log_sema, 1);
}

// before quitting: everything on the card
static void log_flush_wait(void) {
    log_flush();
    while (g_log_sema >= 0 && __atomic_load_n(&g_log_out_n, __ATOMIC_SEQ_CST)) sceKernelDelayThread(1000);
    log_flush();
    while (g_log_sema >= 0 && __atomic_load_n(&g_log_out_n, __ATOMIC_SEQ_CST)) sceKernelDelayThread(1000);
}

static void log_line(const char *fmt, ...);
// Exit to Main Menu steps, on the card at once (a freeze there left no trace)
#define EXIT_TRACE(...) do { log_line(__VA_ARGS__); log_flush_wait(); } while (0)

static void log_line(const char *fmt, ...) {
    if (g_log < 0 || g_log_bytes > LOG_MAX) return;
    char buf[640];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n';
    if (g_log_used + n > (int)sizeof g_log_buf) log_flush();
    memcpy(g_log_buf + g_log_used, buf, n);
    g_log_used += n;
    g_log_bytes += n;
}

// 1 the first time a message is seen; numbers don't count (fx201, fx202... are one message)
static int log_first(const char *msg) {
    uint32_t h = 2166136261u;
    for (const char *c = msg; *c; c++)
        if (*c < '0' || *c > '9') h = (h ^ (uint8_t)*c) * 16777619u;
    for (int i = 0; i < g_log_nseen; i++)
        if (g_log_seen[i] == h) return 0;
    if (g_log_nseen < (int)(sizeof g_log_seen / sizeof g_log_seen[0])) g_log_seen[g_log_nseen++] = h;
    return 1;
}

// the recolour cache, for freeing files from inside a load (main sets these)
static RecolourCache *g_cache;
static void *g_cache_mem;
static size_t g_cache_bytes;
static int free_unused_movies(AsVm *vm, RecolourCache *cache, void *cache_mem, size_t cache_bytes);

// swf_bitmap_loaded: each bitmap becomes a texture as soon as it's decoded, and its pixels are
// then only on the GPU side
static void bitmap_to_texture(Bitmap *bm) {
    vita2d_texture *tex = vita2d_create_empty_texture_format(bm->w, bm->h, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
    if (!tex) return;
    uint8_t *dst = vita2d_texture_get_datap(tex);
    unsigned stride = vita2d_texture_get_stride(tex);
    for (int y = 0; y < bm->h; y++) memcpy(dst + y * stride, bm->rgba + (size_t)y * bm->w * 4, (size_t)bm->w * 4);
    bm->tex = tex;
    free(bm->rgba);
    bm->rgba = NULL;
}

// ux0:data/CastleCrashers/{game,levels}/<base>: 1 and the path if it's there
static int find_file(const char *base, char *path, size_t n, int *is_level) {
    static const char *dirs[] = {"game", "levels"};
    for (int i = 0; i < 2; i++) {
        snprintf(path, n, DATA_DIR "%s/%s", dirs[i], base);
        SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
        if (fd < 0) continue;
        sceIoClose(fd);
        // levels/ (and All You Can Quaff, whose scripts run the developer's quick start where
        // it's false): console_version reads true in its scripts (cc_host.c)
        *is_level = i == 1 || !strcasecmp(base, "QUOFF.swf");
        return 1;
    }
    return 0;
}

// the CPU side of a load: parse, repair, PlayStation buttons, tessellate (no GPU work but the
// bitmaps' textures, which swf_bitmap_loaded makes as they are decoded)
static Movie *prepare_movie(const char *path, int is_level, char *err, size_t n) {
    Movie *m = swf_load(path, err, n);
    if (!m) return NULL;
    m->is_level = is_level;
    cc_repair_level(m);
    cc_buttons_playstation(m, CC_BTN_STYLE_GREY);   // the game's button art as PlayStation buttons
    cc_menu_vita_badge(m);                              // "Vita Edition" on the title
    if (CC_BETA_STICKER) cc_menu_beta_sticker_build(m);  // and "BETA" on Multiplayer
    render_tessellate_all(m);
    return m;
}

// uploads its meshes and keeps it
static Movie *register_movie(const char *base, const char *path, Movie *m) {
    log_line("load %s%s (heap %u KB used)", path, m->truncated ? " (cut short)" : "",
             (unsigned)(mallinfo().uordblks >> 10));
    log_flush();   // loads and frees go to the writer at once: a crash right after keeps them
    GpuBlock *blk = &g_movies[g_nmovies].gpu;
    blk->uid = -1;
    if (render_upload(m, mesh_alloc, blk)) render_release_cpu(m);
    snprintf(g_movies[g_nmovies].name, sizeof g_movies[0].name, "%s", base);
    g_movies[g_nmovies].keep = 0;
    g_movies[g_nmovies++].m = m;
    return m;
}

// a big file (a sky is up to 34 MB of pixels) after a long session: make room first
static void make_room(AsVm *vm) {
    if (vm && g_cache && mallinfo().uordblks > (size_t)_newlib_heap_size_user / 2)
        free_unused_movies(vm, g_cache, g_cache_mem, g_cache_bytes);
}

// ---- loading in the background ----
// The original keeps the game (and its loading screen, the knight walking) running while a
// file loads. Here one file at a time is parsed and tessellated on a thread of its own while
// the scripts hold its loadMovie (vm->prefetch); the upload and the bookkeeping stay on this
// thread. vita_loader picks the finished file up.
enum { BG_IDLE, BG_RUNNING, BG_DONE };
static struct {
    char base[64], path[256], err[256];
    int is_level, preload;
    volatile int state;
    Movie *m;
} g_bg;
static SceUID g_bg_sema = -1;

static SceUID g_main_thread;
static int thread_slot(void) { return sceKernelGetThreadId() != g_main_thread; }

static int bg_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    for (;;) {
        sceKernelWaitSema(g_bg_sema, 1, NULL);
        g_bg.m = prepare_movie(g_bg.path, g_bg.is_level, g_bg.err, sizeof g_bg.err);
        __sync_synchronize();
        g_bg.state = BG_DONE;
    }
    return 0;
}

static void bg_init(void) {
    g_main_thread = sceKernelGetThreadId();
    render_thread_slot = thread_slot;
    g_bg_sema = sceKernelCreateSema("cc_load", 0, 0, 1, NULL);
    SceUID th = sceKernelCreateThread("cc_load", bg_thread, 0x10000100 + 20, 1024 * 1024, 0, 0, NULL);
    if (g_bg_sema < 0 || th < 0 || sceKernelStartThread(th, 0, NULL) < 0) g_bg_sema = -1;
}

// the running job's file, once the thread is done with it: kept, or NULL (failed)
static Movie *bg_finish(void) {
    while (g_bg.state == BG_RUNNING) sceKernelDelayThread(1000);
    __sync_synchronize();
    Movie *m = g_bg.m;
    g_bg.state = BG_IDLE;
    if (!m) {
        log_line("background load %s failed (%s): trying again", g_bg.path, g_bg.err);
        return NULL;
    }
    if (g_nmovies == MAX_MOVIES) { swf_free(m); return NULL; }
    m = register_movie(g_bg.base, g_bg.path, m);
    if (g_bg.preload) g_movies[g_nmovies - 1].keep = 2;   // not freed before the game asks for it
    return m;
}

static Movie *cached_movie(const char *base) {
    for (int i = 0; i < g_nmovies; i++)
        if (!strcasecmp(g_movies[i].name, base)) {
            if (g_movies[i].keep == 2) g_movies[i].keep = 0;   // a preloaded file in use: an ordinary one now
            return g_movies[i].m;
        }
    return NULL;
}

static int cached_movie_peek(const char *base) {
    for (int i = 0; i < g_nmovies; i++)
        if (!strcasecmp(g_movies[i].name, base)) return 1;
    return 0;
}

// ---- preloading ----
// The files the next screens need, read on the loading thread while nothing else loads, in the
// order they're needed: the title's during the intro, the game's start and the character
// select's on the title, the game's (effects, players, animals, the map) after that. Each took
// the Vita seconds with the screen standing still (fx, player and animals 15 s). A file the
// game asks for while another one is loading waits for it, so nothing is read ahead of its stage.
static const char *const g_preload[] = {"MENU.swf",                                              // intro
                                        "LOADING.swf", "PAUSE.swf", "sounds.swf", "lobby.swf",   // title
                                        "fx.swf", "player.swf", "animals.swf", "map.swf"};       // game
enum { PRELOAD_INTRO = 1, PRELOAD_TITLE = 5, PRELOAD_GAME = 9 };
static int g_npreloaded;   // of g_preload, started

static void preload_step(int upto) {   // upto: the PRELOAD_ stage
    if (g_bg_sema < 0) return;
    if (g_bg.state == BG_DONE && g_bg.preload) bg_finish();
    if (g_bg.state != BG_IDLE || g_npreloaded >= upto) return;
    if (mallinfo().uordblks > (size_t)_newlib_heap_size_user * 6 / 10) return;   // room for the game first
    const char *base = g_preload[g_npreloaded++];
    char path[256];
    int is_level;
    if (cached_movie_peek(base) || g_nmovies == MAX_MOVIES || !find_file(base, path, sizeof path, &is_level)) return;
    snprintf(g_bg.base, sizeof g_bg.base, "%s", base);
    snprintf(g_bg.path, sizeof g_bg.path, "%s", path);
    g_bg.is_level = is_level;
    g_bg.preload = 1;
    g_bg.m = NULL;
    g_bg.state = BG_RUNNING;
    __sync_synchronize();
    sceKernelSignalSema(g_bg_sema, 1);
}

static int vita_prefetch(AsVm *vm, const char *url) {
    const char *base = strrchr(url, '/');
    base = base ? base + 1 : url;
    if (g_bg_sema < 0 || cached_movie(base)) return 0;
    if (g_bg.state != BG_IDLE) {
        if (strcasecmp(g_bg.base, base)) {   // another file's job (its load was dropped): finish it
            if (g_bg.state == BG_DONE) bg_finish();
            return 1;
        }
        if (g_bg.state == BG_RUNNING) return 1;
        bg_finish();   // done: a failure falls back to vita_loader's own load
        return 0;
    }
    char path[256];
    int is_level;
    if (g_nmovies == MAX_MOVIES || !find_file(base, path, sizeof path, &is_level)) return 0;
    make_room(vm);
    snprintf(g_bg.base, sizeof g_bg.base, "%s", base);
    snprintf(g_bg.path, sizeof g_bg.path, "%s", path);
    g_bg.is_level = is_level;
    g_bg.preload = 0;
    g_bg.m = NULL;
    g_bg.state = BG_RUNNING;
    __sync_synchronize();
    sceKernelSignalSema(g_bg_sema, 1);
    return 1;
}

static Movie *vita_loader(AsVm *vm, const char *url) {
    const char *base = strrchr(url, '/');
    base = base ? base + 1 : url;
    base = cc_level_file(base);   // Paint Jr.'s castle (paint_jr.h)
    Movie *m = cached_movie(base);
    if (m) return m;
    if (g_bg.state != BG_IDLE) {   // never two loads at once (the tessellator is shared)
        int same = !strcasecmp(g_bg.base, base);
        m = bg_finish();
        if (same && m) return cached_movie(base);
    }
    if (g_nmovies == MAX_MOVIES) return NULL;
    char path[256], err[256];
    int is_level;
    if (!find_file(base, path, sizeof path, &is_level)) {
        snprintf(g_last_error, sizeof g_last_error, "missing %s", base);
        log_line("load %s: MISSING", base);
        return NULL;
    }
    make_room(vm);   // the last frame stays up meanwhile; the game shows its own loading screen
    m = prepare_movie(path, is_level, err, sizeof err);
    if (!m && vm && g_cache && free_unused_movies(vm, g_cache, g_cache_mem, g_cache_bytes))
        m = prepare_movie(path, is_level, err, sizeof err);   // out of memory: try again without the old files
    if (!m) {
        snprintf(g_last_error, sizeof g_last_error, "%s: %s", base, err);
        log_line("load %s FAILED: %s", path, err);
        return NULL;
    }
    return register_movie(base, path, m);
}

// a file kept for the rest of a game (paint_jr.c's part files: it holds their sprites); keep 3
// goes back to 0 when the next game starts (release_game_movies)
static void keep_for_game(Movie *m) {
    for (int i = 0; i < g_nmovies; i++)
        if (g_movies[i].m == m && !g_movies[i].keep) g_movies[i].keep = 3;
}

static void release_game_movies(void) {
    for (int i = 0; i < g_nmovies; i++)
        if (g_movies[i].keep == 3) g_movies[i].keep = 0;
}

// a file the port itself draws from (MAIN, the pause book): never freed
static Movie *host_movie(const char *name) {
    Movie *m = vita_loader(NULL, name);
    for (int i = 0; m && i < g_nmovies; i++)
        if (g_movies[i].m == m) g_movies[i].keep = 1;
    return m;
}

// Frees the files no clip or script function of the game belongs to any more. Their meshes
// may be in the last frame's draw list and the recolour cache, so the GPU is let finish and
// the cache starts over; the caller builds a new frame before drawing again. Returns how many.
static int free_unused_movies(AsVm *vm, RecolourCache *cache, void *cache_mem, size_t cache_bytes) {
    Movie *ms[MAX_MOVIES];
    uint8_t used[MAX_MOVIES];
    for (int i = 0; i < g_nmovies; i++) ms[i] = g_movies[i].m;
    if (!vm) memset(used, 0, sizeof used);   // no game any more: all but the kept files
    else if (!as_movies_used(vm, ms, g_nmovies, used)) return 0;
    int unused = 0;
    for (int i = 0; i < g_nmovies; i++) unused += !used[i] && !g_movies[i].keep;
    if (!unused) return 0;
    vita2d_wait_rendering_done();
    recolour_cache_free(cache);
    recolour_cache_init(cache, cache_mem, cache_bytes);
    int n = 0;
    for (int i = 0; i < g_nmovies; i++) {
        if (used[i] || g_movies[i].keep) { g_movies[n++] = g_movies[i]; continue; }
        Movie *m = g_movies[i].m;
        log_line("free %s", g_movies[i].name);
        for (int c = 0; c < 65536; c++)
            if (m->chars[c].type == CHAR_BITMAP && ((Bitmap *)m->chars[c].ptr)->tex)
                vita2d_free_texture(((Bitmap *)m->chars[c].ptr)->tex);
        as_forget_movie(m);
        swf_free(m);
        gpu_free(&g_movies[i].gpu);
    }
    g_nmovies = n;
    log_flush();
    return unused;
}

// ---- controls -> the game's buttons (cc_port_keys gives them port 1's key codes) -------

#define STICK_DEAD 40   // of 128: below this the stick is centred
#define STICK_RUN 100   // at or beyond this it's a full tilt

static uint32_t pad_buttons(const SceCtrlData *pad, int in_game) {
    uint32_t k = 0;
    unsigned b = pad->buttons;
    int lx = pad->lx - 128, ly = pad->ly - 128;
    // the console scripts run on a full tilt and walk on a light one; the D-pad runs. Outside a
    // level (map, stat screen, menus) only the full directions mean anything: any tilt is one.
    int run = in_game ? STICK_RUN : STICK_DEAD;
    if ((b & SCE_CTRL_LEFT) || lx <= -run) k |= 1u << CCB_LEFT;
    else if (lx <= -STICK_DEAD) k |= 1u << CCB_WALK_LEFT;
    if ((b & SCE_CTRL_RIGHT) || lx >= run) k |= 1u << CCB_RIGHT;
    else if (lx >= STICK_DEAD) k |= 1u << CCB_WALK_RIGHT;
    if ((b & SCE_CTRL_UP) || ly <= -run) k |= 1u << CCB_UP;
    else if (ly <= -STICK_DEAD) k |= 1u << CCB_WALK_UP;
    if ((b & SCE_CTRL_DOWN) || ly >= run) k |= 1u << CCB_DOWN;
    else if (ly >= STICK_DEAD) k |= 1u << CCB_WALK_DOWN;
    // the right stick stands in for L2/R2 (switching items). The script steps once per
    // press and waits for the release, so a flick past the deadzone is one step and the
    // stick re-arms when it comes back to the centre.
    int rx = pad->rx - 128;
    if (rx <= -STICK_RUN + 20) k |= 1u << CCB_L2;
    if (rx >= STICK_RUN - 20) k |= 1u << CCB_R2;
    // menus always accept on Cross and go back on Circle; the actions are on whichever button
    // the player put them (cc_bind; by default the PlayStation layout: block on L, magic on R)
    static const unsigned PAD[CC_ACTIONS] = {SCE_CTRL_SQUARE, SCE_CTRL_TRIANGLE, SCE_CTRL_CROSS,
                                             SCE_CTRL_CIRCLE, SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER};
    static const int ACTION[CC_ACTIONS] = {CCB_PUNCH1, CCB_PUNCH2, CCB_JUMP, CCB_PROJECTILE, CCB_SHIELD, CCB_MAGIC};
    if (b & SCE_CTRL_CROSS) k |= 1u << CCB_ACCEPT;
    if (b & SCE_CTRL_CIRCLE) k |= 1u << CCB_BACK;
    for (int a = 0; a < CC_ACTIONS; a++)
        if (b & PAD[cc_bind[a]]) k |= 1u << ACTION[a];
    if (b & SCE_CTRL_START) k |= 1u << CCB_START;
    return k;
}

// ---- more controllers: a PS TV's paired pads, or a plugin's (ds34vita) ---------------
// The ports as SDL reads them: port 0 is the Vita's own buttons (on a PS TV the first pad's
// too), port 1 the first pad, ports 2-4 players 2-4. Player 1 is ports 0 and 1 together, in
// case a plugin doesn't mirror its first pad on port 0. The Ext2 reads tell L1/R1 from L2/R2.

#define LOCAL_PADS 4
static SceCtrlData g_pads[LOCAL_PADS];   // this frame's, player 1 to 4
static int g_pad_on[LOCAL_PADS];         // paired (player 1 always)

// an Ext2 read as the Vita's own: L1/R1 are its L/R (block, magic); L2/R2 switch items as the
// right stick's flick does, so they push it to the side
static int pad_read_ext(int port, SceCtrlData *p) {
    if (sceCtrlPeekBufferPositiveExt2(port, p, 1) < 0) return 0;
    unsigned b = p->buttons, o = b & ~(SCE_CTRL_L1 | SCE_CTRL_R1 | SCE_CTRL_L2 | SCE_CTRL_R2 | SCE_CTRL_L3 | SCE_CTRL_R3);
    if (b & SCE_CTRL_L1) o |= SCE_CTRL_LTRIGGER;
    if (b & SCE_CTRL_R1) o |= SCE_CTRL_RTRIGGER;
    if (b & SCE_CTRL_L2) p->rx = 0;
    if (b & SCE_CTRL_R2) p->rx = 255;
    p->buttons = o;
    return 1;
}

// player 1 into *p1 and g_pads[0]; players 2-4 into g_pads[] while their pads are paired
// (not in a Wi-Fi game, where each Vita is one player)
static void pads_read(SceCtrlData *p1, int extra) {
    static int frames;
    static SceCtrlPortInfo info;
    if (frames++ % 30 == 0 && sceCtrlGetControllerPortInfo(&info) < 0) memset(&info, 0, sizeof info);
    // the sticks from the plain read only, as before more pads (with the Ext2 reads' sticks
    // they did nothing in a level on Jah's Vita); Ext2 only gives the buttons (L1/R1, L2/R2)
    sceCtrlPeekBufferPositive(0, p1, 1);
    SceCtrlData q;
    if (pad_read_ext(0, &q)) {
        p1->buttons = q.buttons;
        if (q.rx == 0 || q.rx == 255) p1->rx = q.rx;   // L2/R2
    }
    // port 1's buttons only when it's a paired pad (on a handheld it's the Vita's own again)
    if ((info.port[1] == SCE_CTRL_TYPE_DS3 || info.port[1] == SCE_CTRL_TYPE_DS4) && pad_read_ext(1, &q)) {
        p1->buttons |= q.buttons;
        if (q.rx == 0 || q.rx == 255) p1->rx = q.rx;
    }
    g_pads[0] = *p1;
    g_pad_on[0] = 1;
    for (int i = 1; i < LOCAL_PADS; i++) {
        g_pad_on[i] = extra && info.port[i + 1] != SCE_CTRL_TYPE_UNPAIRED && pad_read_ext(i + 1, &g_pads[i]);
        if (!g_pad_on[i]) {
            memset(&g_pads[i], 0, sizeof g_pads[i]);
            g_pads[i].lx = g_pads[i].ly = g_pads[i].rx = g_pads[i].ry = 128;
        }
    }
}

// menus: the buttons pressed since last frame, with the stick moving once per push too
static unsigned pad_nav(const SceCtrlData *pad, const SceCtrlData *old) {
    unsigned nav = pad->buttons & ~old->buttons;
    if (pad->ly < 48 && old->ly >= 48) nav |= SCE_CTRL_UP;
    if (pad->ly > 208 && old->ly <= 208) nav |= SCE_CTRL_DOWN;
    if (pad->lx < 48 && old->lx >= 48) nav |= SCE_CTRL_LEFT;
    if (pad->lx > 208 && old->lx <= 208) nav |= SCE_CTRL_RIGHT;
    return nav;
}

// ---- pause menu ----------------------------------------------------------------------
// The original draws its pause menu in castle.exe (PAUSE.swf only holds the art) and
// tells the scripts through g_nPort<n>State: 2 paused, 1 playing, 3 quit to the map.

typedef struct {
    int open;
    int select;                // opened at character select: no player state to set
    PauseMenu m;               // the menu (pause_menu.c)
    DrawList ui;               // this frame's backdrop, words and button prompts
    DrawList art;              // the pause book (controls and achievements pages)
    int have_art;
    float book[4];             // where the book is on screen
    HelpBook hb;               // Help & Options' book, while the menu is on PG_BOOK
    DrawList hb_ui;
} Pause;

// Settings' volumes, 0..HB_VOLUME_STEPS, and Auto Potion: kept in settings.txt as "music = N",
// "sfx = N" and "autopotion = 0/1"; the Controls page's buttons as "buttons = " and cc_bind's
// six numbers
static int g_volume[2] = {HB_VOLUME_STEPS, HB_VOLUME_STEPS};
static int g_auto_potion;
static int g_fresh;   // "fresh = 0/1": the FRESH look (paint_jr.h)

static void settings_apply(void) {
    audio_set_volume(g_volume[0] / (float)HB_VOLUME_STEPS, g_volume[1] / (float)HB_VOLUME_STEPS);
}

static void settings_load(void) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (f) {
        char line[64];
        int v;
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, " music = %d", &v) == 1 && v >= 0 && v <= HB_VOLUME_STEPS) g_volume[0] = v;
            if (sscanf(line, " sfx = %d", &v) == 1 && v >= 0 && v <= HB_VOLUME_STEPS) g_volume[1] = v;
            if (sscanf(line, " autopotion = %d", &v) == 1) g_auto_potion = v != 0;
            if (sscanf(line, " fresh = %d", &v) == 1) g_fresh = v != 0;
            int nb[CC_ACTIONS], used = 0;
            if (sscanf(line, " buttons = %d %d %d %d %d %d", &nb[0], &nb[1], &nb[2], &nb[3], &nb[4], &nb[5]) ==
                CC_ACTIONS) {
                for (int a = 0; a < CC_ACTIONS; a++)
                    if (nb[a] >= 0 && nb[a] < CC_ACTIONS) used |= 1 << nb[a];
                if (used == (1 << CC_ACTIONS) - 1) memcpy(cc_bind, nb, sizeof nb);   // each button once
            }
        }
        fclose(f);
    }
    settings_apply();
}

static void settings_save(void) {
    FILE *f = fopen(SETTINGS_PATH, "w");
    if (!f) return;
    fprintf(f, "music = %d\nsfx = %d\nautopotion = %d\nfresh = %d\n", g_volume[0], g_volume[1], g_auto_potion,
            g_fresh);
    fprintf(f, "buttons = %d %d %d %d %d %d\n", cc_bind[0], cc_bind[1], cc_bind[2], cc_bind[3], cc_bind[4],
            cc_bind[5]);
    fclose(f);
}

static int in_level(AsVm *vm) {
    int found;
    AsVal g = as_resolve_path(vm, vm->root, "loader.game.game", &found);
    return found && g.t == V_OBJ && g.o->type == OBJ_MC;
}

// the world map (MAP.swf in the level slot): the executable's pause menu opens there too
static int in_loader(AsVm *vm, const char *file) {
    int found;
    AsVal l = as_resolve_path(vm, vm->root, "loader", &found);
    Movie *m = cached_movie(file);
    return found && m && l.t == V_OBJ && l.o->type == OBJ_MC && ((AsMc *)l.o)->movie == m;
}

static int in_map(AsVm *vm) { return in_loader(vm, "MAP.swf"); }

// character select (lobby.swf in the loader): Start opens the pause menu there too, a knight is
// picked with Cross
static int in_select(AsVm *vm) {
    int found;
    AsVal s = as_resolve_path(vm, vm->root, "loader.selectscreen", &found);
    return found && s.t == V_OBJ && s.o->type == OBJ_MC;
}

// the pause book from PAUSE.swf, centred, about 60% of the screen wide
static void pause_build_art(Pause *ps) {
    Movie *pm = host_movie("PAUSE.swf");
    ps->have_art = 0;
    if (!pm) return;
    int cid = -1;
    for (int c = 0; c < 65536 && cid < 0; c++)
        if (pm->chars[c].type == CHAR_SPRITE && pm->chars[c].export && !strcmp(pm->names[pm->chars[c].export], "bookbg"))
            cid = c;
    if (cid < 0) return;
    Mat id = {1, 0, 0, 1, 0, 0};
    render_begin(&ps->art);
    ps->art.view[2] = 0;   // no culling while measuring
    render_char(pm, cid, id, CX_IDENTITY, 0, &ps->art);
    float b[4] = {1e30f, 1e30f, -1e30f, -1e30f};
    for (int k = 0; k < ps->art.n; k++) {
        const float *mb = ps->art.items[k].mesh->bounds;
        Mat mm = ps->art.items[k].m;
        for (int q = 0; q < 4; q++) {
            float x = mb[q & 1 ? 2 : 0], y = mb[q & 2 ? 3 : 1];
            float px = mm.a * x + mm.c * y + mm.tx, py = mm.b * x + mm.d * y + mm.ty;
            if (px < b[0]) b[0] = px;
            if (px > b[2]) b[2] = px;
            if (py < b[1]) b[1] = py;
            if (py > b[3]) b[3] = py;
        }
    }
    if (b[2] <= b[0]) return;
    float s = SCREEN_W * 0.6f / (b[2] - b[0]);
    if ((b[3] - b[1]) * s > SCREEN_H * 0.8f) s = SCREEN_H * 0.8f / (b[3] - b[1]);
    float w = (b[2] - b[0]) * s, h = (b[3] - b[1]) * s;
    Mat m = {s, 0, 0, s, (SCREEN_W - w) / 2 - b[0] * s, (SCREEN_H - h) / 2 - b[1] * s};
    render_begin(&ps->art);
    ps->art.view[2] = 0;
    render_char(pm, cid, m, CX_IDENTITY, 0, &ps->art);
    ps->book[0] = (SCREEN_W - w) / 2;
    ps->book[1] = (SCREEN_H - h) / 2;
    ps->book[2] = ps->book[0] + w;
    ps->book[3] = ps->book[1] + h;
    ps->have_art = 1;
}

// Wi-Fi play: this Vita's player's state goes to every Vita with the next buttons (g_net_state:
// the state + 1, 0 none waiting)
static int g_netgame, g_net_state;

static int g_pause_port = 1;   // the local player whose Start opened the menu

static void pause_set_state(AsVm *vm, int state) {
    if (g_netgame) { g_net_state = state + 1; return; }
    char name[32];
    snprintf(name, sizeof name, "g_nPort%dState", g_pause_port);
    cc_set_flash_global(vm, name, as_num(state));
}

// the achievements page over the pause book
static void pause_book_draw(Pause *ps, int page, int dim) {
    if (dim) vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 140));
    float x0 = SCREEN_W * 0.3f, y0 = SCREEN_H * 0.3f, x1 = SCREEN_W * 0.7f;
    if (ps->have_art) {
        draw_list(&ps->art);
        x0 = ps->book[0] + (ps->book[2] - ps->book[0]) * 0.18f;
        x1 = ps->book[2] - (ps->book[2] - ps->book[0]) * 0.18f;
        y0 = ps->book[1] + (ps->book[3] - ps->book[1]) * 0.25f;
    } else {
        vita2d_draw_rectangle(x0 - 20, y0 - 60, x1 - x0 + 40, 260, RGBA8(237, 222, 180, 255));
    }
    unsigned ink = RGBA8(70, 45, 20, 255), hi = RGBA8(190, 40, 30, 255);
    if (page == PG_ACHIEVEMENTS) {
        unsigned got = cc_achievements(), dimmed = RGBA8(150, 130, 100, 255);
        int n = 0;
        for (int id = 1; id <= CC_ACHIEVEMENTS; id++) n += got >> id & 1;
        vita2d_pgf_draw_textf(g_font, x0, y0 - 30, ink, 1.0f, "Achievements  %d / %d", n, CC_ACHIEVEMENTS);
        for (int id = 1; id <= CC_ACHIEVEMENTS; id++)
            vita2d_pgf_draw_textf(g_font, x0, y0 + (id - 1) * 21, got >> id & 1 ? ink : dimmed, 0.75f, "%s %s",
                                  got >> id & 1 ? "[x]" : "[  ]", cc_achievement_name(id));
        vita2d_pgf_draw_text(g_font, x0, y0 + CC_ACHIEVEMENTS * 21 + 10, hi, 0.9f, "Circle: back");
    }
}

// the menu over the frozen game, or a book page over its backdrop
static void pause_draw(Pause *ps) {
    Movie *pm = host_movie("PAUSE.swf");
    render_begin(&ps->ui);
    ps->ui.view[2] = SCREEN_W;
    ps->ui.view[3] = SCREEN_H;
    if (pm) pause_menu_build(&ps->m, pm, SCREEN_W / 848.0f, &ps->ui);
    else vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, RGBA8(0, 0, 0, 200));
    draw_list(&ps->ui);
    if (ps->m.page == PG_CONTROLS || ps->m.page == PG_ACHIEVEMENTS) pause_book_draw(ps, ps->m.page, 0);
    if (ps->m.page == PG_BOOK && pm && !ps->m.book_new) {
        render_begin(&ps->hb_ui);
        ps->hb_ui.view[2] = SCREEN_W;
        ps->hb_ui.view[3] = SCREEN_H;
        help_book_build(&ps->hb, pm, SCREEN_W / 848.0f, &ps->hb_ui);
        draw_list(&ps->hb_ui);
    }
}

// Help & Options' book: L/R (or left/right) turn How to Play's pages, up/down and left/right
// set the volumes and move the Controls page's actions (Triangle puts them back), Circle
// closes it (the settings are saved then if they moved)
static void help_input(Pause *ps, unsigned pressed, float dt) {
    if (ps->m.book_new) {
        help_book_open(&ps->hb, ps->m.book, g_volume[0], g_volume[1], g_auto_potion,
                       cc_fresh_available() ? g_fresh : -1);
        ps->hb.achieved = cc_achievements();
        ps->m.book_new = 0;
    }
    unsigned b = (pressed & SCE_CTRL_UP ? HB_UP : 0) | (pressed & SCE_CTRL_DOWN ? HB_DOWN : 0) |
                 (pressed & SCE_CTRL_LEFT ? HB_LEFT : 0) | (pressed & SCE_CTRL_RIGHT ? HB_RIGHT : 0) |
                 (pressed & SCE_CTRL_LTRIGGER ? HB_PREV : 0) | (pressed & SCE_CTRL_RTRIGGER ? HB_NEXT : 0) |
                 (pressed & SCE_CTRL_CIRCLE ? HB_BACK : 0) | (pressed & SCE_CTRL_TRIANGLE ? HB_RESET : 0);
    int sound;
    int open = help_book_update(&ps->hb, b, dt, &sound);
    if (ps->hb.volume[0] != g_volume[0] || ps->hb.volume[1] != g_volume[1]) {   // heard straight away
        g_volume[0] = ps->hb.volume[0];
        g_volume[1] = ps->hb.volume[1];
        settings_apply();
    }
    g_auto_potion = ps->hb.auto_potion;   // the game's VM picks it up (not mid Wi-Fi game)
    // the FRESH look: from the next game (switching player files mid-game would have both in
    // memory, too much); the title's sticker shows the setting at once
    if (ps->hb.fresh >= 0) g_fresh = ps->hb.fresh;
    if (sound >= 0) audio_sfx_play(sound == 0 ? "sound_toggle_b.wav" : "sound_player_select.wav", 1, 0, 0, ps);
    if (open) return;
    if (ps->hb.changed) settings_save();
    pause_menu_book_closed(&ps->m);
}

// the menu's buttons for a frame; PM_OPEN while it stays open
static int menu_input(Pause *ps, unsigned pressed, float dt) {
    if (ps->m.page == PG_BOOK) {
        help_input(ps, pressed, dt);
        int sound;
        pause_menu_update(&ps->m, 0, dt, &sound);   // the backdrop keeps drifting
        return PM_OPEN;
    }
    unsigned b = (pressed & SCE_CTRL_UP ? PM_UP : 0) | (pressed & SCE_CTRL_DOWN ? PM_DOWN : 0) |
                 (pressed & (SCE_CTRL_CROSS | SCE_CTRL_START) ? PM_ACCEPT : 0) | (pressed & SCE_CTRL_CIRCLE ? PM_BACK : 0);
    int sound;
    int r = pause_menu_update(&ps->m, b, dt, &sound);
    if (sound >= 0) audio_sfx_play(sound ? "sound_player_select.wav" : "sound_toggle_b.wav", 1, 0, 0, ps);
    return r;
}

// one frame of the open menu: 1 while it stays open, 0 closed, 2 back to the title
static int pause_input(Pause *ps, AsVm *vm, unsigned pressed, float dt) {
    int r = menu_input(ps, pressed, dt);
    if (r == PM_OPEN) return 1;
    ps->open = 0;
    if (r == PM_TO_TITLE) return 2;
    if (ps->select) { ps->select = 0; return 0; }
    pause_set_state(vm, r == PM_TO_MAP ? 3 : 1);   // 3: the scripts take the party back to the map
    return 0;
}

static void pause_open(Pause *ps, int on_map) {
    ps->open = 1;
    pause_menu_open(&ps->m, on_map);
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
    snprintf(g_last_error, sizeof g_last_error, "%s", msg);
    if (log_first(msg)) {
        log_line("[%u] %s", vm ? (unsigned)vm->tick : 0, msg);
        log_flush();
    }
}

// ---- the developer intro ---------------------------------------------------------------
// LEGAL.swf, as the original plays it at start: Newgrounds, The Behemoth's bird, the featured
// programmer and artist. It ends by calling ChangeMovie(); START or CROSS skips it.
static int g_intro_done;
static AsVal intro_change_movie(AsVm *vm, AsVal this_, int argc, AsVal *argv) {
    (void)vm; (void)this_; (void)argc; (void)argv;
    g_intro_done = 1;
    return as_undef();
}

static void intro_screen(DrawList *vb) {
    Movie *m = vita_loader(NULL, "LEGAL.swf");
    if (!m) return;
    AsVm *t = as_new(vita_loader);
    t->sound_start = vita_sound_start;
    t->sound_stop = vita_sound_stop;
    t->warn = vita_warn;
    cc_host_init(t);
    as_render_init(t);
    t->root = as_mc_new(t, m, &m->root, NULL, 0);
    cc_native_root_functions(t, &t->root->obj);
    as_def_fn(t, t->global, "ChangeMovie", intro_change_movie);
    g_intro_done = 0;
    SceCtrlData pad, old = {0};
    sceCtrlPeekBufferPositive(0, &old, 1);
    SceUInt64 prev = sceKernelGetProcessTimeWide(), acc = 0;
    const SceUInt64 tick_us = 1000000 / 30;
    int last = 0;
    while (!g_intro_done) {
        preload_step(PRELOAD_INTRO);   // the title's file while the intro plays
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & ~old.buttons & (SCE_CTRL_START | SCE_CTRL_CROSS)) break;
        old = pad;
        SceUInt64 now = sceKernelGetProcessTimeWide();
        acc += now - prev;
        prev = now;
        if (acc > 2 * tick_us) acc = 2 * tick_us;
        int ticked = 0;
        while (acc >= tick_us && !g_intro_done) {
            as_tick(t);
            as_gc(t);
            acc -= tick_us;
            ticked = 1;
            if (t->root->frame == m->root.nframes - 1) {   // the end, should it not say so
                if (last++ > 30) g_intro_done = 1;
            }
        }
        if (ticked) {
            RecolourCache *cache = g_cache;
            if (cache && (cache->used > cache->cap * 9 / 10 || cache->entries * 4 >= cache->table_size * 3)) {
                vita2d_wait_rendering_done();
                recolour_cache_free(cache);
                recolour_cache_init(cache, g_cache_mem, g_cache_bytes);
            }
            float k = SCREEN_W / m->stage_w;
            render_begin(vb);
            as_render_mc(t->root, (Mat){k / 20, 0, 0, k / 20, 0, 0}, CX_IDENTITY, vb);
        }
        vita2d_set_clear_color(RGBA8(m->bg[0], m->bg[1], m->bg[2], 255));
        vita2d_start_drawing();
        vita2d_clear_screen();
        draw_list(vb);
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }
    vita2d_wait_rendering_done();
    audio_sfx_stop(NULL);
    if (cc_music_stop) cc_music_stop();
    memset(cc_keys, 0, sizeof cc_keys);
    as_free(t);
}

// ---- title screen and main menu --------------------------------------------------------
// The original's executable shows MENU.swf's opening frame (the logo and the knights, with
// the title music), then attaches its "buttonchoices" panel and runs the menu itself: the
// highlight, the moves and what each button does aren't in the files. Here: START or CROSS
// on the title brings the panel up; the D-pad/stick moves (the chosen button grows, as the
// panel's buttons do in the original), CROSS picks. Local Game starts the game, Help &
// Options shows the controls page, Quit leaves; the online, download, leaderboard and store
// buttons say they aren't on the Vita. It all runs in a VM of its own, before the game's VM
// is set up (cc_host keeps per-VM state).

static void outlined_text(int x, int y, float size, const char *s) {
    for (int dx = -2; dx <= 2; dx += 2)
        for (int dy = -2; dy <= 2; dy += 2)
            vita2d_pgf_draw_text(g_font, x + dx, y + dy, RGBA8(0, 0, 0, 255), size, s);
    vita2d_pgf_draw_text(g_font, x, y, RGBA8(255, 255, 255, 255), size, s);
}

#ifdef CC_NET
// a line in the game's font with a black outline, centred on x (y: the line's middle), drawn
// now; the system font when the game font isn't there
static void game_text(int x, int y, float size, uint32_t rgb, const char *s) {
    if (as_render_host_line_height() <= 0) {
        outlined_text(x - vita2d_pgf_text_width(g_font, size * 0.9f, s) / 2, y + 8, size * 0.9f, s);
        return;
    }
    static DrawList dl;
    render_begin(&dl);
    dl.view[2] = SCREEN_W;
    dl.view[3] = SCREEN_H;
    CXform cx = {{(rgb >> 16 & 255) / 255.0f, (rgb >> 8 & 255) / 255.0f, (rgb & 255) / 255.0f, 1}, {0, 0, 0, 0}};
    as_render_host_text_outlined(s, x, y, size, size, 2, 2, (Mat){1, 0, 0, 1, 0, 0}, cx, &dl);
    draw_list(&dl);
}

// ---- Multiplayer: the Wi-Fi lobby, over the title ---------------------------------------
// Host a game (this Vita is player 1) or join one heard on the same Wi-Fi, or one over the
// internet by its game code or from the public games list (lobby.h); the host starts it once
// someone has joined. Everyone then picks their knight at character select, with Cross.
// W_MODE: the host picks the game (Local Game's book of modes) before it starts
enum { W_CLOSED, W_MENU, W_HOST, W_SCAN, W_CODE, W_JOINED, W_PUBLIC, W_MODE };
// the menu's choices (the public ones only with a list server)
enum { WI_HOST, WI_HOST_PUBLIC, WI_SCAN, WI_CODE, WI_PUBLIC };
static struct {
    int page, item;
    int back;                  // W_JOINED: the menu item to go back to
    int public;                // W_HOST: in the public games list (once open to the internet)
    int refresh;               // W_PUBLIC: frames until the list is asked for again
    char code[NET_CODE_LEN + 1];   // W_CODE: the code typed in (letters only), kept for next time
    int typing;                // W_CODE: the system keyboard is up
    char note[96];
    int note_timer;
} g_wifi;

static int wifi_items(int *out) {   // the menu's choices, top to bottom
    int n = 0;
    out[n++] = WI_HOST;
    if (lobby_available()) out[n++] = WI_HOST_PUBLIC;
    out[n++] = WI_SCAN;
    out[n++] = WI_CODE;
    if (lobby_available()) out[n++] = WI_PUBLIC;
    return n;
}

static int wifi_item_at(int which) {   // where that choice is in the menu
    int items[5], n = wifi_items(items);
    for (int i = 0; i < n; i++)
        if (items[i] == which) return i;
    return 0;
}

static int wifi_players(void) {
    int n = 0;
    for (int p = 0; p < NET_PLAYERS; p++) n += net.names[p][0] != 0;
    return n;
}

// a public game is in the list while it's open to the internet and waiting for players
static void wifi_listing(void) {
    uint32_t ext;
    if (g_wifi.page == W_HOST && g_wifi.public && net.state == NET_HOSTING && net_internet(&ext, NULL) > 0)
        lobby_publish(net_local_name(), wifi_players());
    else
        lobby_unpublish();
}

static void wifi_note(const char *s) {
    log_line("Wi-Fi: %s", s);
    log_flush();
    snprintf(g_wifi.note, sizeof g_wifi.note, "%s", s);
    g_wifi.note_timer = 60 * 4;
}

static int my_profile(uint8_t **out) {
    static uint8_t buf[3 * 65536];
    *out = buf;
    int n = cc_storage_pack(0, buf, sizeof buf);
    return n < 0 ? 0 : n;
}

// Join by Game Code takes the code in the Vita's own keyboard and text box (the IME dialog)
static SceWChar16 g_ime_text[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1], g_ime_title[64], g_ime_start[16];

static void wide(SceWChar16 *out, int cap, const char *s) {
    int i = 0;
    for (; s[i] && i < cap - 1; i++) out[i] = (unsigned char)s[i];
    out[i] = 0;
}

static int wifi_keyboard_open(void) {
    char start[NET_CODE_LEN + 2];
    snprintf(start, sizeof start, "%.4s%s%s", g_wifi.code, g_wifi.code[4] ? "-" : "", g_wifi.code[4] ? g_wifi.code + 4 : "");
    wide(g_ime_start, 16, start);
    const char *title = strtab_get("vita.wifi.code_title");
    wide(g_ime_title, 64, title ? title : "Game code (from the host's Multiplayer page)");
    memset(g_ime_text, 0, sizeof g_ime_text);
    SceImeDialogParam p;
    sceImeDialogParamInit(&p);
    p.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
    p.languagesForced = SCE_TRUE;
    p.type = SCE_IME_TYPE_BASIC_LATIN;
    p.option = SCE_IME_OPTION_NO_ASSISTANCE;
    p.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    p.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
    p.title = g_ime_title;
    p.maxTextLength = NET_CODE_LEN + 4;   // room for a dash and spaces
    p.initialText = g_ime_start;
    p.inputTextBuffer = g_ime_text;
    int r = sceImeDialogInit(&p);
    if (r < 0) log_line("Wi-Fi: keyboard didn't open (0x%08x)", r);
    g_wifi.typing = r >= 0;
    return g_wifi.typing;
}

// the keyboard is closed: 1 with the code in g_wifi.code (Enter), 0 if cancelled
static int wifi_keyboard_done(void) {
    SceImeDialogResult res;
    memset(&res, 0, sizeof res);
    sceImeDialogGetResult(&res);
    sceImeDialogTerm();
    g_wifi.typing = 0;
    if (res.button != SCE_IME_DIALOG_BUTTON_ENTER) return 0;
    int n = 0;   // letters and digits only, upper case (the dash and spaces are optional)
    for (int i = 0; g_ime_text[i] && n < NET_CODE_LEN; i++) {
        int c = g_ime_text[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) g_wifi.code[n++] = (char)c;
    }
    g_wifi.code[n] = 0;
    return 1;
}

// host: everyone starts now, playing by the host's settings; off the public list once started
static int wifi_start(void) {
    net.rules = g_auto_potion ? NET_RULE_AUTO_POTION : 0;
    if (!net_start()) return 0;
    lobby_unpublish();
    return 1;
}

// one frame of the lobby: 1 to start the game (everyone's in NET_PLAYING), 2 for the host to
// pick the game first (W_MODE: then net_start), else 0
static int wifi_update(unsigned pressed) {
    net_poll();
    if (net.state == NET_LOST) {
        wifi_note(net.error);
        net_leave();
        g_wifi.item = g_wifi.page == W_JOINED ? g_wifi.back : 0;
        g_wifi.page = W_MENU;
    }
    if (net.state == NET_PLAYING) { lobby_unpublish(); return 1; }
    if (g_wifi.page == W_MODE) return 0;   // the title's book of modes has the buttons
    int sound = -1;
    switch (g_wifi.page) {
    case W_MENU: {
        int items[5], ni = wifi_items(items);
        if (g_wifi.item >= ni) g_wifi.item = 0;
        int which = items[g_wifi.item];
        if (pressed & SCE_CTRL_UP) { g_wifi.item = (g_wifi.item + ni - 1) % ni; sound = 0; }
        if (pressed & SCE_CTRL_DOWN) { g_wifi.item = (g_wifi.item + 1) % ni; sound = 0; }
        if (pressed & SCE_CTRL_CROSS) {
            sound = 1;
            uint8_t *pr;
            int n = my_profile(&pr);
            if ((which == WI_HOST || which == WI_HOST_PUBLIC) && net_host(pr, n)) {
                g_wifi.page = W_HOST;
                g_wifi.public = which == WI_HOST_PUBLIC;
            } else if (which == WI_SCAN && net_scan()) { g_wifi.page = W_SCAN; g_wifi.item = 0; }
            else if (which == WI_CODE) { g_wifi.page = W_CODE; wifi_keyboard_open(); }
            else if (which == WI_PUBLIC) { g_wifi.page = W_PUBLIC; g_wifi.item = 0; g_wifi.refresh = 0; }
            else wifi_note(net.error);
        }
        if (pressed & SCE_CTRL_CIRCLE) { g_wifi.page = W_CLOSED; sound = 1; }
        break;
    }
    case W_HOST: {
        int n = wifi_players();
        if (pressed & (SCE_CTRL_START | SCE_CTRL_CROSS)) {
            sound = 1;
            if (n < 2) wifi_note("Waiting for someone to join first");
            else { g_wifi.page = W_MODE; audio_sfx_play("sound_player_select.wav", 1, 0, 0, NULL); return 2; }
        }
        if (pressed & SCE_CTRL_CIRCLE) {
            net_leave();
            g_wifi.page = W_MENU;
            g_wifi.item = wifi_item_at(g_wifi.public ? WI_HOST_PUBLIC : WI_HOST);
            sound = 1;
        }
        break;
    }
    case W_PUBLIC: {   // the public games list, asked for again every 5 seconds
        if (g_wifi.refresh-- <= 0) { lobby_refresh(); g_wifi.refresh = 60 * 5; }
        LobbyGame games[LOBBY_MAX];
        int n = lobby_games(games, 6);
        if (g_wifi.item >= n) g_wifi.item = n ? n - 1 : 0;
        if (n && (pressed & SCE_CTRL_UP)) { g_wifi.item = (g_wifi.item + n - 1) % n; sound = 0; }
        if (n && (pressed & SCE_CTRL_DOWN)) { g_wifi.item = (g_wifi.item + 1) % n; sound = 0; }
        if (n && (pressed & SCE_CTRL_CROSS)) {
            sound = 1;
            uint8_t *pr;
            int len = my_profile(&pr);
            if (net_join(games[g_wifi.item].addr, pr, len)) { g_wifi.page = W_JOINED; g_wifi.back = wifi_item_at(WI_PUBLIC); }
            else { wifi_note(net.error); g_wifi.refresh = 0; }
        }
        if (pressed & SCE_CTRL_CIRCLE) { g_wifi.page = W_MENU; g_wifi.item = wifi_item_at(WI_PUBLIC); sound = 1; }
        break;
    }
    case W_SCAN: {
        NetHost hosts[8];
        int n = net_hosts(hosts, 8);
        if (g_wifi.item >= n) g_wifi.item = n ? n - 1 : 0;
        if (n && (pressed & SCE_CTRL_UP)) { g_wifi.item = (g_wifi.item + n - 1) % n; sound = 0; }
        if (n && (pressed & SCE_CTRL_DOWN)) { g_wifi.item = (g_wifi.item + 1) % n; sound = 0; }
        if (n && (pressed & SCE_CTRL_CROSS)) {
            sound = 1;
            uint8_t *pr;
            int len = my_profile(&pr);
            if (net_join(hosts[g_wifi.item].addr, pr, len)) { g_wifi.page = W_JOINED; g_wifi.back = wifi_item_at(WI_SCAN); }
            else { wifi_note(net.error); net_scan(); }
        }
        if (pressed & SCE_CTRL_CIRCLE) { net_leave(); g_wifi.page = W_MENU; g_wifi.item = wifi_item_at(WI_SCAN); sound = 1; }
        break;
    }
    case W_CODE:   // the system keyboard takes the code; Cross types it again, Circle goes back
        if (g_wifi.typing) {
            if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) break;   // the dialog has the buttons
            if (!wifi_keyboard_done()) {   // cancelled: back to the menu (or to the code, if one was typed)
                if (!g_wifi.code[0]) { g_wifi.page = W_MENU; g_wifi.item = wifi_item_at(WI_CODE); }
                break;
            }
            sound = 1;
            uint32_t addr = net_code_addr(g_wifi.code);
            uint8_t *pr;
            int len = my_profile(&pr);
            if (!addr) wifi_note("That isn't a game code: check it with the host");
            else if (net_join(addr, pr, len)) { g_wifi.page = W_JOINED; g_wifi.back = wifi_item_at(WI_CODE); }
            else wifi_note(net.error);
            break;
        }
        if (pressed & SCE_CTRL_CROSS) { sound = 1; wifi_keyboard_open(); }
        if (pressed & SCE_CTRL_CIRCLE) { g_wifi.page = W_MENU; g_wifi.item = wifi_item_at(WI_CODE); sound = 1; }
        break;
    case W_JOINED:
        if (pressed & SCE_CTRL_CIRCLE) { net_leave(); g_wifi.page = W_MENU; g_wifi.item = g_wifi.back; sound = 1; }
        break;
    }
    wifi_listing();
    if (sound >= 0) audio_sfx_play(sound ? "sound_player_select.wav" : "sound_toggle_b.wav", 1, 0, 0, &g_wifi);
    return 0;
}

// the lobby as a page of the Help & Options book (PAUSE.swf's art, the game font)
static void wifi_words(const char *key, const char *fallback, char *out, int cap) {
    const char *s = strtab_get(key);
    snprintf(out, cap, "%s", s ? s : fallback);
}
#define WORDS(key, fallback) (strtab_get(key) ? strtab_get(key) : (fallback))

static int wifi_depth(int page) { return page == W_CLOSED ? 0 : page == W_MENU ? 1 : page == W_JOINED ? 3 : 2; }

static void wifi_draw(unsigned frame) {
    static DrawList ui;
    // a page turn each time a page opens (forward going in, back coming out): the old page
    // under the first half of the flip, the new one after; opening from the title turns from
    // an empty page
    static int shown = W_CLOSED, from = W_CLOSED, dir = 1;
    static unsigned last_frame;
    static float turn;
    static SceUInt64 last_us;
    SceUInt64 now = sceKernelGetProcessTimeWide();
    float dt = frame == last_frame + 1 ? (now - last_us) / 1e6f : 0;
    if (frame != last_frame + 1) shown = W_CLOSED;   // the lobby was closed in between
    last_frame = frame;
    last_us = now;
    if (g_wifi.page != shown) {
        from = shown;
        dir = wifi_depth(g_wifi.page) >= wifi_depth(shown) ? 1 : -1;
        turn = HB_TURN_SECONDS;
        shown = g_wifi.page;
    }
    int page = turn > HB_TURN_SECONDS / 2 ? from : shown;
    if (turn > 0) turn -= dt > 0.1f ? 0.1f : dt;
    static char lines[HB_PAGE_LINES][64], note[128], code[NET_CODE_LEN + 2];
    Movie *pm = host_movie("PAUSE.swf");
    HbPage pg = {0};
    pg.chosen = pg.code_chosen = -1;
    pg.time = frame / 60.0f;
    pg.alpha = 1;
    pg.back = WORDS("help.back", "Back");
    for (int i = 0; i < HB_PAGE_LINES; i++) { lines[i][0] = 0; pg.line[i] = lines[i]; }
    note[0] = 0;
    pg.note = note;
    pg.turn = turn > 0 ? turn : 0;
    pg.turn_dir = dir;
    const char *dots = (frame / 20) % 4 == 0 ? "" : (frame / 20) % 4 == 1 ? "." : (frame / 20) % 4 == 2 ? ".." : "...";
    if (page == W_CLOSED) pg.back = NULL;   // the empty page under the first turn
    switch (page) {
    case W_MENU: {
        pg.title = WORDS("vita.wifi.title", "Multiplayer");
        int items[5], ni = wifi_items(items);
        for (int i = 0; i < ni; i++) {
            switch (items[i]) {
            case WI_HOST: wifi_words("vita.wifi.host", "Host a Game", lines[i], 64); break;
            case WI_HOST_PUBLIC: wifi_words("vita.wifi.host_public", "Host a Public Game", lines[i], 64); break;
            case WI_SCAN: wifi_words("vita.wifi.join_wifi", "Join on this Wi-Fi", lines[i], 64); break;
            case WI_CODE: wifi_words("vita.wifi.join_code", "Join by Game Code", lines[i], 64); break;
            case WI_PUBLIC: wifi_words("vita.wifi.public_games", "Public Games", lines[i], 64); break;
            }
        }
        pg.nlines = ni;
        pg.chosen = g_wifi.item;
        if (items[g_wifi.item < ni ? g_wifi.item : 0] == WI_HOST_PUBLIC)
            wifi_words("vita.wifi.about_public", "Anyone online can see your game and join it.", note, sizeof note);
        else if (items[g_wifi.item < ni ? g_wifi.item : 0] == WI_PUBLIC)
            wifi_words("vita.wifi.about_list", "Games anyone can join, from all over.", note, sizeof note);
        else
            wifi_words("vita.wifi.about", "Play on the same Wi-Fi, or online with a game code.", note, sizeof note);
        pg.accept = WORDS("vita.wifi.select", "Select");
        break;
    }
    case W_HOST: case W_JOINED: {
        if (page == W_HOST) pg.title = g_wifi.public ? WORDS("vita.wifi.your_public_game", "Your Public Game")
                                                     : WORDS("vita.wifi.your_game", "Your Game");
        else pg.title = net.state == NET_CONNECTING ? WORDS("vita.wifi.connecting", "Connecting...")
                        : net.state == NET_JOINING   ? WORDS("vita.wifi.joining", "Joining...")
                                                     : WORDS("vita.wifi.joined", "Joined");
        for (int p = 0; p < NET_PLAYERS; p++) {
            if (net.names[p][0]) snprintf(lines[p], 64, "%d   %s%s", p + 1, net.names[p], p + 1 == net.port ? "  (you)" : "");
            else snprintf(lines[p], 64, "%d   %s%s", p + 1, WORDS("vita.wifi.waiting", "waiting"), dots);
        }
        pg.nlines = NET_PLAYERS;
        if (page == W_HOST) {
            uint32_t ext;
            const char *why;
            int open = net_internet(&ext, &why);
            if (open > 0) {
                char c[NET_CODE_LEN + 1];
                net_code(ext, c);
                snprintf(code, sizeof code, "%.4s-%s", c, c + 4);
                pg.code = code;
                pg.code_line = 4;
                int listed = g_wifi.public ? lobby_listed() : LOBBY_OFF;
                if (listed == LOBBY_OK) wifi_words("vita.wifi.listed", "In the Public Games list: anyone can join", note, sizeof note);
                else if (listed == LOBBY_FAILED) snprintf(note, sizeof note, "%s", lobby_why());
                else if (listed == LOBBY_WORKING) snprintf(note, sizeof note, "%s%s", WORDS("vita.wifi.listing", "Adding your game to the Public Games list"), dots);
                else wifi_words("vita.wifi.code_note", "Game code: friends over the internet join with it", note, sizeof note);
            } else if (open == 0) {
                snprintf(note, sizeof note, "%s%s", WORDS("vita.wifi.opening", "Opening your router for internet play"), dots);
            } else {
                snprintf(note, sizeof note, "%s: %s", WORDS("vita.wifi.wifi_only", "This Wi-Fi only"), why);
            }
            pg.accept = WORDS("vita.wifi.play", "Play");
            pg.back = WORDS("vita.wifi.close", "Close");
        } else {
            wifi_words("vita.wifi.wait_host", "Waiting for the host to start", note, sizeof note);
            pg.back = WORDS("vita.wifi.leave", "Leave");
        }
        break;
    }
    case W_SCAN: {
        pg.title = WORDS("vita.wifi.join_wifi", "Join on this Wi-Fi");
        NetHost hosts[8];
        int n = net_hosts(hosts, 8);
        if (!n) {
            snprintf(lines[0], 64, "%s%s", WORDS("vita.wifi.looking", "Looking for games"), dots);
            pg.nlines = 1;
        }
        for (int i = 0; i < n && i < 6; i++) snprintf(lines[i], 64, "%s   %d/4", hosts[i].name, hosts[i].players);
        if (n) { pg.nlines = n < 6 ? n : 6; pg.chosen = g_wifi.item; pg.accept = WORDS("vita.wifi.join", "Join"); }
        break;
    }
    case W_PUBLIC: {
        pg.title = WORDS("vita.wifi.public_games", "Public Games");
        LobbyGame games[LOBBY_MAX];
        int n = lobby_games(games, 6), st = lobby_list_state();
        if (!n) {
            if (st == LOBBY_FAILED) {
                wifi_words("vita.wifi.list_failed", "Can't get the list right now", lines[0], 64);
                snprintf(note, sizeof note, "%s", lobby_why());
            } else if (st == LOBBY_OK) {
                wifi_words("vita.wifi.no_public", "No public games right now", lines[0], 64);
                wifi_words("vita.wifi.no_public_note", "Host a Public Game and others can find yours", note, sizeof note);
            } else {
                snprintf(lines[0], 64, "%s%s", WORDS("vita.wifi.looking", "Looking for games"), dots);
            }
            pg.nlines = 1;
        }
        for (int i = 0; i < n; i++) snprintf(lines[i], 64, "%s   %d/4", games[i].name, games[i].players);
        if (n) { pg.nlines = n; pg.chosen = g_wifi.item; pg.accept = WORDS("vita.wifi.join", "Join"); }
        break;
    }
    case W_CODE: {
        pg.title = WORDS("vita.wifi.join_code", "Join by Game Code");
        int k = 0;   // what was typed, in the boxes (blank ones for the rest)
        for (int i = 0; i < NET_CODE_LEN; i++) {
            if (i == 4) code[k++] = '-';
            code[k++] = i < (int)strlen(g_wifi.code) ? g_wifi.code[i] : ' ';
        }
        code[k] = 0;
        pg.code = code;
        pg.code_line = 1;
        wifi_words("vita.wifi.code_help", "Type in the game code the host's Multiplayer page shows", note, sizeof note);
        pg.accept = WORDS("vita.wifi.type_code", "Type Code");
        break;
    }
    }
    if (pm) {
        render_begin(&ui);
        ui.view[2] = SCREEN_W;
        ui.view[3] = SCREEN_H;
        help_book_build_page(&pg, pm, SCREEN_W / 848.0f, &ui);
        draw_list(&ui);
    }
    if (g_wifi.note_timer > 0) {
        g_wifi.note_timer--;
        game_text(SCREEN_W / 2, SCREEN_H * 0.86f, 1.1f, 0xffffff, g_wifi.note);
    }
}
#endif

static void hide_sprite(AsMc *mc, int cid) {   // every clip of sprite cid under mc
    for (int i = 0; i < mc->ndisp; i++)
        if (mc->disp[i].kind == DISP_MC && mc->disp[i].mc) {
            if (mc->disp[i].cid == cid) mc->disp[i].mc->visible = 0;
            hide_sprite(mc->disp[i].mc, cid);
        }
}

// the menu's buttons, top to bottom
#ifdef CC_NET
static const int g_menu_order[] = {CC_MENU_PLAY, CC_MENU_WIFI, CC_MENU_OPTIONS, CC_MENU_QUIT};
#else
static const int g_menu_order[] = {CC_MENU_PLAY, CC_MENU_OPTIONS, CC_MENU_QUIT};
#endif
#define MENU_N ((int)(sizeof g_menu_order / sizeof *g_menu_order))

// 1: a Wi-Fi game was started (net.state NET_PLAYING), 0: Local Game
// from_game: back from Exit to Main Menu, under the loading screen, which then goes out over it
static int title_screen(DrawList *vb, RecolourCache *cache, void *cache_mem, size_t cache_bytes, int from_game) {
    Movie *m = vita_loader(NULL, "MENU.swf");
    if (!m) return 0;
    // the mode book's borrowed pictures, now rather than when Local Game opens it (a hitch)
    char art_path[256];
    int art_level;
    if (find_file("MODEART.swf", art_path, sizeof art_path, &art_level)) host_movie("MODEART.swf");
    int wifi_game = 0;
#ifdef CC_NET
    cc_menu_wifi = 1;
    g_wifi.page = W_CLOSED;
#endif
    AsVm *t = as_new(vita_loader);
    t->sound_start = vita_sound_start;
    t->sound_stop = vita_sound_stop;
    t->warn = vita_warn;
    cc_host_init(t);
    as_render_init(t);
    t->root = as_mc_new(t, m, &m->root, NULL, 0);
    cc_native_root_functions(t, &t->root->obj);
    if (from_game) {
        EXIT_TRACE("exit: title made");
        cc_loading_reveal(t);
        EXIT_TRACE("exit: loading screen on the title");
    }
    const char *prompt = strtab_number(1);   // the game's "Press START"
    if (!prompt) prompt = "Press START";
    const char *unavailable = strtab_get("vita.online.unavailable");
    if (!unavailable) unavailable = "Not available on PS Vita";
    AsMc *panel = NULL;
    AsMc *modes = NULL;   // Local Game's book of modes (Castle Crashers, Arena, All You Can Quaff)
    int item = 0, message = 0, mode = 0;
    const char *message_text = unavailable;   // shown for a while when message is set
    cc_game_mode = CC_MODE_STORY;
    static Pause controls;   // Help & Options: the pause menu's Help & Options list and its books
    SceCtrlData pad, old = {0};
    sceCtrlPeekBufferPositive(0, &old, 1);
    const SceUInt64 tick_us = 1000000 / 30;
    SceUInt64 prev = sceKernelGetProcessTimeWide(), acc = from_game ? tick_us : 0;   // (no black first frame)
    unsigned frame = 0;
    for (;;) {
        preload_step(PRELOAD_TITLE);
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pressed = pad.buttons & ~old.buttons;
        int ly = pad.ly - 128, lx = pad.lx - 128, oly = old.ly - 128, olx = old.lx - 128;
        if (ly < -80 && oly >= -80) pressed |= SCE_CTRL_UP;   // the stick moves once per push
        if (ly > 80 && oly <= 80) pressed |= SCE_CTRL_DOWN;
        if (lx < -80 && olx >= -80) pressed |= SCE_CTRL_LEFT;
        if (lx > 80 && olx <= 80) pressed |= SCE_CTRL_RIGHT;
        old = pad;
        cc_menu_fresh(t, panel, g_fresh && cc_fresh_available());   // the FRESH sticker while the look is on
#ifdef CC_NET
        if (g_wifi.page != W_CLOSED || net.state != NET_OFF) {
            int w = wifi_update(pressed);
            if (w == 1) { wifi_game = 1; break; }
            if (w == 2 && !modes) {   // the host picks the game: the book of modes over the title
                modes = cc_modemenu_attach(t, m->stage_w / 2.0f, m->stage_h / 2.0f);
                mode = 0;
                if (panel) panel->visible = 0;
                if (!modes && wifi_start()) { wifi_game = 1; break; }   // no book: the story
            }
            if (g_wifi.page != W_MODE) pressed = 0;
        }
#endif
        if (controls.open) {
            SceUInt64 tnow = sceKernelGetProcessTimeWide();
            static SceUInt64 tlast;
            float dt = tlast && tnow > tlast ? (tnow - tlast) / 1000000.0f : 1 / 60.0f;
            tlast = tnow;
            if (menu_input(&controls, pressed, dt > 0.1f ? 0.1f : dt) != PM_OPEN) controls.open = 0;
        } else if (!panel) {
            if (pressed & (SCE_CTRL_START | SCE_CTRL_CROSS)) {
                // left of the knights, over the (hidden, empty) news box
                panel = cc_menu_attach(t, m->stage_w * 0.226f, m->stage_h * 0.635f);
                if (!panel) break;   // no panel in this copy of the file: straight into the game
                cc_menu_highlight(t, panel, g_menu_order[item]);
            }
        } else if (modes && pressed) {
            // the hand goes where the pictures are: Castle Crashers on the right page and
            // Volleyball under it, All You Can Quaff at the top of the left one and Arena under it
            enum { M_STORY, M_ARENA, M_QUAFF, M_VOLLEY };
            static const int left[] = {M_QUAFF, M_ARENA, M_QUAFF, M_ARENA}, right[] = {M_STORY, M_VOLLEY, M_STORY, M_VOLLEY},
                             up[] = {M_STORY, M_QUAFF, M_QUAFF, M_STORY}, down[] = {M_VOLLEY, M_ARENA, M_ARENA, M_VOLLEY};
            int to = mode;
            if (pressed & SCE_CTRL_LEFT) to = left[mode];
            if (pressed & SCE_CTRL_RIGHT) to = right[mode];
            if (pressed & SCE_CTRL_UP) to = up[mode];
            if (pressed & SCE_CTRL_DOWN) to = down[mode];
            if (to != mode) {
                cc_modemenu_point(t, modes, mode, to);
                mode = to;
                audio_sfx_play("sound_toggle_b.wav", 1, 0, 0, &controls);
            }
            char vmenu[256];
            int is_level;
            if ((pressed & SCE_CTRL_CROSS) && mode == M_VOLLEY && !find_file("VMENU.swf", vmenu, sizeof vmenu, &is_level)) {
                // the court picker comes from the PS3 version's files, not the PC's
                message_text = "Volleyball needs VMENU.swf (from the PS3 game) in " DATA_DIR "game";
                message = 30 * 5;
                pressed &= ~SCE_CTRL_CROSS;
            }
            if (pressed & SCE_CTRL_CROSS) {
                audio_sfx_play("sound_player_select.wav", 1, 0, 0, &controls);
                static const int game_mode[CC_MODE_ITEMS] = {CC_MODE_STORY, CC_MODE_ARENA, CC_MODE_QUAFF, CC_MODE_VOLLEYBALL};
                cc_game_mode = game_mode[mode];
#ifdef CC_NET
                if (g_wifi.page == W_MODE) {   // the host's pick: everyone starts it now
                    net.mode = cc_game_mode;
                    if (wifi_start()) { log_line("Wi-Fi game: mode %d, delay %d frames", net.mode, net.delay); wifi_game = 1; break; }
                    wifi_note(net.error);
                    g_wifi.page = W_HOST;
                    as_call_method(t, as_objv(&modes->obj), "removeMovieClip", 0, NULL);
                    modes = NULL;
                    panel->visible = 1;
                    cc_game_mode = CC_MODE_STORY;
                    continue;
                }
#endif
                break;
            } else if (pressed & SCE_CTRL_CIRCLE) {   // back to the menu (the host: to the lobby)
                as_call_method(t, as_objv(&modes->obj), "removeMovieClip", 0, NULL);
                modes = NULL;
#ifdef CC_NET
                if (g_wifi.page == W_MODE) g_wifi.page = W_HOST;
#endif
                panel->visible = 1;
            }
        } else if (pressed) {
            int moved = 0;
            if (pressed & (SCE_CTRL_UP | SCE_CTRL_LEFT)) { item = (item + MENU_N - 1) % MENU_N; moved = 1; }
            if (pressed & (SCE_CTRL_DOWN | SCE_CTRL_RIGHT)) { item = (item + 1) % MENU_N; moved = 1; }
            if (moved) {
                cc_menu_highlight(t, panel, g_menu_order[item]);
                audio_sfx_play("sound_toggle_b.wav", 1, 0, 0, &controls);   // the menus' move sound
            }
            if (pressed & SCE_CTRL_CROSS) {
                audio_sfx_play("sound_player_select.wav", 1, 0, 0, &controls);
                int chosen = g_menu_order[item];
                if (chosen == CC_MENU_PLAY) {   // which game: the book of modes, its hand on Castle Crashers
                    modes = cc_modemenu_attach(t, m->stage_w / 2.0f, m->stage_h / 2.0f);
                    if (!modes) break;   // no book in this copy of the file: straight into the game
                    mode = 0;
                    panel->visible = 0;
                }
#ifdef CC_NET
                if (chosen == CC_MENU_WIFI) {
                    g_wifi.page = W_MENU;
                    g_wifi.item = 0;
                    if (!net_init()) { wifi_note("The network isn't available"); g_wifi.page = W_CLOSED; }
                }
#endif
                if (chosen == CC_MENU_QUIT) {
                    vita2d_wait_rendering_done();
                    audio_shutdown();
                    log_flush_wait();
                    vita2d_fini();
                    sceKernelExitProcess(0);
                }
                if (chosen == CC_MENU_OPTIONS) {
                    controls.open = 1;
                    pause_menu_open_help(&controls.m);
                }
            }
        }
        memset(cc_keys, 0, sizeof cc_keys);
        SceUInt64 now = sceKernelGetProcessTimeWide();
        acc += now - prev;
        prev = now;
        if (acc > 2 * tick_us) acc = 2 * tick_us;
        int ticked = 0;
        while (acc >= tick_us) {
            if (modes) cc_modemenu_tick(t, modes);
            as_tick(t);
            as_gc(t);
            acc -= tick_us;
            ticked = 1;
        }
        if (ticked) {
            if (cache->used > cache->cap * 9 / 10 || cache->entries * 4 >= cache->table_size * 3) {
                vita2d_wait_rendering_done();
                recolour_cache_free(cache);
                recolour_cache_init(cache, cache_mem, cache_bytes);
            }
            float k = SCREEN_W / m->stage_w;
            Mat screen = {k / 20, 0, 0, k / 20, 0, 0};
            render_begin(vb);
            hide_sprite(t->root, 626);   // the news bar (no news on the Vita)
            as_render_mc(t->root, screen, CX_IDENTITY, vb);
            if (!panel && !cc_loading_busy(t) && (frame / 30) % 2 == 0 && as_render_host_line_height() > 0) {   // blinking, in the game's font, where the news bar was
                Mat id = {1, 0, 0, 1, 0, 0};
                CXform black = {{0, 0, 0, 1}, {0, 0, 0, 0}};
                for (int dx = -2; dx <= 2; dx += 2)
                    for (int dy = -2; dy <= 2; dy += 2)
                        if (dx || dy) as_render_host_text(prompt, 172 + dx, 443 + dy, 1.5f, 1.5f, 2, id, black, vb);
                as_render_host_text(prompt, 172, 443, 1.5f, 1.5f, 2, id, CX_IDENTITY, vb);
            }
        }
        vita2d_set_clear_color(RGBA8(m->bg[0], m->bg[1], m->bg[2], 255));
        vita2d_start_drawing();
        vita2d_clear_screen();
        draw_list(vb);
        if (panel && !modes) {   // the power button that stands for Quit
            static CcVert tris[4096];
            float k = SCREEN_W / m->stage_w;
            int n = cc_menu_power_button(panel, (Mat){k / 20, 0, 0, k / 20, 0, 0}, tris, 4096);
            vita2d_color_vertex *v = n ? vita2d_pool_memalign(n * sizeof *v, sizeof *v) : NULL;
            for (int i = 0; v && i < n; i++)
                v[i] = (vita2d_color_vertex){tris[i].x, tris[i].y, 0.5f,
                                             RGBA8(tris[i].rgba[0], tris[i].rgba[1], tris[i].rgba[2], tris[i].rgba[3])};
            if (v) vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, n);
            Mat hm;
            AsMc *hl = cc_menu_highlight_clip(panel, &hm);   // the chosen button's frame, over all that
            if (hl) {
                static DrawList hvb;
                Mat pm = mat_mul((Mat){k / 20, 0, 0, k / 20, 0, 0}, mat_mul(t->root->m, mat_mul(panel->m, hm)));
                render_begin(&hvb);
                hvb.view[2] = SCREEN_W;
                hvb.view[3] = SCREEN_H;
                hl->visible = 1;
                as_render_mc(hl, pm, CX_IDENTITY, &hvb);
                hl->visible = 0;
                draw_list(&hvb);
            }
            Mat bm;   // the BETA sticker on Multiplayer, over everything
            if (cc_menu_beta_sticker() && cc_menu_beta_place(panel, (Mat){k / 20, 0, 0, k / 20, 0, 0}, &bm)) {
                static DrawList svb;
                render_begin(&svb);
                render_shape(cc_menu_beta_sticker(), bm, CX_IDENTITY, &svb);
                draw_list(&svb);
            }
        }
        frame++;
        if (!panel && !cc_loading_busy(t) && (frame / 30) % 2 == 0 && as_render_host_line_height() <= 0) {   // no game font
            int w = vita2d_pgf_text_width(g_font, 1.2f, prompt);
            outlined_text(172 - w / 2, 455, 1.2f, prompt);
        }
        if (message > 0) {
            message--;
            int w = vita2d_pgf_text_width(g_font, 1.0f, message_text);
            outlined_text((SCREEN_W - w) / 2, SCREEN_H - 24, 1.0f, message_text);
        }
        if (controls.open) pause_draw(&controls);
#ifdef CC_NET
        if (g_wifi.page != W_CLOSED && g_wifi.page != W_MODE) wifi_draw(frame);
        else if (g_wifi.note_timer > 0) {   // why the last Wi-Fi game ended
            g_wifi.note_timer--;
            game_text(SCREEN_W / 2, SCREEN_H - 30, 1.1f, 0xffffff, g_wifi.note);
        }
#endif
        vita2d_end_drawing();
        vita2d_common_dialog_update();   // the system keyboard, when it's up (Join by Game Code)
        vita2d_swap_buffers();
    }
    vita2d_wait_rendering_done();
    if (cc_music_stop) cc_music_stop();
    memset(cc_keys, 0, sizeof cc_keys);
    as_free(t);
    return wifi_game;
}
int main(void) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    vita2d_init_advanced_with_msaa(16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
    swf_bitmap_loaded = bitmap_to_texture;
    vita2d_set_vblank_wait(1);
    g_font = vita2d_load_default_pgf();
    make_split_program();
    g_indices = gpu_alloc(sizeof(uint16_t) * MAX_BATCH, &g_index_block);
    for (int i = 0; g_indices && i < MAX_BATCH; i++) g_indices[i] = i;
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG);   // the other pads (Ext2 reads)
    log_open();
    log_line("cc_vita started");
    if (cc_custom_load(DATA_DIR "custom/"))   // optional: a painted character (custom_char.h)
        log_line("custom character \"%s\" (fights like character %d)", cc_custom_name(), cc_custom_base());
    cc_look_load(DATA_DIR "weapon_looks.txt");   // weapon looks from the armoury (weapon_look.h)
    if (cc_animal_load(DATA_DIR "custom/animal/"))   // optional: a custom animal orb (custom_animal.h)
        log_line("custom animal orb \"%s\"", cc_animal_name());
    {   // optional: Paint Jr. from the PC's files (paint_jr.h)
        char path[256];
        int lvl;
        cc_paintjr_set_available(find_file("FRESH01.swf", path, sizeof path, &lvl),
                                 find_file("PJPARTS.swf", path, sizeof path, &lvl),
                                 find_file("PAINTJR.swf", path, sizeof path, &lvl));
        cc_paintjr_set_castle(find_file("LEVEL15_NEW.swf", path, sizeof path, &lvl));
        if (cc_paintjr_p_type()) log_line("Paint Jr. (character %d)", cc_paintjr_p_type());
    }
    bg_init();

    Movie *start = host_movie("MAIN.swf");
    if (!start) {
        SceCtrlData pad;
        for (;;) {
            show_message(g_last_error, "Copy the game and levels folders from your own game files to " DATA_DIR
                                       "   (START: quit)");
            sceCtrlPeekBufferPositive(0, &pad, 1);
            if (pad.buttons & SCE_CTRL_START) break;
        }
        log_flush_wait();
        vita2d_fini();
        sceKernelExitProcess(0);
        return 0;
    }

    cc_storage_load(SAVE_PATH);
    cc_achievements_load(ACH_PATH);
#ifdef CC_TROPHIES
    int trophies = trophies_init();   // with NoTrpDrm; earned achievements are synced below
    if (trophies < 0) log_line("trophies off (0x%08X): is NoTrpDrm under *main?", (unsigned)trophies);
    else log_line("trophies on");
    trophies = trophies > 0;
#else
    int trophies = 0;
#endif
    unsigned trophies_synced = 0;
    // the game's words (from the user's castle.exe by extract_strings.py), then Vita wording
    strtab_load(DATA_DIR "strings_en.txt");
    strtab_load(DATA_DIR "strings_vita_en.txt");
    strtab_load(DATA_DIR "strings_help_en.txt");   // Help & Options' words (tools/extract_help.py)
    audio_init(DATA_DIR);
    settings_load();
    // the menus' move and select sounds, decoded now (silently) so the first press is heard
    audio_sfx_play("sound_toggle_b.wav", 0, 0, 0, NULL);
    audio_sfx_play("sound_player_select.wav", 0, 0, 0, NULL);
    cc_music_play = audio_music_play;
    cc_music_stop = audio_music_stop;
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
    g_cache = &cache;
    g_cache_mem = cache_mem;
    g_cache_bytes = cache_bytes;

    intro_screen(&vb);

    int from_game = 0;
new_game:   // the title again after Exit to Main Menu
    g_npreloaded = 0;
    g_netgame = title_screen(&vb, &cache, cache_mem, cache_bytes, from_game);
#ifdef CC_NET
    if (g_netgame) cc_game_mode = net.mode;   // the host's pick, on every Vita
#endif

    AsVm *vm = as_new(vita_loader);
    vm->rand_state = (uint32_t)sceKernelGetProcessTimeWide() ^ 0x9E3779B9u;   // Flash's random() differs each session
    vm->sound_start = vita_sound_start;
    vm->sound_stop = vita_sound_stop;
    vm->warn = vita_warn;
    vm->prefetch = vita_prefetch;   // files load on a second thread while the game runs
#ifdef CC_NET
    cc_custom_set_active(!g_netgame);
#endif
    cc_look_set_active(!g_netgame);     // nor the looks kept on this one
    cc_animal_set_active(!g_netgame);   // not in Wi-Fi games: the other machine may not have it
    cc_paintjr_set_active(!g_netgame);  // nor Paint Jr.'s files
    release_game_movies();              // the last game's part files (paint_jr.c) may go now
    cc_movie_keep = keep_for_game;
    cc_host_init(vm);
    as_render_init(vm);
    cc_set_auto_potion(vm, g_auto_potion);
    cc_fresh_set(g_fresh && !g_netgame);   // the FRESH look: this Vita's own, not in a Wi-Fi game
    int net_wait = 0;        // Wi-Fi play: passes spent waiting for the others
#ifdef CC_NET
    uint32_t net_tick = 0;   // the next frame to run
    NetInput mine = {0};
    if (g_netgame) {   // the same game on every Vita: seed, clock, everyone's profiles
        netplay_begin(vm);
        vm->prefetch = netplay_prefetch;
        cc_set_auto_potion(vm, net.rules & NET_RULE_AUTO_POTION);   // the host's, on every Vita
        g_net_state = 0;
        log_line("Wi-Fi game: port %d, seed %u", net.port, net.seed);
    } else {   // on your own: player 1 is this Vita's user, as the others' names in a Wi-Fi game
        memset(cc_net_names, 0, sizeof cc_net_names);
        snprintf(cc_net_names[0], sizeof cc_net_names[0], "%s", net_local_name());
    }
#endif
    // All You Can Quaff is a file of its own (its title, joining, the feast, the results); the
    // story and Arena start in MAIN, which asks GetGameMode() which one it is
    Movie *first = start;
    if (cc_game_mode == CC_MODE_QUAFF) {
        first = vita_loader(NULL, "QUOFF.swf");
        if (!first) { first = start; cc_game_mode = CC_MODE_STORY; }
    }
    log_line("game mode %d (%s)", cc_game_mode, first == start ? "MAIN.swf" : "QUOFF.swf");
    vm->root = as_mc_new(vm, first, &first->root, NULL, 0);
    cc_loading_attach(vm);   // the game's loading screen over level changes

    SceCtrlData pad, old = {0}, pads_old[LOCAL_PADS] = {{0}};
    int overlay = 0;
    static Pause pause;
    SceUInt64 last = sceKernelGetProcessTimeWide(), acc = 0, prev = last;
    float fps = 0, tick_ms = 0, build_ms = 0, shown_tick = 0, shown_build = 0;
    int frames = 0, ticks = 0;
    unsigned heat_clock = 0;
    int ach_shown = 0, ach_timer = 0;   // the "achievement unlocked" banner
    int saving_timer = 0;               // game frames left of the "Saving..." note
    uint32_t last_free = 0;
    int last_free_count = -1;   // the files kept after the last pass
    const SceUInt64 tick_us = 1000000 / 30;   // the game's frame rate
    int exit_to_title = 0;
    int leaving = 0;   // Exit to Main Menu: the loading screen is coming in over the game
    pause.open = 0;
    pause.select = 0;
    unsigned select_eat = 0;   // buttons held from closing the menu at character select
    // PAUSE.swf is read, tessellated and uploaded now: on the first Start that took ~3 s
    host_movie("PAUSE.swf");
    if (!pause.have_art) pause_build_art(&pause);

    while (!exit_to_title) {
        if (!leaving) preload_step(PRELOAD_GAME);
        pads_read(&pad, !g_netgame);
        unsigned pressed = pad.buttons & ~old.buttons;
        unsigned nav = pad_nav(&pad, &old);   // menus: the stick moves once per push too
        old = pad;
        // players 2-4's pads: their Start opens the pause menu too, and they all steer it
        unsigned others_start = 0, others_nav = 0, held = pad.buttons;
        for (int i = 1; i < LOCAL_PADS; i++) {
            if (g_pad_on[i]) {
                unsigned n = pad_nav(&g_pads[i], &pads_old[i]);
                others_nav |= n;
                if ((n & SCE_CTRL_START) && !others_start) others_start = i + 1;
                held |= g_pads[i].buttons;
            }
            pads_old[i] = g_pads[i];
        }
        int debug = pad.buttons & SCE_CTRL_SELECT;
        if (debug && (pressed & SCE_CTRL_START)) break;
        if (debug && (pressed & SCE_CTRL_UP)) overlay = !overlay;
        // in a level and on the map, Start is the pause menu's (the executable's in the original)
        int level = in_level(vm), on_map = !level && in_map(vm), select = !level && !on_map && in_select(vm);
        // the mini games: Start is the pause menu's too, except at character select and on
        // Volleyball's court picker (their own menus)
        int mini = cc_game_mode != CC_MODE_STORY && !select && !in_loader(vm, "VMENU.swf");
        uint32_t buttons = debug ? 0 : pad_buttons(&pad, level);
        // character select: the Cross or Start that closed the menu isn't a pick until let go
        if (select_eat && !(held & select_eat)) select_eat = 0;
        if (select_eat) buttons = 0;
        if (leaving) pressed = buttons = others_start = 0;
        uint32_t no_start = (level || on_map || select || mini) ? ~(1u << CCB_START) : ~0u;
        memset(cc_keys, 0, sizeof cc_keys);
        cc_port_keys(1, buttons & no_start);
        for (int i = 1; i < LOCAL_PADS; i++)
            if (g_pad_on[i] && !select_eat && !leaving) cc_port_keys(i + 1, pad_buttons(&g_pads[i], level) & no_start);
        int start_port = (pressed & SCE_CTRL_START) && !debug ? 1 : others_start;
        if (pause.open) {
            float dt = (sceKernelGetProcessTimeWide() - prev) / 1000000.0f;
            int was_select = pause.select;
            int r = pause_input(&pause, vm, nav | others_nav, dt > 0.1f ? 0.1f : dt);
            if (r == 2) { leaving = 1; EXIT_TRACE("exit: loading screen in"); }
            if (!g_netgame) cc_set_auto_potion(vm, g_auto_potion);   // Settings may have changed it
            if (r == 0 && was_select) select_eat = held & (SCE_CTRL_CROSS | SCE_CTRL_START | SCE_CTRL_CIRCLE);
            memset(cc_keys, 0, sizeof cc_keys);
            buttons = 0;   // (Wi-Fi play: the game runs on under the menu, without this player)
        } else if (mini && start_port) {
            // as at character select: no Exit To Map, the scripts aren't told (the game just
            // stands still under it, outside Wi-Fi games); Exit to Main Menu goes to the title
            pause_open(&pause, 1);
            pause.select = 1;
            if (!pause.have_art) pause_build_art(&pause);
        } else if ((level || on_map) && start_port && !as_tobool(vm, cc_flash_global(vm, "g_bNoPause"))) {
            g_pause_port = start_port;
            pause_open(&pause, on_map);
            pause_set_state(vm, 2);
            log_flush();   // the log is complete while paused (to copy it off by FTP)
            if (!pause.have_art) pause_build_art(&pause);
        } else if (select && start_port) {
            // the same menu as the map's (no Exit To Map); the players' states stay as they are
            pause_open(&pause, 1);
            pause.select = 1;
            if (!pause.have_art) pause_build_art(&pause);
        }

        // run the game's frames that are due (at most two, so a slow frame doesn't snowball)
        SceUInt64 now = sceKernelGetProcessTimeWide();
        acc += now - prev;
        prev = now;
        if (acc > 2 * tick_us) acc = 2 * tick_us;
        if (pause.open && !g_netgame) acc = 0;   // the game stands still under the menu (not in a Wi-Fi game)
#ifdef CC_NET
        if (g_netgame) {
            net_poll();
            if (net.state == NET_LOST && !leaving) {   // back to the title, saying why
                wifi_note(net.error);
                log_line("Wi-Fi game ended: %s", net.error);
                leaving = 1;
            }
            const char *want = netplay_file_wanted();   // a level file every Vita must have first
            if (want && !vita_prefetch(vm, want)) netplay_file_ready();
            if (net_catch_up()) acc += tick_us;   // further behind the host than need be: a frame more
            static int logged_delay;
            if (net.delay != logged_delay) { logged_delay = net.delay; log_line("Wi-Fi game: delay now %d frames", net.delay); }
        }
#endif
        int ticked = 0, stalled = 0;
        float hitch_script = 0, hitch_gc = 0, hitch_build = 0;   // this pass, for the slow-frame log
        const char *hitch_what = "";
        // leaving: only the loading screen moves; once it covers the screen the game goes
        while (leaving && acc >= tick_us) {
            if (!exit_to_title && cc_loading_cover(vm)) { exit_to_title = 1; EXIT_TRACE("exit: covered"); }
            acc -= tick_us;
            ticked = 1;
        }
        while (!leaving && acc >= tick_us) {
#ifdef CC_NET
            if (g_netgame) {   // this frame from everyone's buttons, once they're all here
                mine.buttons = buttons;
                mine.state = g_net_state;
                int lv = in_level(vm), om = !lv && in_map(vm);
                int ok = netplay_step(vm, net_tick, &mine, lv || om);
                // not here yet: it's often on its way, a millisecond or two off; wait a little
                // for it rather than a whole picture (the first frame of a pass only)
                for (int w = 0; !ok && !ticked && w < 8; w++) {
                    sceKernelDelayThread(500);
                    ok = netplay_step(vm, net_tick, &mine, lv || om);
                }
                if (!ok) { acc = tick_us; stalled = 1; break; }
                g_net_state = 0;
                net_tick++;
            }
#endif
            SceUInt64 t0 = sceKernelGetProcessTimeWide();
            cc_volleyball_tick(vm);
            as_tick(vm);
            SceUInt64 t1 = sceKernelGetProcessTimeWide();
            as_gc(vm);
            SceUInt64 t2 = sceKernelGetProcessTimeWide();
            hitch_script += (t1 - t0) / 1000.0f;
            hitch_gc += (t2 - t1) / 1000.0f;
            tick_ms += (t2 - t0) / 1000.0f;
            ticks++;
            acc -= tick_us;
            ticked = 1;
        }
        net_wait = stalled ? net_wait + 1 : 0;
        if (cc_quit_requested && !leaving) {   // the scripts went back to the menus: as Exit to Main Menu
            cc_quit_requested = 0;
            leaving = 1;
            pause.open = 0;
            EXIT_TRACE("exit: the game quit to the menu");
        }
        if (cc_save_requested && cc_game_mode == CC_MODE_VOLLEYBALL) cc_save_requested = 0;   // a mini game
        if (cc_save_requested) {
            cc_save_requested = 0;
#ifdef CC_NET
            if (g_netgame) {   // this Vita's player only, as their own profile
                if (!cc_storage_save_as(SAVE_PATH, net.port - 1)) snprintf(g_last_error, sizeof g_last_error, "couldn't write " SAVE_PATH);
            } else
#endif
            if (!cc_storage_save(SAVE_PATH)) snprintf(g_last_error, sizeof g_last_error, "couldn't write " SAVE_PATH);
            saving_timer = 60;   // "Saving..." at the bottom for two seconds, as the original shows it
        }

        if (ticked) {
            // every few seconds, once the heap is half full: let go of the files left behind
            // (levels played earlier, the title). Below that they stay, so going back to the
            // map or replaying a level doesn't load them again.
            // (not on tick % 150: two ticks in one pass could step over it for minutes)
            static SceUInt64 last_flush;
            SceUInt64 t = sceKernelGetProcessTimeWide();
            if (t - last_flush >= 2000000) {
                static int beats;
                if (++beats % 5 == 0)   // every 10 s, so a log that stops says roughly when
                    log_line("[%u] running, heap %u KB, %d files", (unsigned)vm->tick,
                             (unsigned)(mallinfo().uordblks >> 10), g_nmovies);
                last_flush = t;
                log_flush();
            }
            // Only after a new file came in: once over half, a pass every 3 s that finds the
            // rest still in use costs frames for nothing (the map dropped to 45 fps).
            if (vm->tick - last_free >= 90 && g_nmovies != last_free_count &&
                mallinfo().uordblks > (size_t)_newlib_heap_size_user / 2) {
                last_free = vm->tick;
                free_unused_movies(vm, &cache, cache_mem, cache_bytes);
                last_free_count = g_nmovies;
                hitch_what = " (freed files)";
            }
            // a full recolour cache stops helping: start it over (after the GPU is done with it)
            if (cache.used > cache.cap * 9 / 10 || cache.entries * 4 >= cache.table_size * 3) {
                vita2d_wait_rendering_done();
                recolour_cache_free(&cache);
                recolour_cache_init(&cache, cache_mem, cache_bytes);
                hitch_what = " (colour cache restarted)";
            }
            SceUInt64 t0 = sceKernelGetProcessTimeWide();
            float k = SCREEN_W / first->stage_w;
            Mat screen = {k / 20, 0, 0, k / 20, 0, 0};
            render_begin(&vb);
#ifdef CC_NET
            netplay_hud_names(vm);
#endif
            as_render_mc(vm->root, screen, CX_IDENTITY, &vb);
            if (saving_timer > 0) {
                saving_timer--;
                pause_menu_saving(k, saving_timer < 10 ? saving_timer / 10.0f : 1, &vb);
            }
            hitch_build = (sceKernelGetProcessTimeWide() - t0) / 1000.0f;
            build_ms += hitch_build;
        }

        // A new picture comes only with a game frame, 30 a second. Drawing it again in between
        // showed the same frame twice and doubled the GPU's work: the map (66k triangles, ~22 ms
        // of GPU a frame) then missed every other vblank and ran unevenly at 45 fps. The pause
        // menu animates on its own and is drawn every vblank.
        if (!ticked && !pause.open && net_wait < 20) {
            sceDisplayWaitVblankStart();
            continue;
        }
        vita2d_set_clear_color(RGBA8(first->bg[0], first->bg[1], first->bg[2], 255));
        SceUInt64 draw_t0 = sceKernelGetProcessTimeWide();
        draw_frame(&vb, heat_clock);
        if (ticked) heat_clock += 2;   // the shimmer's speed, as when it was drawn 60 times a second
        if (pause.open) pause_draw(&pause);
#ifdef CC_NET
        if (g_netgame && net_wait >= 20) {   // the others haven't sent this frame yet
            const char *w = strtab_get("vita.wifi.wait_players");
            game_text(SCREEN_W / 2, SCREEN_H / 2, 1.4f, 0xffffff, w ? w : "Waiting for the other players...");
        }
        if (g_netgame && net_desync_tick) {
            static int said;
            if (!said++) log_line("Wi-Fi game out of step from frame %u", net_desync_tick);
            const char *w = strtab_get("vita.wifi.out_of_step");
            game_text(SCREEN_W / 2, SCREEN_H - 20, 0.9f, 0xff5a50, w ? w : "Out of step with the other Vitas");
        }
#endif
        int ach = cc_achievement_take_new();
        if (trophies && cc_achievements() != trophies_synced) {
#ifdef CC_TROPHIES
            for (int id = 1; id <= CC_ACHIEVEMENTS; id++)
                if ((cc_achievements() & ~trophies_synced) >> id & 1) trophies_unlock(id);
#endif
            trophies_synced = cc_achievements();
        }
        if (ach) { ach_shown = ach; ach_timer = 30 * 4; }
        if (ach_timer > 0) {   // a banner at the top for four seconds
            if (ticked) ach_timer--;
            vita2d_draw_rectangle(SCREEN_W / 2 - 230, 14, 460, 64, RGBA8(0, 0, 0, 170));
            outlined_text(SCREEN_W / 2 - 210, 40, 0.9f, "Achievement unlocked");
            outlined_text(SCREEN_W / 2 - 210, 68, 1.0f, cc_achievement_name(ach_shown));
        }
        if (overlay) {
            unsigned ink = RGBA8(255, 255, 255, 255);
            vita2d_draw_rectangle(0, 0, SCREEN_W, 64, RGBA8(0, 0, 0, 150));
            vita2d_pgf_draw_textf(g_font, 12, 24, ink, 0.8f,
                                  "%.0f/30 fps   script %.1f ms   build %.1f ms   %d tris  %d draws  cpu-coloured %d   heap %u objs",
                                  fps, shown_tick, shown_build, render_stats.triangles, render_stats.draws,
                                  render_stats.cx_tris, (unsigned)vm->heap_count);
            vita2d_pgf_draw_textf(g_font, 12, 50, ink, 0.8f, "%d files, %.1f MB GPU, heap %.1f of %d MB   %s",
                                  g_nmovies, g_gpu_bytes / 1048576.0, mallinfo().uordblks / 1048576.0,
                                  _newlib_heap_size_user >> 20, g_last_error);
        }
        vita2d_end_drawing();
        vita2d_swap_buffers();

        frames++;
        now = sceKernelGetProcessTimeWide();
        // Hitches: a game frame shown more than 50 ms after the one before (one missed at 30 fps)
        // goes to the log with where the time went, to find what stalls on the Vita.
        static SceUInt64 last_shown;
        if (ticked && !pause.open && last_shown && now - last_shown > 50000)
            log_line("[%u] slow frame %.0f ms: script %.1f gc %.1f build %.1f draw+swap %.1f ms, %d tris%s",
                     (unsigned)vm->tick, (now - last_shown) / 1000.0f, hitch_script, hitch_gc, hitch_build,
                     (now - draw_t0) / 1000.0f, render_stats.triangles, hitch_what);
        if (ticked && !pause.open && last_shown && now - last_shown > 50000 && audio_sfx_pending())
            log_line("  (%d sounds loading)", audio_sfx_pending());
        if (pause.open) last_shown = 0;
        else if (ticked) last_shown = now;
        if (now - last >= 1000000) {
            fps = frames * 1000000.0f / (now - last);
            shown_tick = ticks ? tick_ms / ticks : 0;
            shown_build = ticks ? build_ms / ticks : 0;
            tick_ms = build_ms = 0;
            frames = ticks = 0;
            last = now;
        }
    }
    if (exit_to_title) {   // the game is left: everything of it goes, then the title's menu
#ifdef CC_NET
        if (g_netgame) {   // leave the Wi-Fi game, and this Vita's own profile back from its save
            net_leave();
            netplay_end();
            cc_storage_clear();
            cc_storage_load(SAVE_PATH);
            g_netgame = 0;
        } else
#endif
        if (cc_game_mode == CC_MODE_VOLLEYBALL) {   // the matches leave the story as it was saved
            cc_storage_clear();
            cc_storage_load(SAVE_PATH);
        }
        // Nothing loads while the files go: a file still loading is taken in first. The title's
        // own files stay (as if just preloaded), so the title loads nothing in the background:
        // a background load there (lobby.swf) froze the Vita in the title's drawing, ~3 s in.
        if (g_bg.state != BG_IDLE) bg_finish();
        for (int i = 0; i < g_nmovies; i++)
            for (int j = 0; j < PRELOAD_TITLE; j++)
                if (!g_movies[i].keep && !strcasecmp(g_movies[i].name, g_preload[j])) g_movies[i].keep = 2;
        EXIT_TRACE("exit: leaving the game");
        vita2d_wait_rendering_done();
        audio_sfx_stop(NULL);
        if (cc_music_stop) cc_music_stop();
        EXIT_TRACE("exit: sound stopped");
        as_free(vm);
        EXIT_TRACE("exit: game freed");
        free_unused_movies(NULL, &cache, cache_mem, cache_bytes);
        EXIT_TRACE("exit: files freed");
        memset(cc_keys, 0, sizeof cc_keys);
        render_begin(&vb);   // its meshes were in the files just freed
        from_game = 1;
        goto new_game;
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
    log_flush_wait();
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
