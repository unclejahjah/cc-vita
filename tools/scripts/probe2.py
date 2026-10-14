# Root places of a level SWF, with sprite sizes, labels, and DoAction strings.
import sys, struct, re
sys.path.insert(0, '')
exec(open(sys.argv[1]).read().split("for path in")[0])   # reuse tags()/cstr() from probe.py

class Bits:
    def __init__(s, d, p): s.d, s.p, s.b = d, p, 0
    def u(s, n):
        v = 0
        for _ in range(n):
            byte = s.d[s.p]; v = (v << 1) | ((byte >> (7 - s.b)) & 1); s.b += 1
            if s.b == 8: s.b = 0; s.p += 1
        return v
    def s_(s, n):
        v = s.u(n); return v - (1 << n) if n and v >> (n - 1) else v

def matrix(bt):
    sx = sy = 1.0; r0 = r1 = 0.0
    if bt.u(1): n = bt.u(5); sx = bt.s_(n) / 65536; sy = bt.s_(n) / 65536
    if bt.u(1): n = bt.u(5); r0 = bt.s_(n) / 65536; r1 = bt.s_(n) / 65536
    n = bt.u(5); tx = bt.s_(n) / 20; ty = bt.s_(n) / 20
    return (round(sx, 3), round(r0, 3), round(r1, 3), round(sy, 3), tx, ty)

path = sys.argv[2]
d = open(path, 'rb').read()
nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8; start = 8 + rb + 4
sprites, shapes, exports = {}, {}, {}
for code, p, l in tags(d, start, len(d)):
    if code == 39:
        cid = struct.unpack_from('<H', d, p)[0]
        nf = sum(1 for c, _, _ in tags(d, p + 4, p + l) if c == 1)
        labs = [cstr(d, q)[0] for c, q, _ in tags(d, p + 4, p + l) if c == 43]
        npl = sum(1 for c, _, _ in tags(d, p + 4, p + l) if c == 26)
        sprites[cid] = (nf, npl, labs)
    elif code in (2, 22, 32, 83):
        shapes[struct.unpack_from('<H', d, p)[0]] = l
    elif code == 56:
        n = struct.unpack_from('<H', d, p)[0]; q = p + 2
        for _ in range(n):
            cid = struct.unpack_from('<H', d, q)[0]; name, q = cstr(d, q + 2); exports[cid] = name
    elif code == 26:
        bt = Bits(d, p); flags = bt.u(8); bt.p = p + 1; bt.b = 0
        depth = struct.unpack_from('<H', d, p + 1)[0]; q = p + 3; cid = None
        if flags & 2: cid = struct.unpack_from('<H', d, q)[0]; q += 2
        m = None
        if flags & 4:
            bt = Bits(d, q); m = matrix(bt); q = bt.p + (1 if bt.b else 0)
        name = cstr(d, q)[0] if flags & 0x20 else None
        print('  name:', name)
        print('ROOT depth %d cid %s %s matrix %s sprite %s export %s flags %02x' % (depth, cid,
              'sprite' if cid in sprites else 'shape' if cid in shapes else '?', m, sprites.get(cid), exports.get(cid), flags))
    elif code == 12:
        strs = re.findall(rb'[\x20-\x7e]{3,}', d[p:p + l])
        print('ROOT DoAction %d bytes, strings:' % l, [s.decode() for s in strs][:120])
    elif code == 148 or code == 20:
        print('TAG', code, 'len', l, 'at', p, 'first bytes', d[p:p + 16].hex())
