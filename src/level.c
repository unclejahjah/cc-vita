#include "level.h"

#include <stdlib.h>
#include <string.h>

// From f_UpdateCamera: each container sits at (HALF_GAME_WIDTH, GAME_HEIGHT) and is
// scaled around that point; its child scrolls by camera * factor.
static const struct {
    const char *container, *inner;
    float factor, scale_mul, scale_add;
} LAYER_RULES[] = {
    {"bg3_c", "bg3", 0.1f, 0.1f, 90},
    {"bg2_c", "bg2", 0.25f, 0.25f, 75},
    {"bg1_c", "bg1", 0.5f, 0.5f, 50},
    {"game", "game", 1.0f, 1.0f, 0},
    {"bg0_c", "bg0", 1.5f, 1.5f, -50},
};
#define NRULES (int)(sizeof LAYER_RULES / sizeof LAYER_RULES[0])

static const Frame *first_frame(const Movie *m, const Place *p) {
    if (!p || m->chars[p->cid].type != CHAR_SPRITE) return NULL;
    const Sprite *s = m->chars[p->cid].ptr;
    return s->nframes ? &s->frames[0] : NULL;
}

static int ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcmp(s + a - b, suffix);
}

// Collision segment clips mark their ends with children named bsp1 and bsp2.
static int segment_ends(const Movie *m, int cid, const Place **b1, const Place **b2) {
    if (m->chars[cid].type != CHAR_SPRITE) return 0;
    const Sprite *s = m->chars[cid].ptr;
    if (!s->nframes) return 0;
    *b1 = frame_find_name(m, &s->frames[0], "bsp1");
    *b2 = frame_find_name(m, &s->frames[0], "bsp2");
    return *b1 && *b2;
}

// The game loads collision from a frame of the play layer that holds the segment
// clips (the level script steps to it once); take the frame with the most.
static void load_segments(Level *lv, const Place *inner) {
    Movie *m = lv->m;
    if (m->chars[inner->cid].type != CHAR_SPRITE) return;
    const Sprite *s = m->chars[inner->cid].ptr;
    int best = -1, best_count = 0;
    for (int f = 0; f < s->nframes; f++) {
        int n = 0;
        const Place *b1, *b2;
        for (int i = 0; i < s->frames[f].count; i++)
            n += segment_ends(m, s->frames[f].places[i].cid, &b1, &b2);
        if (n > best_count) { best = f; best_count = n; }
    }
    if (best < 0) return;
    lv->segs = malloc(sizeof(Segment) * best_count);
    const Frame *fr = &s->frames[best];
    for (int i = 0; i < fr->count; i++) {
        const Place *p = &fr->places[i], *b1, *b2;
        if (!segment_ends(m, p->cid, &b1, &b2)) continue;
        const Mat *t = &p->m;
        Segment *sg = &lv->segs[lv->nsegs++];
        sg->x0 = (t->a * b1->m.tx + t->c * b1->m.ty + t->tx) / 20;
        sg->y0 = (t->b * b1->m.tx + t->d * b1->m.ty + t->ty) / 20;
        sg->x1 = (t->a * b2->m.tx + t->c * b2->m.ty + t->tx) / 20;
        sg->y1 = (t->b * b2->m.tx + t->d * b2->m.ty + t->ty) / 20;
        sg->type = ((const Sprite *)m->chars[p->cid].ptr)->n_type;
    }
}

// The game's scripts hide its editor markers (spawn points, camera targets, edges)
// at runtime; without the scripts, drop them from the play layer here.
static int is_marker(const Movie *m, const Place *p) {
    const Character *c = &m->chars[p->cid];
    const Place *b1, *b2;
    if (c->export && !strcmp(m->names[c->export], "invisObject")) return 1;
    if (segment_ends(m, p->cid, &b1, &b2)) return 1;   // collision edges are never drawn
    // cutscene controllers: their "guide" child is the camera target, drawn as a film camera icon
    if (c->type == CHAR_SPRITE && ((const Sprite *)c->ptr)->nframes &&
        frame_find_name(m, &((const Sprite *)c->ptr)->frames[0], "guide"))
        return 1;
    if (!p->name) return 0;
    const char *n = m->names[p->name];
    char base[64];
    size_t len = strlen(n);
    if (len >= sizeof base) return 0;
    memcpy(base, n, len + 1);
    while (len && base[len - 1] >= '0' && base[len - 1] <= '9') base[--len] = 0;   // waypoint12 -> waypoint
    // door1, door2...: spawn/exit portals the game keeps hidden (real doors have other names, e.g. doorjam)
    if (!strcmp(base, "door") || !strcmp(base, "portal")) return 1;
    static const char *prefixes[] = {"edge", "abs_", "waypoint", "p1_start", "p2_start", "p3_start", "p4_start"};
    static const char *suffixes[] = {"cam", "_cp", "_pt", "spawn"};
    for (size_t i = 0; i < sizeof prefixes / sizeof *prefixes; i++)
        if (!strncmp(base, prefixes[i], strlen(prefixes[i]))) return 1;
    for (size_t i = 0; i < sizeof suffixes / sizeof *suffixes; i++)
        if (ends_with(base, suffixes[i])) return 1;
    return 0;
}

