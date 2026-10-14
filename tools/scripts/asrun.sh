#!/bin/bash
# asrun.sh START.swf [frames] [snap_every]
S=/mnt/c/Users/Alex_/AppData/Local/Temp/claude/C--Users-Alex--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
P="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/cc-vita"
G="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
cd "$P"
gcc -O2 -g -Wall -Wno-unused-function -Ithird_party/libtess2/Include -o $S/hosttest tools/hosttest.c src/swf.c src/render.c src/level.c src/player.c src/enemy.c src/as_core.c src/as_mc.c src/as_lib.c src/as_render.c src/cc_host.c src/hostfont.c third_party/libtess2/Source/*.c -lm /lib/x86_64-linux-gnu/libz.so.1 2>&1 | grep -E "error|warning" | head -40
[ -x $S/hosttest ] || exit 1
mkdir -p $S/as && rm -f $S/as/*
$S/hosttest --as "$G" ${1:-MAIN.swf} $S/as/a ${2:-60} ${3:-30}
python3 -I -c "
import glob
from PIL import Image
for f in sorted(glob.glob('$S/as/a_*.ppm')): Image.open(f).save(f[:-4] + '.png')
"
