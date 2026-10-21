import os
import sys, struct, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../prototypes/swfproto'))
import swf as S
def load(path):
    d = open(path, 'rb').read()
    nb = d[8] >> 3; start = 8 + (5 + 4*nb + 7)//8 + 4
    spr = {}; exports = {}; shapes = {}
    for code, q, l in S.read_tags(d, start, len(d)):
        if code == 39:
            cid, nf = struct.unpack_from('<HH', d, q); spr[cid] = (nf, q, l)
        elif code in (2,22,32,83):
            cid = struct.unpack_from('<H', d, q)[0]; b = S.Bits(d, q+2); shapes[cid] = b.rect()
        elif code == 56:
            n = struct.unpack_from('<H', d, q)[0]; r = q + 2
            for _ in range(n):
                i = struct.unpack_from('<H', d, r)[0]; e = d.index(b'\0', r + 2)
                exports[i] = d[r+2:e].decode('latin1'); r = e + 1
    return d, spr, exports, shapes
