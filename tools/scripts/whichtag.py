# Which top-level tag (and nested sprite tag) contains a byte offset?
import sys, struct
exec(open(sys.argv[1]).read().split("for path in")[0])
d = open(sys.argv[2], 'rb').read()
off = int(sys.argv[3])
nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8; start = 8 + rb + 4
for code, p, l in tags(d, start, len(d)):
    if p <= off < p + l:
        print('top tag', code, 'body at', p, 'len', l)
        if code == 39:
            print(' sprite id', struct.unpack_from('<H', d, p)[0])
            for c2, q, l2 in tags(d, p + 4, p + l):
                if q <= off < q + l2:
                    print(' inner tag', c2, 'at', q, 'len', l2)
                    body = d[q:q + l2]
                    print(' ascii:', ''.join(chr(c) if 32 <= c < 127 else '.' for c in body[:600]))
        break
