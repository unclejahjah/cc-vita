# Lists named instances inside the level's game.game sprite with their character's export name.
import sys, struct, collections
exec(open(sys.argv[1]).read().split("for path in")[0])
d = open(sys.argv[2], 'rb').read()
nbits = d[8] >> 3; rb = (5 + 4 * nbits + 7) // 8; start = 8 + rb + 4

def skip_matrix(q):
    bits = ''.join(format(b, '08b') for b in d[q:q + 40]); i = 0
    for _ in range(2):
        if bits[i] == '1': n = int(bits[i+1:i+6], 2); i += 6 + 2 * n
        else: i += 1
    n = int(bits[i:i+5], 2); i += 5 + 2 * n
    return q + (i + 7) // 8

def places(p, e):
    for code, q, l in tags(d, p, e):
        if code == 26:
            flags = d[q]; r = q + 3; cid = None
            if flags & 2: cid = struct.unpack_from('<H', d, r)[0]; r += 2
            if flags & 4: r = skip_matrix(r)
            if flags & 8: continue   # cxform: names after it; skip for brevity
            name = cstr(d, r)[0] if flags & 0x20 else None
            yield cid, name
sprites, exports = {}, {}
for code, p, l in tags(d, start, len(d)):
    if code == 39: sprites[struct.unpack_from('<H', d, p)[0]] = (p + 4, p + l)
    elif code == 56:
        n = struct.unpack_from('<H', d, p)[0]; q = p + 2
        for _ in range(n):
            cid = struct.unpack_from('<H', d, q)[0]; name, q = cstr(d, q + 2); exports[cid] = name
root = list(places(start, len(d)))
game = [c for c, n in root if n == 'game'][0]
inner = [c for c, n in places(*sprites[game]) if n == 'game'][0]
cnt = collections.Counter()
for cid, name in places(*sprites[inner]):
    key = (name.rstrip('0123456789') if name else None, exports.get(cid))
    cnt[key] += 1
for (n, e), c in cnt.most_common(): print('%4d  name=%-20s export=%s' % (c, n, e))
