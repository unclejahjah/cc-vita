// Desktop test for the text and button-prompt modules.
// Runs checks on the string table, tokens and layout, then draws a preview
// screen (960x544, the Vita's resolution) to a PNG. A system TrueType font
// stands in for the game's font, which isn't available off the PC.
//
//   gcc -O2 -Isrc -Ithird_party/stb -o texttest tools/texttest.c
//       src/strtab.c src/prompts.c src/textlayout.c -lm
//   ./texttest data /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf out.png
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prompts.h"
#include "strtab.h"
#include "textlayout.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

// ---- Fixed-width metrics for the layout checks ----
static float mono_adv(void *ud, uint32_t cp) { (void)ud; (void)cp; return 10; }
static const TextMetrics k_mono = {mono_adv, NULL, 20, 16, 6, 2};   // glyph = 20 wide

static void checks(const char *tmpdir) {
    // String table: format, escapes, overrides
    const char *a = "\xEF\xBB\xBF# comment\n  title = Hello  \nmulti = one\\ntwo\nsp = \\sx\\s\nnoeq\n= nokey\n";
    CHECK(strtab_load_mem(a, (int)strlen(a)) == 3);
    CHECK(!strcmp(strtab_get("title"), "Hello"));
    CHECK(!strcmp(strtab_get("multi"), "one\ntwo"));
    CHECK(!strcmp(strtab_get("sp"), " x "));
    const char *b = "title = Bye\r\n";
    strtab_load_mem(b, (int)strlen(b));
    CHECK(!strcmp(strtab_get("title"), "Bye"));
    CHECK(strtab_get("nope") == NULL);

    // Numbered strings (from extract_strings.py)
    const char *num = "742 = Numbered\n";
    strtab_load_mem(num, (int)strlen(num));
    CHECK(!strcmp(strtab_number(742), "Numbered") && strtab_number(743) == NULL);

    // Field lookup order and the missing-strings log
    const char *c = "menu_play = Play\nMAIN:_root.m.t = Title\nOld text = New text\n";
    strtab_load_mem(c, (int)strlen(c));
    CHECK(!strcmp(strtab_field("MAIN", "_root.x", "menu_play", "fixme"), "Play"));
    CHECK(!strcmp(strtab_field("MAIN", "_root.m.t", "", "fixme"), "Title"));
    CHECK(!strcmp(strtab_field("MAIN", "_root.q", NULL, "Old text"), "New text"));
    char log[512];
    snprintf(log, sizeof log, "%s/missing.txt", tmpdir);
    remove(log);
    strtab_set_missing_log(log);
    CHECK(strtab_field("LOBBY", "_root.a", "v1", "fixme") == NULL);
    CHECK(strtab_field("LOBBY", "_root.a", "v1", "fixme") == NULL);   // logged once
    strtab_set_show_keys(1);
    CHECK(!strcmp(strtab_field("LOBBY", "_root.a", "v1", "fixme"), "LOBBY:_root.a"));
    strtab_set_show_keys(0);
    FILE *f = fopen(log, "rb");
    char buf[256] = "";
    if (f) { buf[fread(buf, 1, sizeof buf - 1, f)] = 0; fclose(f); }
    CHECK(!strcmp(buf, "# var=v1 placeholder=fixme\nLOBBY:_root.a = \n"));
    // the log loads back as a string file
    int before = strtab_count();
    CHECK(strtab_load(log) == 1 && strtab_count() == before + 1);
    strtab_set_missing_log(NULL);

    // Tokens and action bindings
    prompts_init();
    CHECK(prompts_lookup("Cross", 5) == PG_CROSS);
    CHECK(prompts_lookup("accept", 6) == PG_CROSS);
    CHECK(prompts_lookup("heavy", 5) == PG_TRIANGLE);
    CHECK(prompts_lookup("Y", 1) == PG_TRIANGLE);
    CHECK(prompts_lookup("bogus", 5) == -1);
    prompts_bind(PA_ACCEPT, PG_CIRCLE);
    CHECK(prompts_lookup("accept", 6) == PG_CIRCLE);
    prompts_init();

    // Layout
    TextRun r[32];
    TextBlock tb = text_layout("Press {start} now", &k_mono, 0, ALIGN_LEFT, 0, r, 32);
    CHECK(tb.nruns == 3 && r[0].kind == RUN_TEXT && r[1].kind == RUN_GLYPH && r[1].glyph == PG_START);
    CHECK(r[0].len == 6 && fabsf(r[1].x - 60) < 0.01f && fabsf(r[1].w - 36) < 0.01f);   // 16*2 + 2*2
    CHECK(r[2].start == 13 && fabsf(r[2].x - 96) < 0.01f && fabsf(tb.width - 136) < 0.01f);

    tb = text_layout("a {{b} {foo}", &k_mono, 0, ALIGN_LEFT, 0, r, 32);
    // "a " + "{" (from "{{") + "b} " + "{foo}" all text
    {
        char shown[64] = "";
        for (int i = 0; i < tb.nruns; i++) strncat(shown, "a {{b} {foo}" + r[i].start, r[i].len);
        CHECK(!strcmp(shown, "a {b} {foo}"));
    }

    // Wrap: "{cross}:" stays together; spaces at the break are dropped
    tb = text_layout("aaaa {cross}: bb", &k_mono, 60, ALIGN_LEFT, 0, r, 32);
    CHECK(tb.lines == 2);
    CHECK(tb.nruns == 3 && r[0].line == 0 && r[0].len == 4);
    CHECK(r[1].kind == RUN_GLYPH && r[1].line == 1 && r[1].x == 0);
    CHECK(r[2].line == 1 && r[2].len == 4 && !strncmp("aaaa {cross}: bb" + r[2].start, ": bb", 4));

    tb = text_layout("ab\nabcd", &k_mono, 0, ALIGN_CENTER, 0, r, 32);
    CHECK(tb.lines == 2 && fabsf(r[0].x - 10) < 0.01f && r[1].x == 0);
    CHECK(fabsf(r[1].y - r[0].y - 20) < 0.01f);

    // Icon characters mapped to glyphs
    const char *gm = "glyph.U+E001 = triangle\n";
    strtab_load_mem(gm, (int)strlen(gm));
    CHECK(prompts_map_from_strtab() == 1);
    tb = text_layout("Hit \xEE\x80\x81!", &k_mono, 0, ALIGN_LEFT, 0, r, 32);   // U+E001
    CHECK(tb.nruns == 3 && r[1].kind == RUN_GLYPH && r[1].glyph == PG_TRIANGLE && r[2].len == 1);

    // UTF-8
    int i = 0;
    CHECK(utf8_next("\xC3\xA9", 2, &i) == 0xE9 && i == 2);
    i = 0;
    CHECK(utf8_next("\xC3", 1, &i) == 0xFFFD && i == 1);

    // Glyph quad with a scale+translate matrix
    float m[6] = {2, 0, 0, 2, 100, 50}, q[4][4];
    prompts_glyph_quad(PG_START, 0, 10, 10, m, q);
    CHECK(q[0][0] == 100 && q[0][1] == 60 && q[2][0] == 140 && q[2][1] == 80);
    CHECK(q[0][2] == 0 && fabsf(q[2][2] - 0.25f) < 1e-6f && fabsf(q[2][3] - 1.0f) < 1e-6f);

    strtab_free();
}

