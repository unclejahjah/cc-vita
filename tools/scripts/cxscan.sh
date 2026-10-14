#!/bin/bash
S=/mnt/c/Users/Jah/AppData/Local/Temp/claude/C--Users-Jah--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
G="/mnt/c/Users/Jah/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
cd "/mnt/c/Users/Jah/Documents/VITA Casle Crashers/cc-vita"
gcc -O2 -Wall -Ithird_party/libtess2/Include -o $S/hosttest tools/hosttest.c src/swf.c src/render.c src/level.c third_party/libtess2/Source/*.c -lm || exit 1
mkdir -p $S/cx
for n in 3 4 7 20 33 54 59; do
  echo "LEVEL$n: $($S/hosttest --level "$G/levels/LEVEL$n.swf" "$G/game/PLAYER.swf" $S/cx/L$n 3 | grep '^frame' | tail -1)"
done
