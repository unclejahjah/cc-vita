// Turns SWF display lists into a list of shape draws. Each draw is a cached
// mesh plus a matrix, so the GPU does the per-vertex transform.
#ifndef CC_RENDER_H
#define CC_RENDER_H

#include <stddef.h>

#include "swf.h"

// Same layout as vita2d_color_vertex. colour is 0xAABBGGRR.
typedef struct {
    float x, y, z;
    uint32_t color;
} Vertex;

// Masks use the stencil buffer. Each draw command covers a run of items
// drawn with stencil test "stored == ref", then applies op to the passing pixels.
// Masked content sits at ref = nesting level; a mask is drawn once with INCR
// (level -> level+1) before its content and again with DECR afterwards.
enum { STENCIL_KEEP, STENCIL_INCR, STENCIL_DECR };

typedef struct {
    int start, count;          // items
    uint8_t ref, op;
} DrawCmd;

// Same layout as vita2d_texture_vertex.
typedef struct {
    float x, y, z;
    float u, v;
} TexVertex;

// A shape draw: positions from vp (the uploaded mesh) or, if NULL, mesh->xy; colours
// from col or DrawList.cv[cv..] when a colour transform applies, else the mesh's own.
typedef struct {
    const Mesh *mesh;
    Mat m;                     // mesh twips -> screen pixels
    const Vertex *vp;          // uploaded vertices, or NULL
    int cv;                    // first of mesh->nverts recoloured colours in DrawList.cv, or -1
    const BitmapFill *bf;      // a bitmap fill (mesh is bf->mesh): drawn textured, multiplied by tint
    float tint[4];
    const uint32_t *col;       // cached recoloured colours (GPU-visible), or NULL
} DrawItem;

// Recoloured vertex colours, kept across frames. Colour transforms are usually
// constant, so each (mesh, transform) pair is recoloured once. The memory comes
// from the caller (GPU-visible on the Vita); when it fills up, new pairs are
// recoloured every frame instead.
typedef struct {
    uint32_t *mem;
    int cap, used;             // colours
    struct RecolourEntry *table;
    int table_size, entries;
} RecolourCache;

void recolour_cache_init(RecolourCache *rc, void *mem, size_t bytes);
void recolour_cache_free(RecolourCache *rc);   // frees the table, not mem

typedef struct {
    DrawItem *items;
    int n, cap;
    uint32_t *cv;              // this frame's recoloured colours (colour transforms, masks) the cache missed
    int ncv, cvcap;
    DrawCmd *cmd;
    int ncmd, cmdcap;
    int level;                 // current mask nesting
    int masking;               // emitting mask geometry (no colour, no nested masks)
    float view[4];             // shapes entirely outside x0,y0,x1,y1 are skipped (unset = no culling)
    RecolourCache *cache;      // optional
    // Character parts pinned to one frame while drawing (like the game's gotoAndStop
    // on them): a clip stopped on frame 0 with exactly part_count[i] frames shows
    // frame part_frame[i]. Each pose has its own copy of a part (26 torso clips in
    // PLAYER.swf), but every copy has one frame per character, so the count finds them all.
    int nparts;
    uint16_t part_count[8], part_frame[8];
    // masks opened with render_mask_push, innermost last: their first item and count
    int nmask;
    int mask_start[16], mask_count[16];
    // a "Heat" clip (AddShader): its own items heat_at..heat_end-1 shimmer, inside heat[]
    // (screen x0, y0, x1, y1); heat_at is -1 if there's none this frame
    int heat_at, heat_end;
    float heat[4];
} DrawList;

typedef struct {
    int shapes_tessellated;
    int triangles;
    int draws;                 // items
    int masks;
    int culled;                // shapes skipped as off-screen
    int cx_tris;               // triangles recoloured on the CPU this frame (cache misses)
    int cached_tris;           // triangles drawn from the recolour cache
} RenderStats;

void render_begin(DrawList *out);
void render_display_list(Movie *m, const Frame *f, Mat mtx, CXform cx, int tick, DrawList *out);
void render_char(Movie *m, int cid, Mat mtx, CXform cx, int tick, DrawList *out);
void render_shape(Shape *s, Mat mtx, CXform cx, DrawList *out);   // one shape (e.g. a font glyph)
void render_bitmap_fill(const BitmapFill *bf, Mat mtx, CXform cx, DrawList *out);   // one textured mesh
extern int (*render_thread_slot)(void);   // 0/1: the tessellator for the calling thread (NULL: one)
void render_tessellate_all(Movie *m);   // optional warm-up so the first frames don't stall
const float *render_shape_bounds(Shape *s);   // min x, min y, max x, max y in twips (tessellates if needed)

// Masks for callers that walk their own display list (the ActionScript runtime):
// push, emit the mask's shapes, apply, emit the masked content, pop.
void render_mask_push(DrawList *out);
void render_mask_apply(DrawList *out);
void render_mask_pop(DrawList *out);

// Copies every tessellated mesh into one block from alloc (GPU-visible memory on
// the Vita) as Vertex arrays and points Mesh.gpu at them. Returns the block, or
// NULL if alloc failed (draws then fall back to recoloured copies).
void *render_upload(Movie *m, void *(*alloc)(size_t bytes, void *ctx), void *ctx);
// After render_upload: frees the outlines and CPU-side vertices of uploaded shapes
// (recolouring then reads the uploaded copy). Halves a movie's memory.
void render_release_cpu(Movie *m);
// After the block at old (size bytes) was copied to new: points Mesh.gpu into new.
void render_relocate(Movie *m, void *old, size_t size, void *new_block);

// Vertex i of an item, transformed to the screen (for software rasterising).
Vertex render_item_vertex(const DrawList *dl, const DrawItem *it, int i);   // bitmap items: colour = tint
void render_item_uv(const DrawItem *it, int i, float uv[2]);                // bitmap items only

extern RenderStats render_stats;

#endif