// ---- Preview renderer ----

#define W 960
#define H 544
static float g_img[H][W][3];
static uint8_t g_atlas[PROMPT_ATLAS_W * PROMPT_ATLAS_H * 4];

typedef struct {
    stbtt_fontinfo *font;
    float px, scale;
    float rgb[3];
} Pen;

static float tt_adv(void *ud, uint32_t cp) {
    Pen *p = ud;
    int adv, lsb;
    stbtt_GetCodepointHMetrics(p->font, (int)cp, &adv, &lsb);
    return adv * p->scale;
}

static void blend(int x, int y, const float *rgb, float a) {
    if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
    for (int c = 0; c < 3; c++) g_img[y][x][c] = g_img[y][x][c] * (1 - a) + rgb[c] * a;
}

static void draw_text_cb(void *ud, const char *s, int len, float x, float base) {
    Pen *p = ud;
    int i = 0;
    while (i < len) {
        uint32_t cp = utf8_next(s, len, &i);
        int w, h, xo, yo;
        unsigned char *bm = stbtt_GetCodepointBitmapSubpixel(p->font, p->scale, p->scale,
                                                             x - floorf(x), 0, (int)cp, &w, &h, &xo, &yo);
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++) {
                float a = bm[yy * w + xx] / 255.0f;
                float shadow[3] = {0, 0, 0};
                blend((int)x + xo + xx + 2, (int)base + yo + yy + 2, shadow, a * 0.5f);
            }
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++)
                blend((int)x + xo + xx, (int)base + yo + yy, p->rgb, bm[yy * w + xx] / 255.0f);
        stbtt_FreeBitmap(bm, NULL);
        x += tt_adv(p, cp);
    }
}