static void hide_markers(Level *lv, const Place *inner) {
    Movie *m = lv->m;
    if (m->chars[inner->cid].type != CHAR_SPRITE) return;
    Sprite *s = m->chars[inner->cid].ptr;
    for (int f = 0; f < s->nframes; f++) {
        Frame *fr = &s->frames[f];
        int k = 0;
        for (int i = 0; i < fr->count; i++) {
            if (is_marker(m, &fr->places[i])) { if (f == 0) lv->hidden++; continue; }
            fr->places[k++] = fr->places[i];
        }
        fr->count = k;
    }
}

static int cmp_sorted(const void *a, const void *b) {
    float ya = ((const SortedPlace *)a)->y, yb = ((const SortedPlace *)b)->y;
    return ya < yb ? -1 : ya > yb;
}

// The game moves registered objects (and all characters) to depths around 1,000,000
// ordered by y; everything else keeps its timeline depth, which is far below.
static void split_depths(Level *lv, const Place *inner) {
    Movie *m = lv->m;
    if (m->chars[inner->cid].type != CHAR_SPRITE) return;
    const Sprite *s = m->chars[inner->cid].ptr;
    if (!s->nframes) return;
    const Frame *fr = &s->frames[0];
    lv->game_static.places = malloc(sizeof(Place) * (fr->count ? fr->count : 1));
    lv->sorted = malloc(sizeof(SortedPlace) * (fr->count ? fr->count : 1));
    for (int i = 0; i < fr->count; i++) {
        const Place *p = &fr->places[i];
        const Character *c = &m->chars[p->cid];
        const Sprite *cs = c->type == CHAR_SPRITE ? c->ptr : NULL;
        if (!p->clip && cs && cs->depth_sort != DEPTH_NONE) {
            SortedPlace *sp = &lv->sorted[lv->nsorted++];
            sp->place = p;
            sp->y = cs->depth_sort == DEPTH_FIXED ? cs->depth_y : p->m.ty / 20.0f;
        } else {
            lv->game_static.places[lv->game_static.count++] = *p;
        }
    }
    qsort(lv->sorted, lv->nsorted, sizeof(SortedPlace), cmp_sorted);
}

Level *level_load(const char *path, char *err, int errlen) {
    Movie *m = swf_load(path, err, errlen);
    if (!m) return NULL;
    Level *lv = calloc(1, sizeof(Level));
    lv->m = m;
    if (!m->root.nframes) return lv;
    const Frame *root = &m->root.frames[0];

    // keep the file's depth order; skip layers we don't drive (sky, fog, HUD, ...)
    for (int i = 0; i < root->count && lv->nlayers < MAX_LAYERS; i++) {
        const Place *p = &root->places[i];
        if (!p->name) continue;
        for (int r = 0; r < NRULES; r++) {
            if (strcmp(m->names[p->name], LAYER_RULES[r].container)) continue;
            const Frame *cf = first_frame(m, p);
            const Place *inner = cf ? frame_find_name(m, cf, LAYER_RULES[r].inner) : NULL;
            if (!inner && cf && cf->count) inner = &cf->places[0];
            if (!inner) break;
            Layer *L = &lv->layer[lv->nlayers++];
            L->container = p;
            L->inner = inner;
            L->factor = LAYER_RULES[r].factor;
            L->scale_mul = LAYER_RULES[r].scale_mul;
            L->scale_add = LAYER_RULES[r].scale_add;
            L->is_game = !strcmp(LAYER_RULES[r].container, "game");
            break;
        }
    }

    for (int i = 0; i < lv->nlayers; i++) {
        if (!lv->layer[i].is_game) continue;
        const Place *inner = lv->layer[i].inner;
        const Frame *gf = first_frame(m, inner);
        // Spawn at player 1's start marker, else a door, else where the editor left the camera.
        lv->spawn_x = -inner->m.tx / 20.0f;
        lv->spawn_y = -inner->m.ty / 20.0f;
        static const char *spawns[] = {"p1_start", "door0", "door1", "door"};
        for (int s = 0; gf && s < 4; s++) {
            const Place *p = frame_find_name(m, gf, spawns[s]);
            if (p) { lv->spawn_x = p->m.tx / 20.0f; lv->spawn_y = p->m.ty / 20.0f; break; }
        }
        const Place *el = gf ? frame_find_name(m, gf, "edge_left") : NULL;
        const Place *er = gf ? frame_find_name(m, gf, "edge_right") : NULL;
        if (el && er && er->m.tx > el->m.tx) {
            lv->edge_left = el->m.tx / 20.0f;
            lv->edge_right = er->m.tx / 20.0f;
        }
        load_segments(lv, inner);
        hide_markers(lv, inner);
        split_depths(lv, inner);
    }
    return lv;
}

