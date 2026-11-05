# selectors.py PLAYER.swf: sprites whose frame scripts call _root.f_Helmet/f_Belt/... -> cid, frames, fn
import os
import sys, struct, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../prototypes/swfproto')); sys.path.insert(0, sys.argv[0].rsplit('/',1)[0])
import swf as S
from sprites import load
d, spr, ex, shapes = load(sys.argv[1])
FN = [b'f_Helmet', b'f_Weapon', b'f_Shield', b'f_Emblem', b'f_Belt', b'f_Flag', b'f_Wings', b'f_ColorSwap', b'f_Face', b'f_Hair']
out = collections.defaultdict(list)
for cid, (nf, q, l) in spr.items():
    for code, p, ln in S.read_tags(d, q+4, q+l):
        if code == 12:
            blk = d[p:p+ln]
            for f in FN:
                if f + b'\0' in blk or blk.find(f) >= 0:
                    out[f.decode()].append((cid, nf))
for f, v in out.items(): print(f, len(v), sorted(set(v)))
