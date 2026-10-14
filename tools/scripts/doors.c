// doors LEVEL.swf... : lists door*/portal* places in the play layer with their sprite's labels
#include <stdio.h>
#include <string.h>
#include "level.h"
int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        char err[256];
        Movie *m = swf_load(argv[a], err, sizeof err);
        if (!m || !m->root.nframes) continue;
        const Place *g = frame_find_name(m, &m->root.frames[0], "game");
        if (!g || m->chars[g->cid].type != CHAR_SPRITE) continue;
        Sprite *gs = m->chars[g->cid].ptr;
        const Place *gi = frame_find_name(m, &gs->frames[0], "game");
        if (!gi || m->chars[gi->cid].type != CHAR_SPRITE) continue;
        Sprite *s = m->chars[gi->cid].ptr;
        const char *base = strrchr(argv[a], '/') + 1;
        for (int i = 0; i < s->frames[0].count; i++) {
            const Place *p = &s->frames[0].places[i];
            const char *n = m->names[p->name];
            if (strncmp(n, "door", 4) && strncmp(n, "portal", 6)) continue;
            printf("%-12s %-10s cid %-4d", base, n, p->cid);
            if (m->chars[p->cid].type == CHAR_SPRITE) {
                Sprite *ds = m->chars[p->cid].ptr;
                printf(" frames %-3d labels:", ds->nframes);
                for (int l = 0; l < ds->nlabels && l < 8; l++) printf(" %s", ds->labels[l].name);
            }
            printf("\n");
        }
        swf_free(m);
    }
    return 0;
}
