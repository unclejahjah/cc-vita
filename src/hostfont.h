// The executable's own text font (data/fonts/synjUnicode_20.fnt + _0.dds from the
// user's PC copy): a BMFont whose glyphs are copied into one small atlas.
// The SWF text fields carry placeholder fonts; the game draws all text with this.
#ifndef CC_HOSTFONT_H
#define CC_HOSTFONT_H

#include "swf.h"

typedef struct {
    int present;
    int16_t xoff, yoff, advance;    // font pixels
    BitmapFill box;                 // two triangles in font pixels (top-left origin), atlas texture coordinates
} HostGlyph;

typedef struct HostFont {
    int size;                       // em height in pixels
    int line_height, base;          // pixels; base = baseline below the top of a line
    Bitmap atlas;                   // straight RGBA (white glyphs, coverage in alpha)
    HostGlyph glyphs[256];          // Latin-1
} HostFont;

// Builds the font from the two files; NULL (and a message in err) if they can't be read.
HostFont *hostfont_load(const char *fnt_path, const char *dds_path, char *err, int errlen);

#endif
