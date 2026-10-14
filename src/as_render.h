// Drawing the ActionScript runtime's clip tree with the shape renderer.
#ifndef CC_AS_RENDER_H
#define CC_AS_RENDER_H

#include "as.h"
#include "render.h"

void as_render_init(AsVm *vm);   // once, before drawing (text fields read script variables)
struct HostFont;
void as_render_set_font(const struct HostFont *hf);   // the game's own text font (optional)
void as_render_mc(AsMc *mc, Mat parent, CXform cx, DrawList *out);
void render_mc_bounds(AsMc *mc, Mat m, float out[4]);

#endif
