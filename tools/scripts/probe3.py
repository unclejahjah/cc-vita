# One line per level: stage size, music, root layer names.
import sys, struct, re, glob, os
exec(open(sys.argv[1]).read().split("for path in")[0])
exec(open(sys.argv[2]).read().split("path = sys.argv[2]")[0].split("exec(")[0] + open(sys.argv[2]).read().split("def matrix")[0].split("class Bits")[1].join(["class Bits", ""]) if False else "")
def layers(d):
    nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8; start = 8 + rb + 4
    w = None
    out, music, bmp = [], '', 0
    for code, p, l in tags(d, start, len(d)):
        if code == 26:
            flags = d[p]; q = p + 3
            if flags & 2: q += 2
            if flags & 4:
                # skip matrix bits
                bits = ''.join(format(b, '08b') for b in d[q:q + 30]); i = 0
                if bits[i] == '1': n = int(bits[i+1:i+6], 2); i += 6 + 2 * n
                else: i += 1
                if bits[i] == '1': n = int(bits[i+1:i+6], 2); i += 6 + 2 * n
                else: i += 1
                n = int(bits[i:i+5], 2); i += 5 + 2 * n
                q += (i + 7) // 8
            if flags & 8: continue
            if flags & 0x20: out.append(cstr(d, q)[0])
        elif code == 12:
            m = re.search(rb'music/[\w.]+', d[p:p + l]); music = m.group().decode() if m and not music else music
        elif code in (6, 20, 21, 35, 36): bmp += 1
    return out, music, bmp
r = sys.argv[3]
for f in sorted(glob.glob(r + '/levels/*.swf'), key=lambda s: int(re.findall(r'\d+', os.path.basename(s))[0])):
    d = open(f, 'rb').read()
    nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8
    bits = int.from_bytes(d[8:8 + rb], 'big'); tot = rb * 8
    v = [(bits >> (tot - 5 - nbits * (i + 1))) & ((1 << nbits) - 1) for i in range(4)]
    names, music, bmp = layers(d)
    print('%-12s %5dx%-4d bmp%d %-28s %s' % (os.path.basename(f), (v[1]-v[0])//20, (v[3]-v[2])//20, bmp, music[6:], ' '.join(names)))
