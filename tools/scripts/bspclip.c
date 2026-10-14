// bspclip LEVEL.swf cid : where the collision clip sits, its frames, and its segments
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swf.h"
int main(int argc, char **argv) {
    char err[256];
    Movie *m = swf_load(argv[1], err, sizeof err);
    int id = atoi(argv[2]);
    for (int q = -1; q < 65536; q++) {
        Sprite *ps = q < 0 ? &m->root : m->chars[q].type == CHAR_SPRITE ? m->chars[q].ptr : NULL;
        if (!ps) continue;
        for (int f = 0; f < ps->nframes && f < 1; f++)
            for (int k = 0; k < ps->frames[f].count; k++)
                if (ps->frames[f].places[k].cid == id)
                    printf("clip %d placed in sprite %d as '%s' at %.0f,%.0f\n", id, q,
                           m->names[ps->frames[f].places[k].name], ps->frames[f].places[k].m.tx / 20,
                           ps->frames[f].places[k].m.ty / 20);
    }
    Sprite *s = m->chars[id].ptr;
    printf("%d frames, labels:", s->nframes);
    for (int i = 0; i < s->nlabels; i++) printf(" %s@%d", s->labels[i].name, s->labels[i].frame);
    printf("\n");
    for (int f = 0; f < s->nframes; f++) printf("frame %d: %d places\n", f, s->frames[f].count);
    Frame *fr = &s->frames[s->nframes > 1 ? 1 : 0];
    for (int i = 0; i < fr->count && i < 25; i++) {
        Place *p = &fr->places[i];
        // segment endpoints: bsp1 at (0,0), bsp2 at (64px,0) in the segment clip
        float x0 = p->m.tx / 20, y0 = p->m.ty / 20;
        float x1 = (p->m.a * 1280 + p->m.tx) / 20, y1 = (p->m.b * 1280 + p->m.ty) / 20;
        printf("  cid %d '%s' (%.0f,%.0f)-(%.0f,%.0f)\n", p->cid, m->names[p->name], x0, y0, x1, y1);
    }
    return 0;
}
