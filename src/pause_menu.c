// The pause menu castle.exe draws (its PauseMenu, 2012 build) on a stage of 848x480: the
// lines 30 apart around 60 below the middle, the title at 70, the button prompts at 425,
// the chosen line pulsing yellow and flashing when picked, each page fading in and out in
// a fifth of a second. Online-only lines (Download Content, Leaderboards, Players) are left
// out. The words are the game's English ones (castle.exe's table isn't readable on the
// Vita); the art is PAUSE.swf's.
#include "pause_menu.h"
#include "as_render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STAGE_W 848.0f
#define STAGE_H 480.0f

enum { A_NONE, A_BACK, A_RESUME, A_HELP, A_INFO, A_MAP, A_EXIT, A_ACHIEVEMENTS, A_NO, A_YES };
typedef struct { const char *text; int act; } Line;   // act A_NONE: not selectable

static int page_lines(const PauseMenu *pm, const char **title, const Line **lines) {
    static const Line main_lines[] = {{"Resume Game", A_RESUME}, {"Help & Options", A_HELP}, {"Information", A_INFO},
                                      {"Exit To Map", A_MAP},    {"Exit to Main Menu", A_EXIT}};
    static const Line info_lines[] = {{"Achievements", A_ACHIEVEMENTS}};
    // page 3 of the original: the question, No, Yes, and the warning
    static const Line map_lines[] = {{"Are You Sure?", 0}, {"", 0}, {"No", A_NO}, {"Yes", A_YES},
                                     {"", 0}, {"You will lose any", 0}, {"unsaved progress", 0}};
    static const Line exit_lines[] = {{"Are you sure you wish", 0}, {"to exit this game?", 0}, {"No", A_NO},
                                      {"Yes", A_YES}, {"", 0}, {"You will lose any", 0}, {"unsaved progress", 0}};
    *title = NULL;
    *lines = NULL;
    if (pm->page == PG_MAIN) { *title = "Paused"; *lines = main_lines; return 5; }
    if (pm->page == PG_INFO) { *title = "Information"; *lines = info_lines; return 1; }
    if (pm->page == PG_CONFIRM) { *lines = pm->confirm ? exit_lines : map_lines; return 7; }
    return 0;
}

static void go(PauseMenu *pm, int page, int sel) {
    pm->page = page;
    pm->sel = sel;
    pm->fade = 1;
    pm->alpha = 0;
    pm->act = A_NONE;
    pm->flash = 0;
}

void pause_menu_open(PauseMenu *pm) {
    float clock = pm->clock;
    memset(pm, 0, sizeof *pm);
    pm->clock = clock;   // the helmets drift on from where they were
    go(pm, PG_MAIN, 0);
}

// what the chosen line (or Back) does once the page has faded out
static int run(PauseMenu *pm, int act) {
    switch (act) {
    case A_BACK:
        if (pm->page == PG_INFO) { go(pm, PG_MAIN, 2); return PM_OPEN; }
        if (pm->page == PG_CONFIRM) { go(pm, PG_MAIN, pm->confirm ? 4 : 3); return PM_OPEN; }
        return PM_RESUME;   // Back on the menu itself
    case A_RESUME: return PM_RESUME;
    case A_HELP: pm->back_page = PG_MAIN; go(pm, PG_CONTROLS, 0); return PM_OPEN;
    case A_INFO: go(pm, PG_INFO, 0); return PM_OPEN;
    case A_ACHIEVEMENTS: pm->back_page = PG_INFO; go(pm, PG_ACHIEVEMENTS, 0); return PM_OPEN;
    case A_MAP: pm->confirm = 0; go(pm, PG_CONFIRM, 2); return PM_OPEN;
    case A_EXIT: pm->confirm = 1; go(pm, PG_CONFIRM, 2); return PM_OPEN;
    case A_NO: go(pm, PG_MAIN, pm->confirm ? 4 : 3); return PM_OPEN;
    case A_YES: return pm->confirm ? PM_TO_TITLE : PM_TO_MAP;
    }
    return PM_OPEN;
}

int pause_menu_update(PauseMenu *pm, unsigned pressed, float dt, int *sound) {
    *sound = -1;
    pm->clock += dt * 30;
    pm->time += dt;
    if (pm->page == PG_CONTROLS || pm->page == PG_ACHIEVEMENTS) {   // the books: any button goes back
        if (pressed & (PM_ACCEPT | PM_BACK)) {
            *sound = 1;
            go(pm, pm->back_page, pm->back_page == PG_MAIN ? 1 : 0);
        }
        return PM_OPEN;
    }
    const char *title;
    const Line *lines;
    int n = page_lines(pm, &title, &lines);
    if (pm->flash > 0) {   // the chosen line flashes, then the page fades out
        pm->flash_on = !pm->flash_on;
        pm->flash -= dt;
        if (pm->flash <= 0) { pm->flash = 0; pm->fade = -1; }
    } else if (pm->fade == 0) {
        if (pressed & PM_ACCEPT) {
            *sound = 1;
            pm->act = lines[pm->sel].act;
            pm->flash = 0.2f;
            pm->flash_on = 1;
        } else if (pressed & PM_BACK) {
            *sound = 1;
            pm->act = A_BACK;
            pm->fade = -1;
        } else if (pressed & (PM_UP | PM_DOWN)) {
            int step = pressed & PM_UP ? n - 1 : 1;
            do pm->sel = (pm->sel + step) % n; while (!lines[pm->sel].act);
            *sound = 0;
        }
    }
    if (pm->fade) {
        pm->alpha += pm->fade * dt * 5;
        if (pm->alpha >= 1) { pm->alpha = 1; pm->fade = 0; }
        if (pm->alpha < 0) { pm->alpha = 0; return run(pm, pm->act); }
    }
    return PM_OPEN;
}

