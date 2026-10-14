# clipev.py avm1.py probe.py file.swf body_offset body_len : disassemble a PlaceObject2's clip actions
import sys, struct
src = open(sys.argv[1]).read()
exec(src.split("path, kw = sys.argv[2], sys.argv[3]")[0].replace("exec(open(sys.argv[1]).read().split(\"for path in\")[0])",
     "exec(open(sys.argv[2]).read().split('for path in')[0])"))
d = open(sys.argv[3], 'rb').read()
p0, ln = int(sys.argv[4]), int(sys.argv[5])
end = p0 + ln
fl = d[p0]
p = p0 + 3
if fl & 2: p += 2
class B:
    def __init__(s, p): s.p = p; s.b = 0
    def bits(s, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((d[s.p] >> (7 - s.b)) & 1)
            s.b += 1
            if s.b == 8: s.b = 0; s.p += 1
        return v
    def align(s):
        if s.b: s.b = 0; s.p += 1
        return s.p
if fl & 4:
    r = B(p)
    if r.bits(1): n = r.bits(5); r.bits(n); r.bits(n)
    if r.bits(1): n = r.bits(5); r.bits(n); r.bits(n)
    n = r.bits(5); r.bits(n); r.bits(n)
    p = r.align()
if fl & 8:
    r = B(p)
    ha, hm = r.bits(1), r.bits(1); n = r.bits(4)
    if hm: [r.bits(n) for _ in range(4)]
    if ha: [r.bits(n) for _ in range(4)]
    p = r.align()
if fl & 0x10: p += 2
if fl & 0x20:
    name = d[p:d.index(b'\0', p)].decode('latin1'); print('name', name); p += len(name) + 1
if fl & 0x40: p += 2
p += 2 + 4
while p + 8 <= end:
    f, size = struct.unpack_from('<II', d, p)
    if not f: break
    body = p + 8 + (1 if f & 0x20000 else 0)
    print('--- event flags 0x%x' % f)
    print('\n'.join(dis(d, body, p + 8 + size, [])))
    p += 8 + size
