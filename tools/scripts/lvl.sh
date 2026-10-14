#!/bin/bash
# usage: lvl.sh LEVELNUM [frames] [start_x start_y step]
S=/mnt/c/Users/Alex_/AppData/Local/Temp/claude/C--Users-Alex--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
P="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/cc-vita"
G="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
cd "$P"
gcc -O2 -g -Wall -Wno-unused-function -Ithird_party/libtess2/Include -o $S/hosttest tools/hosttest.c src/swf.c src/render.c src/level.c src/player.c src/enemy.c src/as_core.c src/as_mc.c src/as_lib.c src/as_render.c src/cc_host.c third_party/libtess2/Source/*.c -lm /lib/x86_64-linux-gnu/libz.so.1 2>&1 | grep -E "error" | head
mkdir -p $S/lv && rm -f $S/lv/L$1_*
$S/hosttest --level "$G/levels/LEVEL$1.swf" "$G/game/PLAYER.swf" $S/lv/L$1 ${2:-3} $3 $4 $5 $6 || exit 1
python3 -I -c "
import glob
from PIL import Image
for f in sorted(glob.glob('$S/lv/L$1_*.ppm')): Image.open(f).save(f[:-4] + '.png')
"
