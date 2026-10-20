// Vita side of the button prompts (vita2d).
#ifndef PROMPTS_VITA_H
#define PROMPTS_VITA_H

#include <vita2d.h>

#include "prompts.h"

// Build the atlas and upload it (linear filtering). Call after vita2d_init.
// Safe to call twice; returns the texture, or NULL if out of GPU memory.
vita2d_texture *prompts_vita_init(void);

// Draw one glyph h pixels tall, left edge at x, vertically centred on
// y_center. tint multiplies the colours, e.g. RGBA8(255,255,255,a) to fade.
void prompts_vita_draw(PromptGlyph g, float x, float y_center, float h, unsigned int tint);

void prompts_vita_free(void);

#endif
