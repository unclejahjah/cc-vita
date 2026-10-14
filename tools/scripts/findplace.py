# findplace.py probe.py file.swf NAME: character ids placed under instance NAME (any timeline)
import sys, struct
exec(open(sys.argv[1]).read().split("for path in")[0])
d = open(sys.argv[2], 'rb').read()
nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8; start = 8 + rb + 4
want = sys.argv[3].encode()
def walk(p, end, owner):
    for code, q, l in tags(d, p, end):
        if code == 39:
            walk(q + 4, q + l, struct.unpack_from('<H', d, q)[0])
        elif code == 26:
            fl = d[q]
            if fl & 0x20 and want in d[q:q + l]:
                cid = struct.unpack_from('<H', d, q + 3)[0] if fl & 2 else None
                print('in', owner, 'PlaceObject2 flags %02x depth %d cid %s' % (fl, struct.unpack_from('<H', d, q + 1)[0], cid))
walk(start, len(d), 'root')
