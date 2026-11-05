# Reads a BMFont binary (v3) font with its atlas and draws a line of text (for the LiveArea art).
import struct
from PIL import Image
def load(fnt, dds):
    d = open(fnt, 'rb').read(); assert d[:4] == b'BMF\x03'
    p = 4; chars = {}; lh = base = 0
    while p < len(d):
        t, sz = d[p], struct.unpack_from('<I', d, p + 1)[0]; b = p + 5
        if t == 2: lh, base = struct.unpack_from('<HH', d, b)
        if t == 4:
            for q in range(b, b + sz, 20):
                cid, x, y, w, h, xo, yo, xa, pg, ch = struct.unpack_from('<IHHHHhhhBB', d, q)
                chars[cid] = (x, y, w, h, xo, yo, xa)
        p = b + sz
    return chars, lh, Image.open(dds).convert('RGBA')
def text(font, s, colour=(255, 255, 255)):
    chars, lh, atlas = font
    W = sum(chars[ord(c)][6] for c in s) + 8
    out = Image.new('RGBA', (W, lh + 8), (0, 0, 0, 0)); x = 4
    for c in s:
        X, Y, w, h, xo, yo, xa = chars[ord(c)]
        g = atlas.crop((X, Y, X + w, Y + h))
        a = g.split()[3] if g.getextrema()[3][0] < 255 else g.convert('L')
        solid = Image.new('RGBA', g.size, colour + (255,)); solid.putalpha(a)
        out.alpha_composite(solid, (x + xo, 4 + yo)); x += xa
    return out.crop(out.getbbox())