// Bilinear sample from the atlas, like the Vita's linear filter.
static void sample(float u, float v, float out[4]) {
    u = u - 0.5f; v = v - 0.5f;
    int x0 = (int)floorf(u), y0 = (int)floorf(v);
    float fx = u - x0, fy = v - y0;
    for (int c = 0; c < 4; c++) out[c] = 0;
    for (int k = 0; k < 4; k++) {
        int x = x0 + (k & 1), y = y0 + (k >> 1);
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= PROMPT_ATLAS_W) x = PROMPT_ATLAS_W - 1;
        if (y >= PROMPT_ATLAS_H) y = PROMPT_ATLAS_H - 1;
        float wgt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
        const uint8_t *px = &g_atlas[(y * PROMPT_ATLAS_W + x) * 4];
        float a = px[3] / 255.0f;
        for (int c = 0; c < 3; c++) out[c] += wgt * px[c] / 255.0f * a;   // premultiplied
        out[3] += wgt * a;
    }
}

static void draw_glyph_cb(void *ud, int g, float x, float yc, float h) {
    (void)ud;
    PromptRect r = prompts_rect((PromptGlyph)g);
    float w = h * prompts_aspect((PromptGlyph)g), s = r.h / h;
    for (int yy = (int)floorf(yc - h / 2); yy < (int)ceilf(yc + h / 2); yy++)
        for (int xx = (int)floorf(x); xx < (int)ceilf(x + w); xx++) {
            float u = r.x + (xx + 0.5f - x) * s, v = r.y + (yy + 0.5f - (yc - h / 2)) * s;
            if (u < r.x || v < r.y || u > r.x + r.w || v > r.y + r.h) continue;
            float px[4];
            sample(u, v, px);
            if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
            for (int c = 0; c < 3; c++) g_img[yy][xx][c] = g_img[yy][xx][c] * (1 - px[3]) + px[c];
        }
}

static void text_at(stbtt_fontinfo *font, const char *key, float px, float x, float y,
                    float wrap, int align, float box, float r, float g, float b) {
    const char *s = strtab_get(key);
    if (!s) { printf("missing key %s\n", key); g_fail++; return; }
    Pen pen = {font, px, stbtt_ScaleForPixelHeight(font, px), {r, g, b}};
    int asc, desc, gap;
    stbtt_GetFontVMetrics(font, &asc, &desc, &gap);
    float cap = asc * pen.scale * 0.72f;
    TextMetrics m = {tt_adv, &pen, px * 1.25f, cap * 1.6f, cap * 0.5f, px * 0.1f};
    TextRun runs[128];
    TextBlock tb = text_layout(s, &m, wrap, align, box, runs, 128);
    text_draw_runs(s, &m, runs, tb.nruns, x, y, draw_text_cb, draw_glyph_cb, &pen);
}

