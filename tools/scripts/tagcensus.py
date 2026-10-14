# tagcensus.py DIR: counts of font/text/bitmap/sound tags (incl. inside sprites) per SWF
import sys, os, struct, glob
NAMES = {10: 'Font', 48: 'Font2', 75: 'Font3', 13: 'FontInfo', 62: 'FontInfo2', 11: 'Text', 33: 'Text2',
         37: 'EditText', 6: 'Jpeg', 21: 'Jpeg2', 35: 'Jpeg3', 20: 'Lossless', 36: 'Lossless2', 8: 'JpegTables',
         14: 'Sound', 15: 'StartSound', 18: 'SoundStreamHead', 19: 'SoundStreamBlock', 45: 'SoundStreamHead2',
         46: 'MorphShape', 7: 'Button', 34: 'Button2', 60: 'Video', 84: 'MorphShape2', 4: 'PlaceObject', 89: 'StartSound2'}

def tags(d, p, end):
    while p + 2 <= end:
        h = struct.unpack_from('<H', d, p)[0]; p += 2
        code, l = h >> 6, h & 63
        if l == 63:
            if p + 4 > end: return
            l = struct.unpack_from('<I', d, p)[0]; p += 4
        yield code, p, l
        if code == 0: return
        p += l

tot = {}
for path in sorted(glob.glob(os.path.join(sys.argv[1], '*', '*.swf')) + glob.glob(os.path.join(sys.argv[1], '*', '*.SWF'))):
    d = open(path, 'rb').read()
    nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8; start = 8 + rb + 4
    cnt = {}
    def walk(p, end):
        for code, q, l in tags(d, p, min(end, len(d))):
            if code in NAMES: cnt[NAMES[code]] = cnt.get(NAMES[code], 0) + 1
            if code == 39: walk(q + 4, q + l)
    walk(start, len(d))
    for k, v in cnt.items(): tot[k] = tot.get(k, 0) + v
    print(os.path.relpath(path, sys.argv[1]), ' '.join('%s:%d' % kv for kv in sorted(cnt.items()) if kv[0] not in ('PlaceObject',)))
print('TOTAL', tot)
