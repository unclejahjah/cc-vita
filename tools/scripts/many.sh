#!/bin/bash
# Renders frame 0 of each listed level, prints stats, and builds a contact sheet.
S=/mnt/c/Users/Jah/AppData/Local/Temp/claude/C--Users-Jah--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
G="/mnt/c/Users/Jah/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
mkdir -p $S/many && rm -f $S/many/*
for n in "$@"; do
  echo "== LEVEL$n: $($S/hosttest --level "$G/levels/LEVEL$n.swf" "$G/game/PLAYER.swf" $S/many/L$n 1 | tr '\n' ' ' | sed 's/  layer [^ ]* factor [0-9.]*//g')"
done
python3 -I - "$S/many" "$@" <<'EOF'
import sys, os
from PIL import Image, ImageDraw
d, nums = sys.argv[1], sys.argv[2:]
ims = [(n, Image.open(os.path.join(d, 'L%s_000.ppm' % n)).resize((480, 272))) for n in nums if os.path.exists(os.path.join(d, 'L%s_000.ppm' % n))]
cols = 3
sheet = Image.new('RGB', (480 * cols, 272 * ((len(ims) + cols - 1) // cols)))
for i, (n, im) in enumerate(ims):
    ImageDraw.Draw(im).text((6, 4), 'LEVEL' + n, fill=(255, 0, 0))
    sheet.paste(im, (480 * (i % cols), 272 * (i // cols)))
sheet.save(os.path.join(d, 'sheet.png'))
EOF
