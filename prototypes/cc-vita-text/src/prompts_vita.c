// Vita side of the button prompts: uploads the glyph atlas to a vita2d
// texture and draws glyphs from it. Only built for the Vita.
#include "prompts_vita.h"

#include <string.h>
#include <vita2d.h>

static vita2d_texture *g_tex;

vita2d_texture *prompts_vita_init(void) {
    if (g_tex) return g_tex;
    g_tex = vita2d_create_empty_texture(PROMPT_ATLAS_W, PROMPT_ATLAS_H);
    if (!g_tex) return NULL;
    // Default format is A8B8G8R8: bytes in memory are R, G, B, A, which is
    // the atlas layout. The stride can be wider than the atlas.
    uint8_t *dst = vita2d_texture_get_datap(g_tex);
    unsigned stride = vita2d_texture_get_stride(g_tex);
    static uint8_t rgba[PROMPT_ATLAS_W * PROMPT_ATLAS_H * 4];
    prompts_build_atlas(rgba);
    for (int y = 0; y < PROMPT_ATLAS_H; y++)
        memcpy(dst + y * stride, rgba + y * PROMPT_ATLAS_W * 4, PROMPT_ATLAS_W * 4);
    vita2d_texture_set_filters(g_tex, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    return g_tex;
}

void prompts_vita_draw(PromptGlyph g, float x, float y_center, float h, unsigned int tint) {
    if (!g_tex || (unsigned)g >= PG_COUNT) return;
    PromptRect r = prompts_rect(g);
    float s = h / r.h;
    vita2d_draw_texture_tint_part_scale(g_tex, x, y_center - h / 2, r.x, r.y, r.w, r.h, s, s, tint);
}

void prompts_vita_free(void) {
    if (!g_tex) return;
    vita2d_wait_rendering_done();
    vita2d_free_texture(g_tex);
    g_tex = NULL;
}
