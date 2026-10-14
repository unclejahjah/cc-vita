#!/bin/bash
# grid.sh file(relative to game dir) cid [cell]
S=/mnt/c/Users/Alex_/AppData/Local/Temp/claude/C--Users-Alex--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
P="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/cc-vita"
G="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
cd "$P"
gcc -O2 -Wall -Ithird_party/libtess2/Include -o $S/hosttest tools/hosttest.c src/swf.c src/render.c src/level.c src/player.c src/enemy.c third_party/libtess2/Source/*.c -lm || exit 1
mkdir -p $S/grid
$S/hosttest --sprite "$G/$1" $2 $S/grid/g$2 ${3:-80} || exit 1
python3 -I -c "from PIL import Image; Image.open('$S/grid/g${2}_000.ppm').save('$S/grid/g$2.png')"
