#!/bin/bash
S=/mnt/c/Users/Jah/AppData/Local/Temp/claude/C--Users-Jah--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
G="/mnt/c/Users/Jah/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
mkdir -p $S/segall
for f in "$G"/levels/LEVEL*.swf; do
  n=$(basename "$f" .swf)
  echo "$n: $($S/hosttest --level "$f" "$G/game/PLAYER.swf" $S/segall/$n 1 2>&1 | grep -E 'collision segments' )"
done | sort -V
