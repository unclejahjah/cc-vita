// Drawing the ActionScript runtime's clip tree with the shape renderer.
#ifndef CC_AS_RENDER_H
#define CC_AS_RENDER_H

#include "as.h"
#include "render.h"

void as_render_init(AsVm *vm);   // once, before drawing (text fields read script variables)
struct HostFont;
void as_render_set_font(const struct HostFont *hf);   // the game's own text font (optional)
// the game font's text outside any field (the executable's own menus): see as_render.c
void as_render_host_text(const char *s, float x, float y, float sx, float sy, int align, Mat m, CXform cx,
                         DrawList *out);
float as_render_host_text_width(const char *s);   // pixels of the 20 pixel font
float as_render_host_line_height(void);           // the same, 0 without the font
void as_render_mc(AsMc *mc, Mat parent, CXform cx, DrawList *out);
void render_mc_bounds(AsMc *mc, Mat m, float out[4]);

#endif
