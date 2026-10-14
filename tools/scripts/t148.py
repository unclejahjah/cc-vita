# Dumps custom tag 148 of a level as int32s (and notes where it sits among tags).
import sys, struct
exec(open(sys.argv[1]).read().split("for path in")[0])
d = open(sys.argv[2], 'rb').read()
nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8; start = 8 + rb + 4
prev = None
for code, p, l in tags(d, start, len(d)):
    if code == 148:
        print('tag148 len', l, 'previous tag', prev)
        n = l // 4
        ints = struct.unpack_from('<%di' % n, d, p)
        print('first 80 ints:', ints[:80])
        print('last 40 ints:', ints[-40:])
        fl = struct.unpack_from('<%df' % n, d, p)
        print('as floats (first 40):', [round(x, 2) for x in fl[:40]])
    prev = (code, l)