static void fill(int x0, int y0, int x1, int y1, float r, float g, float b, float a) {
    float rgb[3] = {r, g, b};
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) blend(x, y, rgb, a);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: texttest <data dir> <font.ttf> <out.png> [atlas.png]\n");
        return 2;
    }
    checks("/tmp");

    char path[512];
    snprintf(path, sizeof path, "%s/strings_en.txt", argv[1]);
    int n_game = strtab_load(path);
    snprintf(path, sizeof path, "%s/strings_vita_en.txt", argv[1]);
    int n_vita = strtab_load(path);
    printf("strings: %d game, %d vita\n", n_game, n_vita);
    CHECK(n_game == 0 && n_vita > 10);

    FILE *f = fopen(argv[2], "rb");
    if (!f) { fprintf(stderr, "can't open font %s\n", argv[2]); return 2; }
    fseek(f, 0, SEEK_END);
    long fl = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *ttf = malloc(fl);
    if (fread(ttf, 1, fl, f) != (size_t)fl) return 2;
    fclose(f);
    stbtt_fontinfo font;
    stbtt_InitFont(&font, ttf, 0);

    prompts_init();
    prompts_build_atlas(g_atlas);

    // Plain backdrop: a dusk gradient. No game art in the preview.
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float t = (float)y / H;
            g_img[y][x][0] = 0.30f + 0.45f * t;
            g_img[y][x][1] = 0.42f + 0.18f * t;
            g_img[y][x][2] = 0.68f - 0.30f * t;
        }

    text_at(&font, "vita.title.press_start", 44, 0, 150, 0, ALIGN_CENTER, W, 1, 1, 1);
    text_at(&font, "vita.title.version", 16, 0, 16, 0, ALIGN_RIGHT, W - 16, 1, 1, 1);

    static const char *controls[] = {
        "vita.controls.move", "vita.controls.light", "vita.controls.heavy", "vita.controls.jump",
        "vita.controls.block", "vita.controls.magic", "vita.controls.projectile",
        "vita.controls.pause", "vita.controls.stats",
    };
    fill(60, 230, 470, 470, 0, 0, 0, 0.35f);
    text_at(&font, "vita.controls.title", 24, 80, 236, 0, ALIGN_LEFT, 0, 1, 0.9f, 0.4f);
    for (int i = 0; i < 9; i++)
        text_at(&font, controls[i], 20, 80, 270 + i * 22, 0, ALIGN_LEFT, 0, 1, 1, 1);

    fill(500, 230, 900, 330, 0, 0, 0, 0.35f);
    text_at(&font, "vita.online.unavailable", 20, 520, 245, 360, ALIGN_LEFT, 0, 1, 1, 1);
    text_at(&font, "vita.pause.quit_hint", 20, 520, 340, 360, ALIGN_LEFT, 0, 1, 1, 1);

    fill(0, H - 48, W, H, 0, 0, 0, 0.55f);
    text_at(&font, "vita.menu.legend", 22, 0, H - 44, 0, ALIGN_CENTER, W, 1, 1, 1);

    static uint8_t out[H][W][3];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int c = 0; c < 3; c++) {
                float v = g_img[y][x][c];
                out[y][x][c] = (uint8_t)(255 * (v < 0 ? 0 : v > 1 ? 1 : v) + 0.5f);
            }
    stbi_write_png(argv[3], W, H, 3, out, W * 3);

    if (argc > 4) stbi_write_png(argv[4], PROMPT_ATLAS_W, PROMPT_ATLAS_H, 4, g_atlas, PROMPT_ATLAS_W * 4);

    strtab_free();
    printf(g_fail ? "%d check(s) failed\n" : "all checks passed\n", g_fail);
    return g_fail != 0;
}
