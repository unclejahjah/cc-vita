// whatat LEVEL.swf PLAYER.swf x y : which play-layer place draws at screen pixel x,y (frame 0)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "level.h"

static int covers(const VertexBuf *vb, float x, float y) {
    for (int i = 0; i + 2 < vb->n; i += 3) {
        const Vertex *a = &vb->v[i], *b = &vb->v[i + 1], *c = &vb->v[i + 2];
        float w0 = (b->x - x) * (c->y - y) - (b->y - y) * (c->x - x);
        float w1 = (c->x - x) * (a->y - y) - (c->y - y) * (a->x - x);
        float w2 = (a->x - x) * (b->y - y) - (a->y - y) * (b->x - x);
        if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    char err[256];
    Level *lv = level_load(argv[1], err, sizeof err);
    float px = atof(argv[3]), py = atof(argv[4]);
    float k = 960 / GAME_W;
    Mat screen = {k / 20, 0, 0, k / 20, 0, 0};
    float cx = level_clamp_camera_x(lv, lv->spawn_x), cy = lv->spawn_y;
    VertexBuf vb = {0};
    for (int i = 0; i < lv->nlayers; i++) {
        Layer *L = &lv->layer[i];
        float s = 1;
        Mat container = {s, 0, 0, s, GAME_W / 2 * 20, GAME_H * 20};
        Mat inner = L->inner->m;
        inner.tx = -cx * 20 * L->factor;
        inner.ty = (-cy - 100) * 20 * L->factor;
        printf("[layer %s]\n", lv->m->names[L->container->name]);
        Mat m = mat_mul(screen, mat_mul(container, inner));
        Sprite *sp = lv->m->chars[L->inner->cid].ptr;
        Frame *f = &sp->frames[0];
        for (int j = 0; j < f->count; j++) {
            Place *p = &f->places[j];
            render_begin(&vb);
            Frame one = {1, p};
            render_display_list(lv->m, &one, m, CX_IDENTITY, 0, &vb);
            (void)covers;
            int hit = 0;
            for (int v = 0; v < vb.n && !hit; v++)
                hit = vb.v[v].x > px - 30 && vb.v[v].x < px + 30 && vb.v[v].y > py - 30 && vb.v[v].y < py + 30;
            if (hit && vb.n / 3 < 3000) {
                float x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
                for (int v = 0; v < vb.n; v++) {
                    if (vb.v[v].x < x0) x0 = vb.v[v].x;
                    if (vb.v[v].x > x1) x1 = vb.v[v].x;
                    if (vb.v[v].y < y0) y0 = vb.v[v].y;
                    if (vb.v[v].y > y1) y1 = vb.v[v].y;
                }
                Sprite *cs = lv->m->chars[p->cid].type == CHAR_SPRITE ? lv->m->chars[p->cid].ptr : NULL;
                printf("depth %d cid %d name '%s' export '%s' type %d clip %d tris %d box %.0f,%.0f-%.0f,%.0f frames %d labels %s\n",
                       p->depth, p->cid, lv->m->names[p->name], lv->m->names[lv->m->chars[p->cid].export],
                       lv->m->chars[p->cid].type, p->clip, vb.n / 3, x0, y0, x1, y1, cs ? cs->nframes : 0,
                       cs && cs->nlabels ? cs->labels[0].name : "-");
            }
        }
    }
    return 0;
}
