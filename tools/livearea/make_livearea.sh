#!/bin/bash
# LiveArea art from the user's own game files: renders MENU.swf's title and LEGAL.swf's bird with
# hosttest at twice the Vita size, then make_livearea.py lays them out.
#   tools/livearea/make_livearea.sh GAME_DIR FONTS_DIR OUT_DIR
# GAME_DIR has game/MENU.swf and game/LEGAL.swf; FONTS_DIR has synjUnicode_20.{fnt,dds}.
# Then build with cmake -DLIVEAREA_DIR=OUT_DIR. The PNGs are made from game art: never commit them.
set -e
GAME=$(realpath "$1"); FONTS=$(realpath "$2"); OUT=$(realpath -m "$3")
REPO=$(cd "$(dirname "$0")/../.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
cd "$REPO"
gcc -O2 -w -Ithird_party/libtess2/Include -o "$W/hosttest" tools/hosttest.c src/swf.c src/render.c src/level.c \
  src/player.c src/enemy.c src/as_core.c src/as_mc.c src/as_lib.c src/as_render.c src/cc_host.c src/cc_native.c \
  src/cc_buttons.c src/hostfont.c src/strtab.c src/pause_menu.c third_party/libtess2/Source/*.c -lm -lz
cd "$W"
export CC_SCALE=2 CC_PS_BUTTONS=grey   # grey buttons also put the Vita Edition badge on the logo
run() { timeout 300 ./hosttest --as "$GAME" "$@" 121 120 > /dev/null 2>&1; }
# MENU.swf: sprite 626 = news bar, shape 354 = logo (+ badge), shape 600 = backdrop
AS_HIDE_CHAR=626 AS_HIDE_SHAPES=354 run MENU.swf scene
AS_HIDE_CLIPS=1 AS_HIDE_SHAPES=600 CC_BG=0,0,0 run MENU.swf logok
AS_HIDE_CLIPS=1 AS_HIDE_SHAPES=600 CC_BG=255,255,255 run MENU.swf logow
# LEGAL.swf frame 150: the bird sitting still; sprite 15 = white backdrop, "legal" = the small print
AS_HIDE_CHAR=15 AS_HIDE=legal CC_BG=0,0,0 run LEGAL.swf behk
AS_HIDE_CHAR=15 AS_HIDE=legal CC_BG=255,255,255 run LEGAL.swf behw
python3 -I - "$W" <<'PY'
import sys
import numpy as np
from PIL import Image
d = sys.argv[1]
Image.open(f'{d}/scene_001.ppm').save(f'{d}/scene.png')
for name in ('logo', 'behemoth'):
    p = 'logo' if name == 'logo' else 'beh'
    k = np.asarray(Image.open(f'{d}/{p}k_001.ppm')).astype(float)   # over black
    w = np.asarray(Image.open(f'{d}/{p}w_001.ppm')).astype(float)   # over white
    a = np.clip(1 - (w - k).mean(axis=2) / 255, 0, 1)
    rgb = np.clip(np.where(a[..., None] > 0.01, k / np.maximum(a[..., None], 1e-3), 0), 0, 255)
    Image.fromarray(np.dstack([rgb, a * 255]).astype(np.uint8), 'RGBA').save(f'{d}/{name}.png')
PY
V=$(sed -n 's/.*VITA_VERSION *"\([0-9]*\)\.\([0-9]*\)".*/\1 \2/p' "$REPO/CMakeLists.txt")
python3 -I "$REPO/tools/livearea/make_livearea.py" "$W" "$FONTS" "$OUT" "$(echo $V | awk '{printf "V%d.%s", $1, $2}')"
