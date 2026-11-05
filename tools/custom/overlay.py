# overlay.py PLAYER.swf OUT.png "bi:cid:frame:k:ox:oy" ...  guide (black) + sprite (red tint) per box, side by side
import os
import sys
sys.path.insert(0, sys.argv[0].rsplit('/', 1)[0])
sys.argv, args = sys.argv[:2], sys.argv[2:]
import fit
from PIL import Image
import numpy as np
out = args[0]; tiles = []
for a in args[1:]:
    bi, cid, fr, k, ox, oy = a.split(':'); bi, cid, fr = int(bi), int(cid), int(fr); k, ox, oy = float(k), float(ox), float(oy)
    g = fit.guide(bi); H, W = g.shape
    sil, o = fit.render_mask(cid, fr, k)
    img = np.full((H, W, 3), 255, np.uint8)
    ys, xs = np.nonzero(sil)
    ty = (ys - o + oy).astype(int); tx = (xs - o + ox).astype(int)
    ok = (ty >= 0) & (ty < H) & (tx >= 0) & (tx < W)
    img[ty[ok], tx[ok]] = (255, 170, 170)
    img[g] = (0, 0, 0)
    oyi, oxi = int(oy), int(ox)
    img[max(0,oyi-4):oyi+5, oxi] = (0, 0, 255); img[oyi, max(0,oxi-4):oxi+5] = (0, 0, 255)
    tiles.append(Image.fromarray(img))
Wt = sum(t.width for t in tiles) + 4 * len(tiles); Ht = max(t.height for t in tiles)
sheet = Image.new('RGB', (Wt, Ht), (90, 90, 90)); x = 0
for t in tiles: sheet.paste(t, (x, 0)); x += t.width + 4
sheet.save(out)
