#!/bin/bash
# ptest.sh LEVELNUM [fight]
S=/mnt/c/Users/Alex_/AppData/Local/Temp/claude/C--Users-Alex--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
P="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/cc-vita"
G="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
cd "$P"
gcc -O2 -Wall -Ithird_party/libtess2/Include -o $S/hosttest tools/hosttest.c src/swf.c src/render.c src/level.c src/player.c src/enemy.c third_party/libtess2/Source/*.c -lm || exit 1
mkdir -p $S/pt && rm -f $S/pt/*
$S/hosttest --player "$G/levels/LEVEL${1:-4}.swf" "$G/game/PLAYER.swf" $S/pt/p $2 || exit 1
python3 -I - "$S/pt" <<'EOF'
import sys, glob, os
from PIL import Image
d = sys.argv[1]
fs = sorted(glob.glob(os.path.join(d, 'p_*.ppm')))
ims = [Image.open(f).resize((480, 272)) for f in fs]
cols = 4
sheet = Image.new('RGB', (480 * cols, 272 * ((len(ims) + cols - 1) // cols)))
for i, im in enumerate(ims): sheet.paste(im, (480 * (i % cols), 272 * (i // cols)))
sheet.save(os.path.join(d, 'sheet.png'))
EOF
