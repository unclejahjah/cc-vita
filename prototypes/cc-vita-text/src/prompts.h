// PlayStation button prompts: the glyph set, the {token} names used in
// strings, and a texture atlas of the glyphs drawn procedurally at startup
// (original art made from circles and lines, so no image files ship).
//
// Tokens, case-insensitive, written as {name} inside any string:
//   Buttons:  cross circle square triangle l r start select
//             dpad up down left right updown leftright lstick rstick
//   Actions:  jump accept light heavy block magic back projectile pause
//             (resolved through the action table, so prompts follow the
//             control mapping if it ever changes)
//   Xbox:     a b x y lb rb lt rt back_xbox start_xbox
//             (for text taken from the PC/Xbox versions: A=cross, B=circle,
//             X=square, Y=triangle, LB/LT=L, RB/RT=R, Back=select)
#ifndef PROMPTS_H
#define PROMPTS_H

#include <stdint.h>

typedef enum {
    PG_CROSS, PG_CIRCLE, PG_SQUARE, PG_TRIANGLE,
    PG_L, PG_R, PG_START, PG_SELECT,
    PG_DPAD, PG_DPAD_UP, PG_DPAD_DOWN, PG_DPAD_LEFT, PG_DPAD_RIGHT,
    PG_DPAD_UPDOWN, PG_DPAD_LEFTRIGHT,
    PG_LSTICK, PG_RSTICK,
    PG_COUNT
} PromptGlyph;

typedef enum {
    PA_JUMP, PA_ACCEPT, PA_LIGHT, PA_HEAVY, PA_BLOCK, PA_MAGIC,
    PA_BACK, PA_PROJECTILE, PA_PAUSE,
    PA_COUNT
} PromptAction;

// Sets the default action table (the Vita control mapping):
// Cross jump/accept, Square light, Triangle heavy, R block, L magic,
// Circle back/projectile, Start pause.
void prompts_init(void);

// Rebind one action, e.g. if the controls become configurable.
void prompts_bind(PromptAction a, PromptGlyph g);

// Glyph for a token name (without braces), or -1 if it isn't one.
int prompts_lookup(const char *name, int len);

// Make a character draw as a glyph, for text that carries button icons as
// special characters (for example private-use codepoints in the game's
// font). Up to 32 mappings; mapping a codepoint again replaces it.
void prompts_map_codepoint(uint32_t cp, PromptGlyph g);

// Glyph for a character, or -1 if it isn't mapped.
int prompts_lookup_codepoint(uint32_t cp);

// Read mappings from the string table: every key "glyph.U+XXXX" whose
// value is a token name, e.g. "glyph.U+E000 = cross". Returns the count.
int prompts_map_from_strtab(void);

// Width of a glyph relative to its height (1.0 for round buttons,
// 2.0 for Start/Select, which are wide pills).
float prompts_aspect(PromptGlyph g);

// ---- Atlas ----
// Glyph cells are PROMPT_CELL px tall (and PROMPT_CELL * aspect wide).
#define PROMPT_CELL 64
#define PROMPT_ATLAS_W 512
#define PROMPT_ATLAS_H 192

typedef struct { int x, y, w, h; } PromptRect;

// Pixel rectangle of a glyph inside the atlas.
PromptRect prompts_rect(PromptGlyph g);

// Draw every glyph into rgba (PROMPT_ATLAS_W * PROMPT_ATLAS_H * 4 bytes,
// straight alpha, rows top to bottom). Takes a few ms; call once.
void prompts_build_atlas(uint8_t *rgba);

// Corners of a glyph quad, for engines that draw textured triangles with a
// 2D affine matrix m = {a, b, c, d, tx, ty} (x' = a*x + c*y + tx,
// y' = b*x + d*y + ty, as in Flash). The glyph's left edge is at x, its
// vertical centre at y_center, and it is h units tall. out[i] = {x, y, u, v}
// for corners top-left, top-right, bottom-right, bottom-left; u and v are
// 0..1 across the atlas.
void prompts_glyph_quad(PromptGlyph g, float x, float y_center, float h,
                        const float m[6], float out[4][4]);

#endif
