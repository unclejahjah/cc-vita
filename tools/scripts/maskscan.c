// Lists which root labels use masks (renders first 30 ticks of each).
#include <stdio.h>
#include "render.h"
int main(int argc, char **argv) {
    char err[256];
    Movie *m = swf_load(argv[1], err, sizeof err);
    if (!m) { fprintf(stderr, "%s\n", err); return 1; }
    VertexBuf vb = {0};
    int total = 0;
    for (int l = 0; l < m->root.nlabels; l++) {
        int maxm = 0;
        for (int t = 0; t < 30; t++) {
            render_begin(&vb);
            render_display_list(m, &m->root.frames[m->root.labels[l].frame], MAT_IDENTITY, CX_IDENTITY, t, &vb);
            if (render_stats.masks > maxm) maxm = render_stats.masks;
            if (vb.level != 0) printf("LEVEL LEAK %s\n", m->root.labels[l].name);
        }
        if (maxm) { printf("%3d %-24s masks %d\n", l + 1, m->root.labels[l].name, maxm); total++; }
    }
    printf("%d of %d labels use masks\n", total, m->root.nlabels);
    return 0;
}
