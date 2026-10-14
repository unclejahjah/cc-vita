// memsize: heap and vertex totals per SWF after parse + tessellate_all
#include <malloc.h>
#include <stdio.h>
#include "swf.h"
#include "render.h"

int main(int argc, char **argv) {
    size_t tot_heap = 0, tot_v = 0;
    for (int i = 1; i < argc; i++) {
        struct mallinfo2 a = mallinfo2();
        char err[256];
        Movie *m = swf_load(argv[i], err, sizeof err);
        if (!m) { printf("%s: %s\n", argv[i], err); continue; }
        render_tessellate_all(m);
        struct mallinfo2 b = mallinfo2();
        size_t v = 0;
        for (int c = 0; c < 65536; c++)
            if (m->chars[c].type == CHAR_SHAPE) v += ((Shape *)m->chars[c].ptr)->mesh.nverts;
        size_t heap = b.uordblks - a.uordblks;
        tot_heap += heap; tot_v += v;
        printf("%-60s heap %7.1f MB  verts %8zu (%5.1f MB gpu)\n", argv[i], heap / 1048576.0, v, v * 16 / 1048576.0);
    }
    printf("total heap %.1f MB, gpu %.1f MB\n", tot_heap / 1048576.0, tot_v * 16 / 1048576.0);
    return 0;
}
