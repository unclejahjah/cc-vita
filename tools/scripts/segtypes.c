// segtypes LEVEL.swf... : collision segment clip types per level (sprites with bsp1+bsp2 children)
#include <stdio.h>
#include <string.h>
#include "swf.h"

int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        char err[256];
        Movie *m = swf_load(argv[a], err, sizeof err);
        printf("== %s\n", strrchr(argv[a], '/') + 1);
        for (int id = 0; id < 65536; id++) {
            if (m->chars[id].type != CHAR_SPRITE) continue;
            Sprite *s = m->chars[id].ptr;
            if (!s->nframes) continue;
            Frame *f = &s->frames[0];
            const Place *b1 = frame_find_name(m, f, "bsp1"), *b2 = frame_find_name(m, f, "bsp2");
            if (!b1 || !b2) continue;
            printf("seg clip %d: bsp1 %.0f,%.0f bsp2 %.0f,%.0f export '%s' |", id, b1->m.tx / 20, b1->m.ty / 20,
                   b2->m.tx / 20, b2->m.ty / 20, m->names[m->chars[id].export]);
            for (int i = 0; i < f->count; i++) {
                const Place *p = &f->places[i];
                if (p == b1 || p == b2) continue;
                const Character *c = &m->chars[p->cid];
                if (c->type == CHAR_SHAPE) {
                    Shape *sh = c->ptr;
                    if (sh->nfills)
                        printf(" shape%d fill %02x%02x%02x pts %d", p->cid, sh->fills[0].style.color[0],
                               sh->fills[0].style.color[1], sh->fills[0].style.color[2], sh->fills[0].npoints);
                    else printf(" shape%d (lines only)", p->cid);
                } else {
                    printf(" clip%d'%s'@%.0f,%.0f s%.1f,%.1f", p->cid, m->names[p->name], p->m.tx / 20, p->m.ty / 20, p->m.a, p->m.d);
                }
            }
            printf("\n");
        }
        swf_free(m);
    }
    return 0;
}
