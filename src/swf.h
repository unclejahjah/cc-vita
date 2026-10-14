// Minimal SWF reader for Castle Crashers' decompiled assets.
// C port of code/swfproto/swf.py. Reads game files at runtime; ships no data.
#ifndef CC_SWF_H
#define CC_SWF_H

#include <stddef.h>
#include <stdint.h>

typedef struct { float a, b, c, d, tx, ty; } Mat;   // x' = a*x + c*y + tx, y' = b*x + d*y + ty
typedef struct { float mul[4]; float add[4]; } CXform;

extern const Mat MAT_IDENTITY;
extern const CXform CX_IDENTITY;

enum { FILL_SOLID, FILL_LINEAR, FILL_RADIAL, FILL_BITMAP };

// DefineBitsLossless/Lossless2 pixels, straight (not premultiplied) RGBA.
typedef struct Bitmap {
    uint16_t id;
    int w, h;
    uint8_t *rgba;             // NULL once the platform has its own copy
    void *tex;                 // the platform's texture
} Bitmap;

typedef struct {
    int type;
    uint8_t color[4];          // solid colour, or fallback for bitmap fills
    Mat gmat;                  // gradient matrix (bitmap fills: bitmap pixels -> shape twips)
    int nstops;
    uint8_t ratio[15];
    uint8_t stop[15][4];
    float focal;
    uint16_t bitmap_id;        // bitmap fills
    uint8_t bitmap_clip, bitmap_smooth;
    Bitmap *bitmap;            // resolved after the whole file is read; NULL if missing
} FillStyle;

typedef struct {
    FillStyle style;
    int npoints;               // flattened outline points (twips)
    float *pts;                // x,y pairs
    int nloops;
    int *loop_start;           // nloops + 1 offsets into pts (in points)
} ShapeFill;

typedef struct {
    float width;
    uint8_t color[4];
    int npoints;
    float *pts;
    int npolys;
    int *poly_start;
} ShapeLine;

// Tessellated mesh, filled lazily by the renderer.
typedef struct {
    int nverts;
    float *xy;                 // triangle list, x,y pairs (twips)
    uint8_t *rgba;             // per-vertex colour (before colour transform); NULL for bitmap meshes
    float *uv;                 // bitmap meshes: texture coordinates per vertex
    float bounds[4];           // min x, min y, max x, max y of xy
    void *gpu;                 // uploaded copy: render Vertex records (TexVertex for bitmap meshes)
} Mesh;

// The part of a shape filled with a bitmap, drawn textured.
typedef struct {
    Bitmap *bitmap;
    uint8_t clip, smooth;
    Mesh mesh;
} BitmapFill;

typedef struct {
    uint16_t id;
    int nfills, nlines;
    ShapeFill *fills;
    ShapeLine *lines;
    Mesh mesh;                 // the colour fills and lines
    int nbfills;               // bitmap fills, drawn after the colour mesh
    BitmapFill *bfills;
    int tessellated;
} Shape;

typedef struct {
    uint16_t depth;
    uint16_t cid;
    uint16_t clip;             // clip depth (mask) or 0
    uint16_t ratio;
    uint16_t name;             // instance name: index into Movie.names, 0 = unnamed
    Mat m;
    CXform cx;
    uint32_t uid;              // same value while the timeline keeps this instance (moves/recolours)
    uint32_t events_off, events_len;   // onClipEvent handlers (PlaceObject2 clip actions) in Movie.data
} Place;

// A script in Movie.data.
typedef struct {
    int frame;                 // timeline frame it runs on (DoAction), or the sprite id (DoInitAction)
    uint32_t off, len;
} ActionBlock;

typedef struct {
    int count;
    Place *places;             // sorted by depth
} Frame;

typedef struct {
    char *name;
    int frame;
} Label;

typedef struct {
    uint16_t id;
    int nframes;
    Frame *frames;
    int nlabels;
    Label *labels;
    int first_stop;            // first frame whose script calls stop(), or -1
    int n_type;                // first-frame script "n_type = N" (collision segment type), else 0
    // The first-frame script registers the clip for the game's depth sorting:
    // f_AddNeutral/f_AddObject(this) sort it by its own y; f_Depth(this, N) sorts it as if at y = N.
    uint8_t depth_sort;        // DEPTH_NONE, DEPTH_OWN_Y or DEPTH_FIXED
    float depth_y;             // for DEPTH_FIXED
    int nactions;              // DoAction scripts, in timeline order
    ActionBlock *actions;
} Sprite;

enum { DEPTH_NONE, DEPTH_OWN_Y, DEPTH_FIXED };

enum { CHAR_NONE, CHAR_SHAPE, CHAR_SPRITE, CHAR_EDITTEXT, CHAR_FONT, CHAR_BITMAP };

// DefineFont2/3: glyph outlines (white fills) and the layout text fields use.
typedef struct {
    uint16_t id;
    char *name;
    int nglyphs;
    Shape **glyphs;            // in font units: em units to the em square
    uint16_t *codes;           // character code of each glyph
    int16_t *advance;          // font units (if has_layout)
    int ascent, descent, leading;
    int has_layout, bold, italic;
    int em;                    // 1024 (DefineFont2) or 20480 (DefineFont3)
} Font;

int font_glyph(const Font *f, int code);   // glyph index for a character, -1 if none

// DefineEditText: a dynamic text field.
typedef struct {
    uint16_t id;
    int32_t bounds[4];         // xmin, xmax, ymin, ymax (twips)
    uint16_t font, height;     // height in twips
    uint8_t color[4];
    uint8_t align;             // 0 left, 1 right, 2 center, 3 justify
    uint8_t html, multiline, wordwrap;
    uint16_t left_margin, right_margin, indent;   // twips
    int16_t leading;           // extra twips between lines
    char *var;                 // variable it shows, or NULL
    char *text;                // initial text
} EditText;

typedef struct {
    uint8_t type;
    uint16_t export;           // ExportAssets name: index into Movie.names, 0 = none
    void *ptr;
} Character;

typedef struct {
    int version;
    float fps;
    int stage_w, stage_h;      // pixels
    Character chars[65536];
    Sprite root;
    int truncated;             // file shorter than its header says
    uint8_t bg[3];             // SetBackgroundColor
    int nnames;                // instance names; names[0] is ""
    char **names;
    uint8_t *data;             // the whole file: scripts are run from here
    size_t len;
    int ninit;                 // DoInitAction scripts (frame = sprite id), in file order
    ActionBlock *init;
} Movie;

Movie *swf_load(const char *path, char *err, int errlen);
void swf_free(Movie *m);
int sprite_frame_at(const Sprite *s, int tick);
int sprite_find_label(const Sprite *s, const char *name);
const Place *frame_find_name(const Movie *m, const Frame *f, const char *name);

Mat mat_mul(Mat m, Mat n);      // apply n first, then m
CXform cx_mul(CXform outer, CXform inner);

#endif
