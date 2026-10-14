// cidinfo file.swf cid : frame-0 contents of a sprite
#include <stdio.h>
#include <stdlib.h>
#include "swf.h"
int main(int argc, char **argv) {
    char err[256];
    Movie *m = swf_load(argv[1], err, sizeof err);
    int cid = atoi(argv[2]);
    Sprite *s = m->chars[cid].ptr;
    printf("cid %d: %d frames, frame0 %d places\n", cid, s->nframes, s->frames[0].count);
    for (int i = 0; i < s->frames[0].count; i++) {
        Place *p = &s->frames[0].places[i];
        printf("  depth %d cid %d type %d name '%s' clip %d\n", p->depth, p->cid, m->chars[p->cid].type, m->names[p->name], p->clip);
    }
    return 0;
}
