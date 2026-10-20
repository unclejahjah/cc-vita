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
#include <psp2/ctrl.h>
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
#include "hostfont.h"
#include "pause_menu.h"
#include "render.h"
#include "strtab.h"
#ifdef CC_TROPHIES
#include "trophies.h"
#endif

// parsed SWFs and the script heap (meshes live in GPU memory blocks outside it)
int _newlib_heap_size_user = 160 * 1024 * 1024;

#define DATA_DIR "ux0:data/CastleCrashers/"
#define SAVE_PATH DATA_DIR "save.dat"   // the game's storage (profiles, levels, unlocks)
#define ACH_PATH DATA_DIR "achievements.dat"
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
static struct { char name[64]; Movie *m; GpuBlock gpu; int keep; } g_movies[MAX_MOVIES];
static int g_nmovies;
static char g_last_error[256];

// ---- the log file -------------------------------------------------------------------
// ux0:data/CastleCrashers/log.txt: each distinct warning once, the files loaded and freed,
// and the animal orbs each level places, for bugs that only show on the Vita. The run
// before is kept as log_prev.txt. Lines collect in memory and go to the card every few
// seconds (log_flush): a write per line cost frames in fights.
#define LOG_PATH DATA_DIR "log.txt"
#define LOG_MAX (1 << 20)
static SceUID g_log = -1;
static int g_log_bytes, g_log_used;
static char g_log_buf[16384];
static uint32_t g_log_seen[2048];
static int g_log_nseen;

static void log_open(void) {
    sceIoRemove(DATA_DIR "log_prev.txt");
    sceIoRename(LOG_PATH, DATA_DIR "log_prev.txt");
    g_log = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
}

static void log_flush(void) {
    if (g_log >= 0 && g_log_used) sceIoWrite(g_log, g_log_buf, g_log_used);
    g_log_used = 0;
}

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
        *is_level = i == 1;   // levels/: console_version reads true in its scripts (cc_host.c)
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
    cc_buttons_playstation(m, CC_BTN_STYLE_COLOURED);   // the game's button art as PlayStation buttons
    cc_menu_vita_badge(m);                              // "Vita Edition" on the title
    render_tessellate_all(m);
    return m;
}

// uploads its meshes and keeps it
static Movie *register_movie(const char *base, const char *path, Movie *m) {
    log_line("load %s%s (heap %u KB used)", path, m->truncated ? " (cut short)" : "",
             (unsigned)(mallinfo().uordblks >> 10));
    GpuBlock *blk = &g_movies[g_nmovies].gpu;
    blk->uid = -1;
    if (render_upload(m, mesh_alloc, blk)) render_release_cpu(m);
    snprintf(g_movies[g_nmovies].name, sizeof g_movies[0].name, "%s", base);
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
    int is_level;
    volatile int state;
    Movie *m;
} g_bg;
static SceUID g_bg_sema = -1;

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
    return register_movie(g_bg.base, g_bg.path, m);
}

static Movie *cached_movie(const char *base) {
    for (int i = 0; i < g_nmovies; i++)
        if (!strcasecmp(g_movies[i].name, base)) return g_movies[i].m;
    return NULL;
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
    g_bg.m = NULL;
    g_bg.state = BG_RUNNING;
    __sync_synchronize();
    sceKernelSignalSema(g_bg_sema, 1);
    return 1;
}

