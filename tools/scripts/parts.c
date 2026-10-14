// parts PLAYER.swf label : the named clips inside the body for one animation, with frame counts and labels
#include <stdio.h>
#include "swf.h"
static Movie *m;
static void walk(int cid, int depth, const char *path) {
    if (m->chars[cid].type != CHAR_SPRITE || depth > 6) return;
    const Sprite *s = m->chars[cid].ptr;
    if (!s->nframes) return;
    for (int i = 0; i < s->frames[0].count; i++) {
        const Place *p = &s->frames[0].places[i];
        if (m->chars[p->cid].type != CHAR_SPRITE) continue;
        const Sprite *c = m->chars[p->cid].ptr;
        char sub[512];
        snprintf(sub, sizeof sub, "%s/%s", path, p->name ? m->names[p->name] : "_");
        if (p->name || c->nframes > 3) {
            printf("%-40s cid %-4d %3d frames stop@%-3d", sub, p->cid, c->nframes, c->first_stop);
            for (int k = 0; k < c->nlabels && k < 8; k++) printf(" %s@%d", c->labels[k].name, c->labels[k].frame);
            printf("\n");
        }
        walk(p->cid, depth + 1, sub);
    }
}
int main(int argc, char **argv) {
    char err[256];
    m = swf_load(argv[1], err, sizeof err);
    int f = sprite_find_label(&m->root, argv[2]);
    const Frame *fr = &m->root.frames[f];
    for (int i = 0; i < fr->count; i++)
        if (fr->places[i].name && m->chars[fr->places[i].cid].type == CHAR_SPRITE) {
            printf("[%s]\n", m->names[fr->places[i].name]);
            walk(fr->places[i].cid, 0, m->names[fr->places[i].name]);
        }
    return 0;
}
