// bspfind LEVEL.swf : sprites containing places named bsp*, their contents, and who places them
#include <stdio.h>
#include <string.h>
#include "render.h"

static Movie *m;

static void describe(int cid, int depth) {
    Character *c = &m->chars[cid];
    if (c->type == CHAR_SHAPE) {
        Shape *s = c->ptr;
        printf("%*sshape %d: %d fills %d lines\n", depth * 2, "", cid, s->nfills, s->nlines);
        for (int i = 0; i < s->nfills && i < 3; i++) {
            ShapeFill *f = &s->fills[i];
            printf("%*s  fill %d: %d loops %d pts colour %02x%02x%02x%02x first pt %.0f,%.0f\n", depth * 2, "", i,
                   f->nloops, f->npoints, f->style.color[0], f->style.color[1], f->style.color[2], f->style.color[3],
                   f->pts[0] / 20, f->pts[1] / 20);
        }
    } else if (c->type == CHAR_SPRITE) {
        Sprite *s = c->ptr;
        printf("%*ssprite %d: %d frames, frame0 %d places\n", depth * 2, "", cid, s->nframes, s->nframes ? s->frames[0].count : 0);
        if (depth < 3 && s->nframes)
            for (int i = 0; i < s->frames[0].count; i++) {
                Place *p = &s->frames[0].places[i];
                printf("%*s- '%s' at %.0f,%.0f scale %.2f,%.2f\n", depth * 2 + 2, "", m->names[p->name], p->m.tx / 20,
                       p->m.ty / 20, p->m.a, p->m.d);
                describe(p->cid, depth + 2);
            }
    }
}

int main(int argc, char **argv) {
    char err[256];
    m = swf_load(argv[1], err, sizeof err);
    int shown = 0;
    for (int id = 0; id < 65536 && shown < 4; id++) {
        if (m->chars[id].type != CHAR_SPRITE) continue;
        Sprite *s = m->chars[id].ptr;
        if (!s->nframes) continue;
        for (int i = 0; i < s->frames[0].count; i++) {
            Place *p = &s->frames[0].places[i];
            if (strncmp(m->names[p->name], "bsp", 3)) continue;
            printf("== sprite %d has '%s' (cid %d)\n", id, m->names[p->name], p->cid);
            describe(id, 0);
            // who places sprite id? (any frame of any sprite, and the root)
            int count = 0;
            for (int q = -1; q < 65536; q++) {
                Sprite *ps = q < 0 ? &m->root : m->chars[q].type == CHAR_SPRITE ? m->chars[q].ptr : NULL;
                if (!ps) continue;
                for (int f = 0; f < ps->nframes; f++)
                    for (int k = 0; k < ps->frames[f].count; k++)
                        if (ps->frames[f].places[k].cid == id) {
                            if (count++ < 3)
                                printf("   placed in sprite %d frame %d as '%s'\n", q, f,
                                       m->names[ps->frames[f].places[k].name]);
                        }
            }
            printf("   placements (all frames): %d\n", count);
            shown++;
            break;
        }
    }
    int total = 0;
    for (int id = 0; id < 65536; id++) {
        if (m->chars[id].type != CHAR_SPRITE) continue;
        Sprite *s = m->chars[id].ptr;
        if (s->nframes && frame_find_name(m, &s->frames[0], "bsp1")) total++;
    }
    printf("sprites with a bsp1 child: %d\n", total);
    return 0;
}
