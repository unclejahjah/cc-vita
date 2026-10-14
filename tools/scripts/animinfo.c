// animinfo PLAYER.swf : every root label, the clips in that frame and how long they run
#include <stdio.h>
#include "swf.h"
int main(int argc, char **argv) {
    char err[256];
    Movie *m = swf_load(argv[1], err, sizeof err);
    for (int l = 0; l < m->root.nlabels; l++) {
        const Frame *f = &m->root.frames[m->root.labels[l].frame];
        printf("%3d %-22s", l + 1, m->root.labels[l].name);
        for (int i = 0; i < f->count; i++) {
            const Place *p = &f->places[i];
            if (m->chars[p->cid].type != CHAR_SPRITE) continue;
            const Sprite *s = m->chars[p->cid].ptr;
            printf(" [%s:%d fr stop@%d", m->names[p->name], s->nframes, s->first_stop);
            for (int k = 0; k < s->nlabels && k < 6; k++) printf(" %s@%d", s->labels[k].name, s->labels[k].frame);
            printf("]");
        }
        printf("\n");
    }
    return 0;
}
