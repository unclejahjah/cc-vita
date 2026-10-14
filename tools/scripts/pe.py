# pe.py EXE: sections, and references (as 32-bit VAs) to given file offsets
import sys, struct
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
magic = struct.unpack_from('<H', d, pe + 24)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0] if magic == 0x10b else struct.unpack_from('<Q', d, pe + 24 + 24)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    name = d[o:o + 8].rstrip(b'\0').decode()
    vsize, va, rsize, raw = struct.unpack_from('<IIII', d, o + 8)
    secs.append((name, va, vsize, raw, rsize))
    print('%-8s va %08x vsize %08x raw %08x rsize %08x' % (name, va, vsize, raw, rsize))
print('image base %x, magic %x' % (base, magic))

def off2va(off):
    for n, va, vs, raw, rs in secs:
        if raw <= off < raw + rs: return base + va + off - raw
def va2off(v):
    for n, va, vs, raw, rs in secs:
        if base + va <= v < base + va + rs: return raw + v - base - va

import re
for a in sys.argv[2:]:
    off = int(a)
    v = off2va(off)
    refs = [m.start() for m in re.finditer(re.escape(struct.pack('<I', v)), d)]
    print('offset %d -> va %08x, refs at %s' % (off, v, [(r, next((s[0] for s in secs if s[3] <= r < s[3] + s[4]), '?')) for r in refs[:10]]))