// a line of the game font: a black copy one pixel left and down, then the line in rgb
static void text(DrawList *out, float k, const char *s, float x, float y, float scale, int align, uint32_t rgb,
                 float alpha) {
    Mat m = {k, 0, 0, k, 0, 0};
    CXform shadow = {{0, 0, 0, alpha}, {0, 0, 0, 0}};
    CXform cx = {{(rgb >> 16 & 255) / 255.0f, (rgb >> 8 & 255) / 255.0f, (rgb & 255) / 255.0f, alpha}, {0, 0, 0, 0}};
    as_render_host_text(s, roundf(x) - 1, roundf(y) + 1, scale, scale, align, m, shadow, out);
    as_render_host_text(s, roundf(x), roundf(y), scale, scale, align, m, cx, out);
}

static int find_export(Movie *mv, const char *name) {
    for (int c = 0; c < 65536; c++)
        if (mv->chars[c].export && !strcmp(mv->names[mv->chars[c].export], name)) return c;
    return -1;
}

// width and height (pixels) of a clip's first frame
static void clip_size(Movie *mv, int cid, float size[2]) {
    DrawList dl = {0};
    render_begin(&dl);
    render_char(mv, cid, MAT_IDENTITY, CX_IDENTITY, 0, &dl);
    float b[4] = {1e30f, 1e30f, -1e30f, -1e30f};
    for (int k = 0; k < dl.n; k++) {
        const float *mb = dl.items[k].mesh->bounds;
        Mat mm = dl.items[k].m;
        for (int q = 0; q < 4; q++) {
            float x = mb[q & 1 ? 2 : 0], y = mb[q & 2 ? 3 : 1];
            float px = mm.a * x + mm.c * y + mm.tx, py = mm.b * x + mm.d * y + mm.ty;
            if (px < b[0]) b[0] = px;
            if (px > b[2]) b[2] = px;
            if (py < b[1]) b[1] = py;
            if (py > b[3]) b[3] = py;
        }
    }
    size[0] = b[2] > b[0] ? (b[2] - b[0]) / 20 : 0;
    size[1] = b[3] > b[1] ? (b[3] - b[1]) / 20 : 0;
    free(dl.items);
    free(dl.cv);
    free(dl.cmd);
}

// a button prompt (the game's art; PlayStation once cc_buttons_playstation has run on the
// file) with its word to the right, at a seventh of the stage in from the left or the middle
static void prompt(DrawList *out, Movie *mv, float k, int right, const char *label, float alpha) {
    static Movie *seen;
    static int cid[2];
    static float size[2][2];
    if (seen != mv) {
        seen = mv;
        for (int i = 0; i < 2; i++) {
            cid[i] = find_export(mv, i ? "Bbutton" : "Abutton");
            size[i][0] = size[i][1] = 0;
            if (cid[i] >= 0) clip_size(mv, cid[i], size[i]);
        }
    }
    float x0 = STAGE_W * 0.1428571492433548f + (right ? STAGE_W * 0.5f : 0), y0 = 425;
    if (cid[right] >= 0) {
        Mat m = {k / 20, 0, 0, k / 20, x0 * k, y0 * k};
        CXform cx = {{1, 1, 1, alpha}, {0, 0, 0, 0}};
        render_char(mv, cid[right], m, cx, 0, out);
    }
    float th = as_render_host_line_height() * 0.7f + 1;
    text(out, k, label, size[right][0] * 0.5f + x0 + 5, size[right][1] * 0.5f + y0 - th * 0.5f, 0.7f, 1, 0xaaaaaa,
         alpha);
}

void pause_menu_build(const PauseMenu *pm, Movie *mv, float k, DrawList *out) {
    // PAUSE.swf's frame 2 (in a level): the dark veil and the drifting helmets and hearts
    if (mv->root.nframes > 1) {
        const Frame *f = &mv->root.frames[1];
        Mat screen = {k / 20, 0, 0, k / 20, 0, 0};
        for (int i = 0; i < f->count; i++)
            render_char(mv, f->places[i].cid, mat_mul(screen, f->places[i].m), f->places[i].cx, (int)pm->clock, out);
    }
    const char *title;
    const Line *lines;
    int n = page_lines(pm, &title, &lines);
    if (!n) return;
    float a = pm->alpha;
    if (title) text(out, k, title, STAGE_W / 2, 70, 1.25f, 2, 0xdddddd, a);
    int rows = n + (title != NULL);
    for (int i = 0; i < n; i++) {
        float y = 30.0f * (i + (title != NULL)) + (STAGE_H / 2 - rows * 15.0f) + 60;
        uint32_t c = lines[i].act ? 0x777777 : 0xaaaaaa;
        if (i == pm->sel) {
            if (pm->flash > 0) {
                c = pm->flash_on ? 0xffff00 : 0x777777;
            } else {
                int v = (int)(sinf(pm->time * 10) * 34 + 221) & 255;
                c = (uint32_t)v << 16 | (uint32_t)v << 8;
            }
        }
        text(out, k, lines[i].text, STAGE_W / 2, y, 0.8421052694320679f, 2, c, a);
    }
    prompt(out, mv, k, 0, "Select", a);
    prompt(out, mv, k, 1, "Back", a);
}
