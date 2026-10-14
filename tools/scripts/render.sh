#!/bin/bash
S=/mnt/c/Users/Alex_/AppData/Local/Temp/claude/C--Users-Alex--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
SWF="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files/game/PLAYER.swf"
mkdir -p $S/out && cd $S/out && rm -f *.ppm *.png
for l in dig beefy_arrow burried swim_walk; do
  $S/hosttest "$SWF" $l $l 12 | tail -2
done
python3 -I - "$S/out" <<'EOF'
import sys, glob, os
from PIL import Image
d = sys.argv[1]
for lab in ["dig", "beefy_arrow", "burried", "swim_walk"]:
    fr = [Image.open(f) for f in sorted(glob.glob(os.path.join(d, lab + "_*.ppm")))][::3]
    sheet = Image.new("RGB", (480 * len(fr), 480))
    for i, im in enumerate(fr): sheet.paste(im, (480 * i, 0))
    sheet.save(os.path.join(d, lab + ".png"))
EOF
ls $S/out/*.png
