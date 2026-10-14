// partcounts PLAYER.swf : clips stopped on frame 0 with many frames (character part candidates), by frame count
#include <stdio.h>
#include "swf.h"
int main(int argc, char **argv) {
    char err[256];
    Movie *m = swf_load(argv[1], err, sizeof err);
    int count[2000] = {0}, example[2000] = {0};
    for (int id = 0; id < 65536; id++) {
        if (m->chars[id].type != CHAR_SPRITE) continue;
        const Sprite *s = m->chars[id].ptr;
        if (s->first_stop != 0 || s->nframes < 8 || s->nframes >= 2000) continue;
        if (!count[s->nframes]++) example[s->nframes] = id;
    }
    for (int n = 0; n < 2000; n++)
        if (count[n]) printf("%4d frames: %3d clips (e.g. cid %d)\n", n, count[n], example[n]);
    return 0;
}
