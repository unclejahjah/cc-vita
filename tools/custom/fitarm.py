# fitarm.py PLAYER.swf "bi:cid" ...: filled silhouette IoU over scale, rotation, mirror; FFT for the shift
import os
import sys, math
sys.path.insert(0, sys.argv[0].rsplit('/', 1)[0])
pairs = sys.argv[2:]; sys.argv = sys.argv[:2]
import fit, swf, cairo
import numpy as np
from scipy import ndimage

def render(cid, k, rot, flip, size):
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size); ctx = cairo.Context(surf)
    c, s = math.cos(rot), math.sin(rot); f = -1 if flip else 1
    # local -> render: R * diag(f*k/20, k/20) then + centre
    a, b, cc, d = c * f * k / 20, s * f * k / 20, -s * k / 20, c * k / 20
    sp = fit.m.sprites[cid]
    fit.R.draw_display_list(ctx, sp.frames[0], (a, b, cc, d, size / 2, size / 2), swf.IDENTITY_CX, 0)
    buf = np.frombuffer(surf.get_data(), np.uint8).reshape(size, size, 4)
    return buf[..., 3] > 128

UPRIGHT = {138, 429, 574, 640, 686, 76, 334, 350, 458, 536, 946, 1202, 1278, 1430, 1562, 1615, 243, 102, 484}
for p in pairs:
    bi, cid = map(int, p.split(':'))
    g = fit.guide(bi)
    g = ndimage.binary_fill_holes(ndimage.binary_closing(g, iterations=5))
    upright = cid in UPRIGHT
    H, W = g.shape; N = 512
    G = np.zeros((N, N)); G[:H, :W] = g
    FG = np.fft.rfft2(G)
    best = None
    for k in (np.arange(1.7, 2.41, 0.02) if upright else np.arange(1.9, 2.11, 0.05)):
        for flip in (False, True):
            for deg in ((0,) if upright else range(0, 360, 6)):
                sil = render(cid, k, math.radians(deg), flip, 256)
                Sm = np.zeros((N, N)); Sm[:256, :256] = sil
                corr = np.fft.irfft2(FG * np.conj(np.fft.rfft2(Sm)), s=(N, N))
                iy, ix = np.unravel_index(np.argmax(corr), corr.shape)
                ov = corr[iy, ix]; iou = ov / (g.sum() + sil.sum() - ov)
                if best is None or iou > best[0]:
                    # sprite origin (128,128) in render -> box coords: + shift
                    oy = 128 + (iy if iy < N // 2 else iy - N); ox = 128 + (ix if ix < N // 2 else ix - N)
                    best = (iou, k, deg, flip, ox, oy)
    # refine rotation +-5 by 1
    iou, k, deg, flip, ox, oy = best
    for k2 in (k - 0.02, k, k + 0.02):
        for d2 in ((0,) if upright else range(deg - 5, deg + 6)):
            sil = render(cid, k2, math.radians(d2), flip, 256)
            Sm = np.zeros((N, N)); Sm[:256, :256] = sil
            corr = np.fft.irfft2(FG * np.conj(np.fft.rfft2(Sm)), s=(N, N))
            iy, ix = np.unravel_index(np.argmax(corr), corr.shape)
            ov = corr[iy, ix]; i2 = ov / (g.sum() + sil.sum() - ov)
            if i2 > best[0]:
                best = (i2, k2, d2, flip, 128 + (ix if ix < N // 2 else ix - N), 128 + (iy if iy < N // 2 else iy - N))
    print(bi, cid, 'iou %.3f k %.2f rot %d flip %d origin %d %d' % (best[0], best[1], best[2], best[3], best[4], best[5]), flush=True)
