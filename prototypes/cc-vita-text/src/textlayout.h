// Text layout with inline button glyphs.
//
// Splits a UTF-8 string into runs of plain text and {token} glyphs (see
// prompts.h), wraps it to a width, and aligns each line. Measuring is done
// through a callback, so it works with whatever font the engine draws with
// (the game's own DefineFont glyphs on the Vita).
//
// "{{" writes a literal "{". An unknown {token} is kept as plain text, so a
// typo shows on screen instead of vanishing.
#ifndef TEXTLAYOUT_H
#define TEXTLAYOUT_H

#include <stdint.h>

typedef struct {
    // Advance width of one character, in the same units as everything else.
    float (*advance)(void *ud, uint32_t codepoint);
    void *ud;
    float line_height;    // distance between baselines
    float glyph_size;     // button glyph height; usually ~1.15x the cap height
    float glyph_raise;    // how far above the baseline the glyph's centre sits
                          // (about half the cap height)
    float glyph_pad;      // gap on each side of a glyph
} TextMetrics;

enum { RUN_TEXT, RUN_GLYPH };
enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

typedef struct {
    uint8_t kind;      // RUN_TEXT or RUN_GLYPH
    uint8_t glyph;     // PromptGlyph, for RUN_GLYPH
    uint16_t line;
    int start, len;    // RUN_TEXT: bytes of the source string to draw
    float x;           // left edge (text: pen position)
    float y;           // baseline
    float w;           // width (glyph: including its pads)
} TextRun;

typedef struct {
    float width, height;   // size of the laid-out block
    int lines;
    int nruns;             // runs written (may be < needed if max_runs ran out)
    int truncated;         // 1 if max_runs was too small
} TextBlock;

// Lay out text. wrap_width <= 0 means no wrapping (only \n breaks lines).
// box_width is the width alignment is measured against; <= 0 uses the
// widest line. The first baseline sits at y = line_height * 0.8.
TextBlock text_layout(const char *utf8, const TextMetrics *m,
                      float wrap_width, int align, float box_width,
                      TextRun *runs, int max_runs);

// Draw laid-out runs through two callbacks, offset by (ox, oy) (the top-left
// of the block). Text runs get their bytes and baseline; glyphs get their
// left edge, vertical centre and height (pads already applied).
typedef void (*TextDrawFn)(void *ud, const char *s, int len, float x, float baseline);
typedef void (*GlyphDrawFn)(void *ud, int glyph, float x, float y_center, float h);
void text_draw_runs(const char *utf8, const TextMetrics *m,
                    const TextRun *runs, int nruns, float ox, float oy,
                    TextDrawFn draw_text, GlyphDrawFn draw_glyph, void *ud);

// Decode one UTF-8 character starting at s[*i] and advance *i. Bad bytes
// come back as U+FFFD.
uint32_t utf8_next(const char *s, int len, int *i);

#endif
