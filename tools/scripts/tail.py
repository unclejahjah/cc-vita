# Where does each level's SWF end (End tag), and what follows it?
import sys, struct, glob, os
exec(open(sys.argv[1]).read().split("for path in")[0])
for f in sys.argv[2:]:
    d = open(f, 'rb').read()
    nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8; start = 8 + rb + 4
    end = None; last = None
    for code, p, l in tags(d, start, len(d)):
        last = (code, p, l)
        if code == 0: end = p; break
    hdr = struct.unpack_from('<I', d, 4)[0]
    print('%s size %d header %d end-tag at %s last tag %s' % (os.path.basename(f), len(d), hdr, end, last[:1] + (last[2],)))
    if end is not None and end < len(d):
        t = d[end:end + 256]
        print('  after End:', t[:64].hex())
        print('  ascii   :', ''.join(chr(c) if 32 <= c < 127 else '.' for c in t[:200]))