float level_hit_test(const Level *lv, float x0, float y0, float x1, float y1, int *hit) {
    float dx = x1 - x0, dy = y1 - y0, best = 2;
    for (int i = 0; i < lv->nsegs; i++) {
        const Segment *s = &lv->segs[i];
        float ex = s->x1 - s->x0, ey = s->y1 - s->y0;
        float den = dx * ey - dy * ex;
        if (den == 0) continue;   // parallel
        float wx = s->x0 - x0, wy = s->y0 - y0;
        float t = (wx * ey - wy * ex) / den;   // along the move
        float u = (wx * dy - wy * dx) / den;   // along the segment
        if (t > 0 && t <= 1 && u >= 0 && u <= 1 && t < best) {
            best = t;
            if (hit) *hit = i;
        }
    }
    return best <= 1 ? best : 0;   // like the game, 0 (starting on the line) counts as no hit
}

static int is_stair_edge(int type) { return type == 100 || type == 101; }

void level_move(const Level *lv, Mover *mv, float dx, float dy) {
    int hit = -1;
    if (dx) {
        float t = level_hit_test(lv, mv->x, mv->y, mv->x + dx, mv->y, &hit);
        if (mv->ground == 100) {
            // f_StairsMCH: crossing any edge leaves the stairs, just past the edge
            if (t) {
                mv->x += (t + 0.001f) * dx;
                mv->ground = 0;
            } else {
                // Follow the slope of the stair line under the feet (probe 800 px down):
                // the game then moves the feet by slope * step with f_MoveCharV, so walking
                // sideways climbs the stairs. (body_y only follows along while airborne.)
                int below = -1;
                float climb = 0;
                if (level_hit_test(lv, mv->x, mv->y, mv->x, mv->y + 800, &below) && lv->segs[below].type == 100) {
                    const Segment *s = &lv->segs[below];
                    float ex = s->x1 - s->x0;
                    if (ex) climb = (s->y1 - s->y0) / ex * dx;
                }
                mv->x += dx;
                if (climb) {
                    if (mv->body_y < 0) mv->body_y -= climb;
                    level_move(lv, mv, 0, climb);
                }
            }
        } else if (t && is_stair_edge(lv->segs[hit].type)) {
            // f_PlainMCH: a stair edge switches to stairs mode, just past the edge
            mv->x += (t + 0.0001f) * dx;
            mv->ground = 100;
        } else {
            mv->x += t ? (t - 0.001f > 0 ? (t - 0.001f) * dx : 0) : dx;
        }
    }
    if (dy) {
        float t = level_hit_test(lv, mv->x, mv->y, mv->x, mv->y + dy, &hit);
        if (mv->ground == 100) {
            // f_InsideMCV: inside the stairs, crossing any edge leaves them, just past the edge
            if (t) {
                mv->y += (t + 0.001f) * dy;
                mv->ground = 0;
            } else {
                mv->y += dy;
            }
        } else if (t && is_stair_edge(lv->segs[hit].type)) {
            // f_PlainMCV: a stair edge switches to stairs mode, just past the edge
            mv->y += (t + 0.0001f) * dy;
            mv->ground = 100;
        } else {
            mv->y += t ? (t - 0.001f > 0 ? (t - 0.001f) * dy : 0) : dy;
        }
    }
}

float level_clamp_camera_x(const Level *lv, float cam_x) {
    if (lv->edge_right <= lv->edge_left) return cam_x;
    float lo = lv->edge_left + GAME_W / 2, hi = lv->edge_right - GAME_W / 2;
    if (lo > hi) return (lv->edge_left + lv->edge_right) / 2;
    return cam_x < lo ? lo : cam_x > hi ? hi : cam_x;
}

void level_free(Level *lv) {
    if (!lv) return;
    swf_free(lv->m);
    free(lv->segs);
    free(lv->game_static.places);
    free(lv->sorted);
    free(lv);
}

