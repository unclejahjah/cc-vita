# testpaint2.py PLAYER.swf TEMPLATE custom_char.c OUT: paints each part's own art (frame 0, made
# purple) into its box with the table's transform, so a correct mapping gives a purple knight
import os
import sys, re, math
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../prototypes/swfproto'))
import swf, cairo, colorsys
import numpy as np
from PIL import Image
from render import Renderer
m = swf.Movie(sys.argv[1]); R = Renderer(m)
src = open(sys.argv[3]).read()
boxes = [tuple(map(int, b)) for b in re.findall(r'\{(\d+), (\d+), (\d+), (\d+)\}', src.split('static const int16_t BOX')[1].split('};')[0])]
parts = re.findall(r'\{(G_\w+), (\d+), (\d+), PART_\w+, ([-\d.]+)f?, ([-\d.]+), (\d), ([-\d.]+)f?, ([-\d.]+)f?\}', src)
# group order, as the engine finds it: sprites whose first-frame script calls the selector
data = open(sys.argv[1], 'rb').read()
sel = {'G_HELMET': b'f_Helmet\0', 'G_BELT': b'f_Belt\0', 'G_WEAPON': b'f_Weapon\0', 'G_SHIELD': b'f_Shield\0'}
sys.path.insert(0, sys.argv[0].rsplit('/', 1)[0])
from sprites import load
d, spr, ex, sh = load(sys.argv[1])
groups = {g: [] for g in sel}
for cid in sorted(spr):
    nf, q, l = spr[cid]
    for code, p, ln in swf.read_tags(d, q + 4, q + l):
        if code == 1: break
        if code == 12:
            for g, s in sel.items():
                if s in d[p:p + ln] and cid not in groups[g]: groups[g].append(cid)
t = Image.open(sys.argv[2]).convert('RGBA')
W = 2048
surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, W, W); ctx = cairo.Context(surf)
for g, nth, box, k, rot, flip, ox, oy in parts:
    cid = groups[g][int(nth)]; bx, by, bw, bh = boxes[int(box)]
    k, rot, f = float(k), math.radians(float(rot)), -1 if flip == '1' else 1
    c, s = math.cos(rot), math.sin(rot)
    ctx.save(); ctx.rectangle(bx, by, bw, bh); ctx.clip()
    mt = (c * f * k / 20, s * f * k / 20, -s * k / 20, c * k / 20, bx + float(ox), by + float(oy))
    R.draw_display_list(ctx, m.sprites[cid].frames[0], mt, swf.IDENTITY_CX, 0)
    ctx.restore()
a = np.frombuffer(surf.get_data(), np.uint8).reshape(W, W, 4).copy()
bgra = a[..., [2, 1, 0, 3]].astype(float)
al = bgra[..., 3:4] / 255.0
rgb = np.where(al > 0, bgra[..., :3] / np.maximum(al, 1e-6), 0)
lum = rgb.mean(2, keepdims=True) / 255.0
purple = np.array([150, 60, 210]) * (0.35 + 0.9 * lum)
img = np.concatenate([purple.clip(0, 255), bgra[..., 3:4]], 2).astype(np.uint8)
paint = Image.fromarray(img, 'RGBA')
out = t.copy(); out.alpha_composite(paint); out.save(sys.argv[4])
