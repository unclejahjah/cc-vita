# Disassembles the DoAction tags inside the given sprite ids.
import sys, struct
src = open(sys.argv[1]).read()
exec(src.split("path, kw = sys.argv[2], sys.argv[3]")[0].replace("exec(open(sys.argv[1]).read().split(\"for path in\")[0])",
     "exec(open(sys.argv[2]).read().split('for path in')[0])"))
d = open(sys.argv[3], 'rb').read()
nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8; start = 8 + rb + 4
want = set(int(x) for x in sys.argv[4:])
for code, p, l in tags(d, start, len(d)):
    if code == 39 and struct.unpack_from('<H', d, p)[0] in want:
        sid = struct.unpack_from('<H', d, p)[0]
        tagsin = [c for c, _, _ in tags(d, p + 4, p + l)]
        print('== sprite', sid, 'tags', tagsin)
        for c2, q, l2 in tags(d, p + 4, p + l):
            if c2 == 12:
                print('\n'.join(dis(d, q, q + l2, [])))
    elif code == 59 and struct.unpack_from('<H', d, p)[0] in want:
        print('== init action for', struct.unpack_from('<H', d, p)[0])
        print('\n'.join(dis(d, p + 2, p + l, [])))