static Mat layer_matrix(const Layer *L, Mat screen, float cam_x, float cam_y, float zoom) {
    float camera_x = -cam_x;
    float comp_y = -cam_y - 100.0f;   // compensated_camera_y
    float s = (zoom * L->scale_mul + L->scale_add) / 100.0f;
    Mat container = {s, 0, 0, s, GAME_W / 2 * 20, GAME_H * 20};
    Mat inner = L->inner->m;
    inner.tx = camera_x * L->factor * 20;
    inner.ty = comp_y * L->factor * 20;
    return mat_mul(screen, mat_mul(container, inner));
}

Mat level_game_matrix(const Level *lv, Mat screen, float cam_x, float cam_y, float zoom) {
    for (int i = 0; i < lv->nlayers; i++)
        if (lv->layer[i].is_game) return layer_matrix(&lv->layer[i], screen, cam_x, cam_y, zoom);
    return screen;
}

// Draws a character frame. Like the game, only the "body" child moves by body_y
// (the shadow stays on the ground), and body_frame can pin the body clip's frame.
static void draw_actor_parts(const Actor *A, Mat m, CXform cx, DrawList *out);

static void draw_actor(const Actor *A, Mat m, CXform cx, DrawList *out) {
    out->nparts = A->nparts;
    for (int i = 0; i < A->nparts; i++) {
        out->part_count[i] = A->part_count[i];
        out->part_frame[i] = A->part_frame[i];
    }
    // in tenths, so the recolour cache sees a handful of colour transforms rather than one per frame
    if (A->fade > 0) cx.mul[3] *= 1 - (int)((A->fade < 1 ? A->fade : 1) * 10 + 0.5f) / 10.0f;
    draw_actor_parts(A, m, cx, out);
    out->nparts = 0;
}

static void draw_actor_parts(const Actor *A, Mat m, CXform cx, DrawList *out) {
    float sx = A->scale, sy = A->scale < 0 ? -A->scale : A->scale;
    Mat am = mat_mul(m, (Mat){sx, 0, 0, sy, A->x * 20, A->y * 20});
    const Movie *mv = A->m;
    for (int i = 0; i < A->frame->count; i++) {
        const Place *p = &A->frame->places[i];
        if (p->clip) {   // characters don't mask their top level; keep it simple and correct for the rest
            Frame rest = {A->frame->count - i, (Place *)p};
            render_display_list(A->m, &rest, am, cx, A->tick, out);
            return;
        }
        int is_body = p->name && !strcmp(mv->names[p->name], "body");
        Mat pm = p->m;
        if (is_body) pm.ty += A->body_y * 20 / sy;
        if (is_body && A->body_frame > 0 && mv->chars[p->cid].type == CHAR_SPRITE) {
            const Sprite *s = mv->chars[p->cid].ptr;
            int f = A->body_frame < s->nframes ? A->body_frame : s->nframes - 1;
            render_display_list(A->m, &s->frames[f], mat_mul(am, pm), cx_mul(cx, p->cx), A->tick, out);
        } else {
            Place moved = *p;
            moved.m = pm;
            Frame one = {1, &moved};
            render_display_list(A->m, &one, am, cx, A->tick, out);
        }
    }
}

void level_render(const Level *lv, Mat screen, float cam_x, float cam_y, float zoom, int tick,
                  const Actor *actors, int nactors, DrawList *out) {
    for (int i = 0; i < lv->nlayers; i++) {
        const Layer *L = &lv->layer[i];
        Mat m = layer_matrix(L, screen, cam_x, cam_y, zoom);
        CXform cx = cx_mul(L->container->cx, L->inner->cx);
        if (!L->is_game || !lv->game_static.places) {
            render_char(lv->m, L->inner->cid, m, cx, tick, out);
            continue;
        }
        // play layer: static art, then sorted objects and actors merged by y
        // (ties go to the object, as characters standing level with a prop draw in front of it)
        render_display_list(lv->m, &lv->game_static, m, cx, tick, out);
        const Actor *order[16];
        int n = 0;
        for (int a = 0; a < nactors && n < 16; a++) {
            int k = n++;
            while (k > 0 && order[k - 1]->y > actors[a].y) { order[k] = order[k - 1]; k--; }
            order[k] = &actors[a];
        }
        int si = 0, ai = 0;
        while (si < lv->nsorted || ai < n) {
            if (ai >= n || (si < lv->nsorted && lv->sorted[si].y <= order[ai]->y)) {
                Frame one = {1, (Place *)lv->sorted[si++].place};
                render_display_list(lv->m, &one, m, cx, tick, out);
            } else {
                draw_actor(order[ai++], m, cx, out);
            }
        }
    }
}
