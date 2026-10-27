# fit.py: register template boxes against PLAYER.swf part sprites.
# For a box and a sprite (at a frame), find scale k (template px per flash px) and the sprite
# origin (ox, oy) in template px so the sprite's silhouette outline lies on the box's guide lines.
import os, sys, math, json
import numpy as np
from PIL import Image
from scipy import ndimage
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../prototypes/swfproto'))
import swf, cairo
from render import Renderer

TPL = os.environ.get('PBP_TEMPLATE', 'template.png')   # the blank template (pbp_template.pak's PNG)
tpl = np.array(Image.open(TPL))
m = swf.Movie(sys.argv[1]); R = Renderer(m)

def boxes():
    lab, n = ndimage.label(tpl[..., 3] == 0)
    out = []
    for i, s in enumerate(ndimage.find_objects(lab)):
        h = s[0].stop - s[0].start; w = s[1].stop - s[1].start
        if h * w > 3000: out.append((s[1].start, s[0].start, w, h))
    out.sort(key=lambda b: (b[1], b[0]))
    return out
BOX = boxes()

def guide(bi, pad=0):
    x, y, w, h = BOX[bi]
    a = tpl[y:y+h, x:x+w]
    g = a[..., 3] > 100
    return g

def render_mask(cid, frame, k, size=600, flip=False):
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size); ctx = cairo.Context(surf)
    sp = m.sprites.get(cid)
    o = size / 2
    if sp:
        R.draw_display_list(ctx, sp.frames[min(frame, len(sp.frames)-1)], ((-k if flip else k)/20, 0, 0, k/20, o, o), swf.IDENTITY_CX, 0)
    else:
        R.draw_char(ctx, cid, (k/20, 0, 0, k/20, o, o), swf.IDENTITY_CX, 0)
    buf = np.frombuffer(surf.get_data(), np.uint8).reshape(size, size, 4)
    return buf[..., 3] > 128, o

def fit(bi, cid, frame, ks, flip=False):
    g = guide(bi)
    dt = ndimage.distance_transform_edt(~g)
    gy, gx = np.nonzero(g)
    best = None
    for k in ks:
        sil, o = render_mask(cid, frame, k, flip=flip)
        if not sil.any(): continue
        edge = sil & ~ndimage.binary_erosion(sil, iterations=2)
        ey, ex = np.nonzero(edge)
        # align bounding-box centres first, then search a window
        cy0 = (gy.min() + gy.max()) / 2 - (ey.min() + ey.max()) / 2
        cx0 = (gx.min() + gx.max()) / 2 - (ex.min() + ex.max()) / 2
        H, W = g.shape
        sdt = ndimage.distance_transform_edt(~edge)
        for dy in range(-40, 41, 1):
            for dx in range(-40, 41, 1):
                ty = (ey + cy0 + dy).astype(int); tx = (ex + cx0 + dx).astype(int)
                ok = (ty >= 0) & (ty < H) & (tx >= 0) & (tx < W)
                if ok.mean() < 0.9: continue
                d1 = np.minimum(dt[ty[ok], tx[ok]], 20).mean()
                # reverse: guide pixels to the outline
                sy = (gy - cy0 - dy).astype(int); sx = (gx - cx0 - dx).astype(int)
                ok2 = (sy >= 0) & (sy < sdt.shape[0]) & (sx >= 0) & (sx < sdt.shape[1])
                d2 = np.minimum(sdt[sy[ok2], sx[ok2]], 20).mean() if ok2.any() else 20
                sc = d1 + d2
                if best is None or sc < best[0]:
                    best = (sc, k, o + cx0 + dx, o + cy0 + dy)
    return best

if __name__ == '__main__':
    bi, cid, frame = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    ks = [float(x) for x in sys.argv[5].split(',')]
    print(BOX[bi], fit(bi, cid, frame, ks))