static Movie *vita_loader(AsVm *vm, const char *url) {
    const char *base = strrchr(url, '/');
    base = base ? base + 1 : url;
    Movie *m = cached_movie(base);
    if (m) return m;
    if (g_bg.state != BG_IDLE) {   // never two loads at once (the tessellator is shared)
        int same = !strcasecmp(g_bg.base, base);
        m = bg_finish();
        if (same && m) return m;
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
    return unused;
}

// ---- controls -> the key codes f_GetButtons gives port 1 ---------------------------

enum {
    K_ACCEPT = 2, K_BACK = 3, K_LEFT = 37, K_UP = 38, K_RIGHT = 39, K_DOWN = 40,
    K_START = 49, K_SELECT = 53, K_PUNCH1 = 65, K_R2 = 66, K_L2 = 67, K_PROJECTILE = 68,
    K_MAGIC = 69, K_SHIELD = 81, K_JUMP = 83, K_PUNCH2 = 87,
    // the light-tilt directions (f_Walk: these walk, the full ones above run)
    K_WALK_LEFT = 117, K_WALK_UP = 118, K_WALK_RIGHT = 119, K_WALK_DOWN = 120,
};

#define STICK_DEAD 40   // of 128: below this the stick is centred
#define STICK_RUN 100   // at or beyond this it's a full tilt

static void map_pad(const SceCtrlData *pad, int debug, int in_game) {
    memset(cc_keys, 0, sizeof cc_keys);
    if (debug) return;
    unsigned b = pad->buttons;
    int lx = pad->lx - 128, ly = pad->ly - 128;
    // the console scripts run on a full tilt and walk on a light one; the D-pad runs. Outside a
    // level (map, stat screen, menus) only the full directions mean anything: any tilt is one.
    int run = in_game ? STICK_RUN : STICK_DEAD;
    if ((b & SCE_CTRL_LEFT) || lx <= -run) cc_keys[K_LEFT] = 1;
    else if (lx <= -STICK_DEAD) cc_keys[K_WALK_LEFT] = 1;
    if ((b & SCE_CTRL_RIGHT) || lx >= run) cc_keys[K_RIGHT] = 1;
    else if (lx >= STICK_DEAD) cc_keys[K_WALK_RIGHT] = 1;
    if ((b & SCE_CTRL_UP) || ly <= -run) cc_keys[K_UP] = 1;
    else if (ly <= -STICK_DEAD) cc_keys[K_WALK_UP] = 1;
    if ((b & SCE_CTRL_DOWN) || ly >= run) cc_keys[K_DOWN] = 1;
    else if (ly >= STICK_DEAD) cc_keys[K_WALK_DOWN] = 1;
    // the right stick stands in for L2/R2 (switching items). The script steps once per
    // press and waits for the release, so a flick past the deadzone is one step and the
    // stick re-arms when it comes back to the centre.
    int rx = pad->rx - 128;
    if (rx <= -STICK_RUN + 20) cc_keys[K_L2] = 1;
    if (rx >= STICK_RUN - 20) cc_keys[K_R2] = 1;
    if (b & SCE_CTRL_CROSS) cc_keys[K_ACCEPT] = cc_keys[K_JUMP] = 1;
    if (b & SCE_CTRL_CIRCLE) cc_keys[K_BACK] = cc_keys[K_PROJECTILE] = 1;
    if (b & SCE_CTRL_SQUARE) cc_keys[K_PUNCH1] = 1;
    if (b & SCE_CTRL_TRIANGLE) cc_keys[K_PUNCH2] = 1;
    // the PlayStation layout's sides: block on the left (L2), magic on the right (R2)
    if (b & SCE_CTRL_LTRIGGER) cc_keys[K_SHIELD] = 1;
    if (b & SCE_CTRL_RTRIGGER) cc_keys[K_MAGIC] = 1;
    if (b & SCE_CTRL_START) cc_keys[K_START] = 1;
}

// ---- pause menu ----------------------------------------------------------------------
// The original draws its pause menu in castle.exe (PAUSE.swf only holds the art) and
// tells the scripts through g_nPort<n>State: 2 paused, 1 playing, 3 quit to the map.

typedef struct {
    int open;
    PauseMenu m;               // the menu (pause_menu.c)
    DrawList ui;               // this frame's backdrop, words and button prompts
    DrawList art;              // the pause book (controls and achievements pages)
    int have_art;
    float book[4];             // where the book is on screen
} Pause;

static int in_level(AsVm *vm) {
    int found;
    AsVal g = as_resolve_path(vm, vm->root, "loader.game.game", &found);
    return found && g.t == V_OBJ && g.o->type == OBJ_MC;
}

// the world map (MAP.swf in the level slot): the executable's pause menu opens there too
static int in_map(AsVm *vm) {
    int found;
    AsVal l = as_resolve_path(vm, vm->root, "loader", &found);
    Movie *map = cached_movie("MAP.swf");
    return found && map && l.t == V_OBJ && l.o->type == OBJ_MC && ((AsMc *)l.o)->movie == map;
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

static void pause_set_state(AsVm *vm, int state) { cc_set_flash_global(vm, "g_nPort1State", as_num(state)); }

// the book pages: the old pages of controls and achievements over the pause book
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
    if (page == PG_CONTROLS) {
        static const char *lines[] = {
            "Left stick: walk / run",    "Square: light attack",  "Triangle: heavy attack",
            "Cross: jump",               "Circle: use item",      "L: block",
            "R (hold) + attack: magic",  "Right stick left/right: switch item", "Start: pause",
        };
        for (int i = 0; i < 9; i++) vita2d_pgf_draw_text(g_font, x0, y0 + i * 26, ink, 0.9f, lines[i]);
        vita2d_pgf_draw_text(g_font, x0, y0 + 9 * 26 + 14, hi, 0.9f, "Circle: back");
    } else if (page == PG_ACHIEVEMENTS) {
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
}

// one frame of the open menu: 1 while it stays open, 0 closed, 2 back to the title
static int pause_input(Pause *ps, AsVm *vm, unsigned pressed, float dt) {
    unsigned b = (pressed & SCE_CTRL_UP ? PM_UP : 0) | (pressed & SCE_CTRL_DOWN ? PM_DOWN : 0) |
                 (pressed & (SCE_CTRL_CROSS | SCE_CTRL_START) ? PM_ACCEPT : 0) | (pressed & SCE_CTRL_CIRCLE ? PM_BACK : 0);
    int sound;
    int r = pause_menu_update(&ps->m, b, dt, &sound);
    if (sound >= 0) audio_sfx_play(sound ? "sound_player_select.wav" : "sound_toggle_b.wav", 1, 0, 0, ps);
    if (r == PM_OPEN) return 1;
    ps->open = 0;
    if (r == PM_TO_TITLE) return 2;
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
    if (log_first(msg)) log_line("[%u] %s", vm ? (unsigned)vm->tick : 0, msg);
}

// the level's animal orbs (animal1, animal2... on the game layer), logged when they change:
// they show in the PC harness but not on the Vita
static void log_animals(AsVm *vm) {
    static char last[512];
    char now[512];
    int len = 0, found;
    now[0] = 0;
    AsVal g = as_resolve_path(vm, vm->root, "loader.game.game", &found);
    if (found && g.t == V_OBJ && g.o->type == OBJ_MC) {
        AsMc *game = (AsMc *)g.o;
        for (int i = 0; i < game->ndisp && len < (int)sizeof now - 96; i++) {
            AsMc *c = game->disp[i].mc;
            if (!c || !c->name || strncasecmp(c->name->s, "animal", 6)) continue;
            len += snprintf(now + len, sizeof now - len, " %s(frame %d/%d at %.0f,%.0f depth %d%s%s%s alpha %.0f)",
                            c->name->s, c->frame + 1, c->def ? c->def->nframes : 0, c->m.tx / 20, c->m.ty / 20,
                            c->depth, c->visible ? "" : " hidden", c->removed ? " removed" : "",
                            c->ndisp ? "" : " empty", c->alpha);
        }
    }
    if (strcmp(now, last)) {
        log_line("[%u] animals:%s", (unsigned)vm->tick, now[0] ? now : " none");
        snprintf(last, sizeof last, "%s", now);
    }
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

static void title_screen(DrawList *vb, RecolourCache *cache, void *cache_mem, size_t cache_bytes) {
    Movie *m = vita_loader(NULL, "MENU.swf");
    if (!m) return;
    AsVm *t = as_new(vita_loader);
    t->sound_start = vita_sound_start;
    t->sound_stop = vita_sound_stop;
    t->warn = vita_warn;
    cc_host_init(t);
    as_render_init(t);
    t->root = as_mc_new(t, m, &m->root, NULL, 0);
    cc_native_root_functions(t, &t->root->obj);
    const char *prompt = strtab_number(1);   // the game's "Press START"
    if (!prompt) prompt = "Press START";
    const char *unavailable = strtab_get("vita.online.unavailable");
    if (!unavailable) unavailable = "Not available on PS Vita";
    AsMc *panel = NULL;
    int item = 0, message = 0;
    static Pause controls;   // Help & Options: the pause book's controls page
    SceCtrlData pad, old = {0};
    sceCtrlPeekBufferPositive(0, &old, 1);
    SceUInt64 prev = sceKernelGetProcessTimeWide(), acc = 0;
    const SceUInt64 tick_us = 1000000 / 30;
    unsigned frame = 0;
    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pressed = pad.buttons & ~old.buttons;
        int ly = pad.ly - 128, lx = pad.lx - 128, oly = old.ly - 128, olx = old.lx - 128;
        if (ly < -80 && oly >= -80) pressed |= SCE_CTRL_UP;   // the stick moves once per push
        if (ly > 80 && oly <= 80) pressed |= SCE_CTRL_DOWN;
        if (lx < -80 && olx >= -80) pressed |= SCE_CTRL_LEFT;
        if (lx > 80 && olx <= 80) pressed |= SCE_CTRL_RIGHT;
        old = pad;
        if (controls.open) {
            if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS | SCE_CTRL_START)) controls.open = 0;
        } else if (!panel) {
            if (pressed & (SCE_CTRL_START | SCE_CTRL_CROSS)) {
                // left of the knights, over the (hidden, empty) news box
                panel = cc_menu_attach(t, m->stage_w * 0.226f, m->stage_h * 0.635f);
                if (!panel) break;   // no panel in this copy of the file: straight into the game
                cc_menu_highlight(t, panel, item);
            }
        } else {
            int moved = 0;
            if (pressed & (SCE_CTRL_UP | SCE_CTRL_LEFT)) { item = (item + CC_MENU_ITEMS - 1) % CC_MENU_ITEMS; moved = 1; }
            if (pressed & (SCE_CTRL_DOWN | SCE_CTRL_RIGHT)) { item = (item + 1) % CC_MENU_ITEMS; moved = 1; }
            if (moved) cc_menu_highlight(t, panel, item);
            if (pressed & SCE_CTRL_CROSS) {
                if (item == CC_MENU_PLAY) break;
                if (item == CC_MENU_QUIT) {
                    vita2d_wait_rendering_done();
                    audio_shutdown();
                    log_flush();
                    vita2d_fini();
                    sceKernelExitProcess(0);
                }
                if (item == CC_MENU_OPTIONS) {
                    controls.open = 1;
                    if (!controls.have_art) pause_build_art(&controls);
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
            as_render_mc(t->root, screen, CX_IDENTITY, vb);
        }
        vita2d_set_clear_color(RGBA8(m->bg[0], m->bg[1], m->bg[2], 255));
        vita2d_start_drawing();
        vita2d_clear_screen();
        draw_list(vb);
        frame++;
        if (!panel && (frame / 30) % 2 == 0) {   // blinking, over the news box at the bottom left
            int w = vita2d_pgf_text_width(g_font, 1.2f, prompt);
            outlined_text(180 - w / 2, 470, 1.2f, prompt);
        }
        if (message > 0) {
            message--;
            int w = vita2d_pgf_text_width(g_font, 1.0f, unavailable);
            outlined_text((SCREEN_W - w) / 2, SCREEN_H - 24, 1.0f, unavailable);
        }
        if (controls.open) pause_book_draw(&controls, PG_CONTROLS, 1);
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }
    vita2d_wait_rendering_done();
    if (cc_music_stop) cc_music_stop();
    memset(cc_keys, 0, sizeof cc_keys);
    as_free(t);
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
    log_open();
    log_line("cc_vita started");
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
        log_flush();
        vita2d_fini();
        sceKernelExitProcess(0);
        return 0;
    }

    cc_storage_load(SAVE_PATH);
    cc_achievements_load(ACH_PATH);
#ifdef CC_TROPHIES
    int trophies = trophies_init();   // with NoTrpDrm; earned achievements are synced below
#else
    int trophies = 0;
#endif
    unsigned trophies_synced = 0;
    // the game's words (from the user's castle.exe by extract_strings.py), then Vita wording
    strtab_load(DATA_DIR "strings_en.txt");
    strtab_load(DATA_DIR "strings_vita_en.txt");
    audio_init(DATA_DIR);
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

new_game:   // the title again after Exit to Main Menu
    title_screen(&vb, &cache, cache_mem, cache_bytes);

    AsVm *vm = as_new(vita_loader);
    vm->rand_state = (uint32_t)sceKernelGetProcessTimeWide() ^ 0x9E3779B9u;   // Flash's random() differs each session
    vm->sound_start = vita_sound_start;
    vm->sound_stop = vita_sound_stop;
    vm->warn = vita_warn;
    vm->prefetch = vita_prefetch;   // files load on a second thread while the game runs
    cc_host_init(vm);
    as_render_init(vm);
    vm->root = as_mc_new(vm, start, &start->root, NULL, 0);
    cc_loading_attach(vm);   // the game's loading screen over level changes

    SceCtrlData pad, old = {0};
    int overlay = 0;
    static Pause pause;
    SceUInt64 last = sceKernelGetProcessTimeWide(), acc = 0, prev = last;
    float fps = 0, tick_ms = 0, build_ms = 0, shown_tick = 0, shown_build = 0;
    int frames = 0, ticks = 0;
    unsigned heat_clock = 0;
    int ach_shown = 0, ach_timer = 0;   // the "achievement unlocked" banner
    uint32_t last_free = 0;
    const SceUInt64 tick_us = 1000000 / 30;   // the game's frame rate
    int exit_to_title = 0;
    pause.open = 0;
    // PAUSE.swf is read, tessellated and uploaded now: on the first Start that took ~3 s
    host_movie("PAUSE.swf");
    if (!pause.have_art) pause_build_art(&pause);

    while (!exit_to_title) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pressed = pad.buttons & ~old.buttons;
        unsigned nav = pressed;   // menus: the stick moves once per push too
        if (pad.ly < 48 && old.ly >= 48) nav |= SCE_CTRL_UP;
        if (pad.ly > 208 && old.ly <= 208) nav |= SCE_CTRL_DOWN;
        old = pad;
        int debug = pad.buttons & SCE_CTRL_SELECT;
        if (debug && (pressed & SCE_CTRL_START)) break;
        if (debug && (pressed & SCE_CTRL_UP)) overlay = !overlay;
        // in a level and on the map, Start is the pause menu's (the executable's in the original)
        int level = in_level(vm), on_map = !level && in_map(vm);
        map_pad(&pad, debug, level);
        if (level || on_map) cc_keys[K_START] = 0;
        if (pause.open) {
            float dt = (sceKernelGetProcessTimeWide() - prev) / 1000000.0f;
            int r = pause_input(&pause, vm, nav, dt > 0.1f ? 0.1f : dt);
            if (r == 2) exit_to_title = 1;
            memset(cc_keys, 0, sizeof cc_keys);
        } else if ((level || on_map) && !debug && (pressed & SCE_CTRL_START) &&
                   !as_tobool(vm, cc_flash_global(vm, "g_bNoPause"))) {
            pause_open(&pause, on_map);
            pause_set_state(vm, 2);
            log_flush();   // the log is complete while paused (to copy it off by FTP)
            if (!pause.have_art) pause_build_art(&pause);
        }

        // run the game's frames that are due (at most two, so a slow frame doesn't snowball)
        SceUInt64 now = sceKernelGetProcessTimeWide();
        acc += now - prev;
        prev = now;
        if (acc > 2 * tick_us) acc = 2 * tick_us;
        if (pause.open) acc = 0;   // the game stands still under the menu
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
            // every few seconds, once the heap is half full: let go of the files left behind
            // (levels played earlier, the title). Below that they stay, so going back to the
            // map or replaying a level doesn't load them again.
            if (vm->tick % 30 == 0) log_animals(vm);
            if (vm->tick % 150 == 0) log_flush();
            if (vm->tick - last_free >= 90 && mallinfo().uordblks > (size_t)_newlib_heap_size_user / 2) {
                last_free = vm->tick;
                free_unused_movies(vm, &cache, cache_mem, cache_bytes);
            }
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
        draw_frame(&vb, heat_clock++);
        if (pause.open) pause_draw(&pause);
        int ach = cc_achievement_take_new();
        if (trophies && cc_achievements() != trophies_synced) {
#ifdef CC_TROPHIES
            for (int id = 1; id <= CC_ACHIEVEMENTS; id++)
                if ((cc_achievements() & ~trophies_synced) >> id & 1) trophies_unlock(id);
#endif
            trophies_synced = cc_achievements();
        }
        if (ach) { ach_shown = ach; ach_timer = 60 * 4; }
        if (ach_timer > 0) {   // a banner at the top for four seconds
            ach_timer--;
            vita2d_draw_rectangle(SCREEN_W / 2 - 230, 14, 460, 64, RGBA8(0, 0, 0, 170));
            outlined_text(SCREEN_W / 2 - 210, 40, 0.9f, "Achievement unlocked");
            outlined_text(SCREEN_W / 2 - 210, 68, 1.0f, cc_achievement_name(ach_shown));
        }
        if (overlay) {
            unsigned ink = RGBA8(255, 255, 255, 255);
            vita2d_draw_rectangle(0, 0, SCREEN_W, 64, RGBA8(0, 0, 0, 150));
            vita2d_pgf_draw_textf(g_font, 12, 24, ink, 0.8f,
                                  "%.0f fps   script %.1f ms   build %.1f ms   %d tris  %d draws  cpu-coloured %d   heap %u objs",
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
        vita2d_wait_rendering_done();
        audio_sfx_stop(NULL);
        if (cc_music_stop) cc_music_stop();
        as_free(vm);
        free_unused_movies(NULL, &cache, cache_mem, cache_bytes);
        memset(cc_keys, 0, sizeof cc_keys);
        render_begin(&vb);   // its meshes were in the files just freed
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
    log_flush();
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
