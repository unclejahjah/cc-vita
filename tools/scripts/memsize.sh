#!/bin/bash
S=/mnt/c/Users/Alex_/AppData/Local/Temp/claude/C--Users-Alex--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
P="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/cc-vita"
G="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
cd "$P"
gcc -O2 -Isrc -Ithird_party/libtess2/Include -o $S/memsize $S/memsize.c src/swf.c src/render.c third_party/libtess2/Source/*.c -lm || exit 1
cd "$G"
$S/memsize game/MAIN.swf game/SOUNDS.swf game/LOBBY.swf game/MAP.swf game/PLAYER.swf game/ANIMALS.swf game/HORSE.swf game/CHICKEN.swf game/FX.swf game/SKY1.swf levels/LEVEL13.swf levels/LEVEL1.swf levels/LEVEL2.swf
ls -S game | head -20
