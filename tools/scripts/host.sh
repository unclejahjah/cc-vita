#!/bin/bash
S=/mnt/c/Users/Jah/AppData/Local/Temp/claude/C--Users-Jah--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
P="/mnt/c/Users/Jah/Documents/VITA Casle Crashers/cc-vita"
SWF="/mnt/c/Users/Jah/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files/game/PLAYER.swf"
cd "$P"
LIB="src/swf.c src/render.c src/level.c third_party/libtess2/Source/*.c"
gcc -O2 -Wall -Ithird_party/libtess2/Include -o $S/hosttest tools/hosttest.c $LIB -lm || exit 1
gcc -O2 -Isrc -Ithird_party/libtess2/Include -o $S/maskscan $S/maskscan.c $LIB -lm || exit 1
$S/maskscan "$SWF"
which convert python3; python3 -c "import PIL; print('PIL ok')"
