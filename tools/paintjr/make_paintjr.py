#!/usr/bin/env python3
"""Paint Jr.'s files for the Vita (src/paint_jr.h), from your own PC copy of the game.

    make_paintjr.py --modtool "Castle Mod Tool/castlecrashers-main" --pc-data "CastleCrashers/data" --out GAME_DIR/game

decrypts the PC's player_new.pak and makes from it PJPARTS.swf (Paint Jr.'s own pieces of the part
clips, drawn over the original PLAYER.swf's: see PARTS) and FRESH01.swf.. (the FRESH look: one
file per frame of the part clips), PAINTJR.swf: one DoAction with Paint Jr.'s script
functions copied out of the PC's MAIN.swf (main.pak), each with the constant pool it was written
against, and levels/LEVEL15_NEW.swf (the PC's castle, whose weapon rack has his pencil). The decryption is the Castle Mod Tool's (decrypt_asset.py), which
must be given; nothing here holds game data, and the outputs must never go into git.
"""
import argparse, struct, sys
from pathlib import Path

FUNCTIONS = ('f_PaintJrMagicSplash', 'f_ProjectileHitPaintJrSplash', 'f_ProjectilePhase')


def decrypt(da, pak):
    name, data = da.unzip_to_cok6_nrec(pak)
    raw = da.blowfish_decrypt(name, data)
    da.validate_checksum(raw)
    return da.decrypted_to_swf(raw)


def tags(d, pos, end):
    while pos + 2 <= end:
        cl, = struct.unpack_from('<H', d, pos); pos += 2
        c, l = cl >> 6, cl & 63
        if l == 63:
            if pos + 4 > end:
                break
            l, = struct.unpack_from('<I', d, pos); pos += 4
        yield c, pos, l
        pos += l
        if c == 0:
            break


def functions(d, start, end):
    """Top-level named DefineFunction2s of an action block: name -> (pool action bytes, function bytes)."""
    found, pool, p = {}, b'', start
    while p < end:
        op = d[p]; at = p; p += 1
        if op == 0:
            break
        ln = 0
        if op >= 0x80:
            ln, = struct.unpack_from('<H', d, p); p += 2
        body = p; p += ln
        if op == 0x88:
            pool = d[at:p]
        elif op in (0x8E, 0x9B):
            name = d[body:d.index(b'\0', body)].decode('latin1')
            size, = struct.unpack_from('<H', d, p - 2)
            p += size
            if name in FUNCTIONS:
                found[name] = (pool, d[at:p])
    return found


SHAPES = {2, 22, 32, 83}
FRAME = 33   # Paint Jr.'s frame of them (character 32)


def placements(d, p, end):
    """a sprite's timeline: per frame (1-based), the display list {depth: char} at its end"""
    frames, dl, f = {}, {}, 1
    for c, q, l in tags(d, p, end):
        if c == 1:
            frames[f] = dict(dl)
            f += 1
        elif c == 26:
            flags, depth = d[q], struct.unpack_from('<H', d, q + 1)[0]
            if flags & 2:
                dl[depth] = struct.unpack_from('<H', d, q + 3)[0]
        elif c == 28:
            dl.pop(struct.unpack_from('<H', d, q)[0], None)
        elif c == 70:
            sys.exit('PlaceObject3 in PLAYER_NEW.swf: not handled')
    return frames


# PJPARTS.swf: for each part clip of the original PLAYER.swf (by the MAIN selector its first frame
# calls and its order among them, as src/custom_char.c finds them), the PC PLAYER_NEW.swf sprite
# that is the same part (matched by eye: frame 4, Red Knight, side by side), and the frame of it
# to show (1-based). The sprites keep their timelines and ids; every shape another frame shows is
# emptied, so the file holds only Paint Jr.'s pieces and the pencil.
PARTS = [
    ('helmet', 0, 311, FRAME), ('helmet', 1, 619, FRAME), ('helmet', 2, 730, FRAME),
    ('helmet', 3, 832, FRAME), ('helmet', 4, 881, FRAME),
    ('shield', 0, 550, FRAME), ('shield', 1, 59, FRAME),
    ('belt', 0, 129, FRAME), ('belt', 1, 669, FRAME), ('belt', 2, 490, FRAME), ('belt', 3, 458, FRAME),
    ('belt', 4, 763, FRAME),
] + [('belt', 5 + i, s, FRAME) for i, s in enumerate(
    (1142, 1166, 1190, 1271, 1355, 1379, 1409, 1433, 1458, 1488, 1512, 1536, 1601, 1644, 1668, 1703, 1742,
     1782, 1806, 1837, 1861))] + [
    ('weapon', 0, 499, 86),   # weapon 86, his pencil (empty in the original PLAYER.swf)
]


# ---- fitting the PC's parts to the original's size ------------------------------------------
# The PC redrew the knights with bigger heads (its helmets are ~11% bigger than the original's,
# about the same place), so drawn over the original PLAYER.swf they look too big. With --player,
# each part is fitted to the original part clip it stands for: one scale and offset over the
# frames both have (the median of the bounding boxes'), baked into its timeline's matrices.

class Bits:
    def __init__(self, d, p):
        self.d, self.p, self.b = d, p, 0

    def u(self, n):
        v = 0
        for _ in range(n):
            v = v << 1 | self.d[self.p] >> (7 - self.b) & 1
            self.b += 1
            if self.b == 8:
                self.b, self.p = 0, self.p + 1
        return v

    def s(self, n):
        v = self.u(n)
        return v - (1 << n) if n and v >> (n - 1) else v

    def end(self):
        return self.p + (self.b > 0)


IDENTITY = (1.0, 0.0, 0.0, 1.0, 0, 0)   # a b c d tx ty: x' = a x + c y + tx, y' = b x + d y + ty


def read_matrix(d, p):
    """(matrix, the offset after it)"""
    b = Bits(d, p)
    a, rb, rc, dd = 1.0, 0.0, 0.0, 1.0
    if b.u(1):
        n = b.u(5); a = b.s(n) / 65536; dd = b.s(n) / 65536
    if b.u(1):
        n = b.u(5); rb = b.s(n) / 65536; rc = b.s(n) / 65536
    n = b.u(5)
    tx = b.s(n); ty = b.s(n)
    return (a, rb, rc, dd, tx, ty), b.end()


def write_matrix(m):
    bits = []

    def put(v, n):
        bits.extend((v >> (n - 1 - i)) & 1 for i in range(n))

    def nbits(*vs):
        return max(max(v.bit_length() if v >= 0 else (~v).bit_length() for v in vs) + 1, 1)

    a, rb, rc, dd, tx, ty = m
    fa, fd, fb, fc = (int(round(v * 65536)) for v in (a, dd, rb, rc))
    for has, x, y in ((fa != 65536 or fd != 65536, fa, fd), (fb or fc, fb, fc)):
        put(1 if has else 0, 1)
        if has:
            n = nbits(x, y); put(n, 5); put(x & (1 << n) - 1, n); put(y & (1 << n) - 1, n)
    tx, ty = int(round(tx)), int(round(ty))
    n = nbits(tx, ty) if tx or ty else 0
    put(n, 5)
    if n:
        put(tx & (1 << n) - 1, n); put(ty & (1 << n) - 1, n)
    bits += [0] * (-len(bits) % 8)
    return bytes(int(''.join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))


def mul(p, m):
    """p after m"""
    A, B, C, D, TX, TY = p
    a, b, c, d, tx, ty = m
    return (A * a + C * b, B * a + D * b, A * c + C * d, B * c + D * d, A * tx + C * ty + TX, B * tx + D * ty + TY)


def library(d):
    """shape bounds {id: (xmin, xmax, ymin, ymax)}, sprite timelines {id: [{depth: (char, matrix)}]},
    each sprite's first-frame actions {id: bytes}"""
    nb = d[8] >> 3
    st = 8 + (5 + nb * 4 + 7) // 8 + 4
    shapes, sprites, acts = {}, {}, {}
    for c, p, l in tags(d, st, len(d)):
        if p + l > len(d):   # (a file cut before its End)
            break
        if c in SHAPES:
            b = Bits(d, p + 2); n = b.u(5)
            shapes[struct.unpack_from('<H', d, p)[0]] = tuple(b.s(n) for _ in range(4))
        elif c == 39:
            sid = struct.unpack_from('<H', d, p)[0]
            frames, dl, a0 = [], {}, b''
            for c2, q, l2 in tags(d, p + 4, p + l):
                if c2 == 1:
                    frames.append(dict(dl))
                elif c2 == 12 and not frames:
                    a0 += d[q:q + l2]
                elif c2 == 26:
                    fl, depth = d[q], struct.unpack_from('<H', d, q + 1)[0]
                    r, was = q + 3, dl.get(depth)
                    ch = struct.unpack_from('<H', d, r)[0] if fl & 2 else was and was[0]
                    r += 2 if fl & 2 else 0
                    m = read_matrix(d, r)[0] if fl & 4 else was[1] if was and fl & 1 else IDENTITY
                    dl[depth] = (ch, m)
                elif c2 == 28:
                    dl.pop(struct.unpack_from('<H', d, q)[0], None)
            sprites[sid], acts[sid] = frames, a0
    return shapes, sprites, acts


def bounds(lib, cid, frame=0, m=IDENTITY, depth=0):
    shapes, sprites, _ = lib
    if cid in shapes:
        x0, x1, y0, y1 = shapes[cid]
        pts = [(m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]) for x in (x0, x1) for y in (y0, y1)]
        return [min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts), max(p[1] for p in pts)]
    if cid not in sprites or not sprites[cid] or depth > 8:
        return None
    out = None
    for ch, cm in sprites[cid][min(frame, len(sprites[cid]) - 1)].values():
        r = bounds(lib, ch, 0, mul(m, cm), depth + 1)
        if r:
            out = r if not out else [min(out[0], r[0]), max(out[1], r[1]), min(out[2], r[2]), max(out[3], r[3])]
    return out


def fits(player, player_new):
    """{PLAYER_NEW sprite id: matrix} taking each PARTS sprite onto its original part clip"""
    import statistics
    old, new = library(player), library(player_new)
    sel = {b'f_Helmet\0': 'helmet', b'f_Belt\0': 'belt', b'f_Weapon\0': 'weapon', b'f_Shield\0': 'shield'}
    clips, nth = {}, {}
    for sid in sorted(old[1]):   # in id order, as custom_char.c counts them
        g = next((g for s, g in sel.items() if s in old[2][sid]), None)
        if g:
            clips[g, nth.get(g, 0)] = sid
            nth[g] = nth.get(g, 0) + 1
    out = {}
    for g, n, sid, _ in PARTS:
        if g == 'weapon' or (g, n) not in clips:   # (the pencil has no original)
            continue
        pairs = []
        for f in range(min(len(old[1][clips[g, n]]), len(new[1][sid]))):
            a, b = bounds(old, clips[g, n], f), bounds(new, sid, f)
            if a and b and a[1] > a[0] and a[3] > a[2] and b[1] > b[0] and b[3] > b[2]:
                pairs.append((a, b))
        if not pairs:
            continue
        s = statistics.median((((a[1] - a[0]) * (a[3] - a[2])) / ((b[1] - b[0]) * (b[3] - b[2]))) ** .5 for a, b in pairs)
        tx = statistics.median(((a[0] + a[1]) - s * (b[0] + b[1])) / 2 for a, b in pairs)
        ty = statistics.median(((a[2] + a[3]) - s * (b[2] + b[3])) / 2 for a, b in pairs)
        out[sid] = (s, 0.0, 0.0, s, tx, ty)
        print('  %s %d (sprite %d): scale %.3f, offset %.1f, %.1f px' % (g, n, sid, s, tx / 20, ty / 20))
    return out


def refit(body, fit):
    """a DefineSprite body with every PlaceObject2 of its timeline put through fit"""
    out = bytearray(body[:4])
    for c, q, l in tags(body, 4, len(body)):
        t = body[q:q + l]
        if c == 26 and (t[0] & 4 or t[0] & 2 and not t[0] & 1):   # a matrix, or a new placement (identity)
            fl, r = t[0], 3 + (2 if t[0] & 2 else 0)
            m, e = read_matrix(t, r) if fl & 4 else (IDENTITY, r)
            t = bytes([fl | 4]) + t[1:r] + write_matrix(mul(fit, m)) + t[e:]
        out += struct.pack('<HI', c << 6 | 63, len(t)) + t if c else struct.pack('<H', 0)
    return bytes(out)


def make_parts(d, frame_of, fit=None):
    """the PARTS sprites, each showing only frame_of(group, its frame in PARTS) (None: left out),
    fitted by fit {sprite id: matrix}"""
    nb = d[8] >> 3
    st = 8 + (5 + nb * 4 + 7) // 8 + 4
    defs, sprites, order = {}, {}, []
    for c, p, l in tags(d, st, len(d)):
        if c in SHAPES or c == 39:
            cid = struct.unpack_from('<H', d, p)[0]
            defs[cid] = (c, d[p:p + l])
            order.append(cid)
            if c == 39:
                sprites[cid] = placements(d, p + 4, p + l)
    parts = [(g, n, sid, frame_of(g, f)) for g, n, sid, f in PARTS]
    parts = [p for p in parts if p[3] is not None and p[3] <= len(sprites[p[2]])]
    keep, todo = set(), []
    for _, _, sid, frame in parts:
        keep.add(sid)
        todo += list(sprites[sid][frame].values())
    while todo:   # what those frames show, all of it (nested clips play)
        c = todo.pop()
        if c in keep or c not in defs:
            continue
        keep.add(c)
        if c in sprites:
            todo += [x for dl in sprites[c].values() for x in dl.values()]
    referenced = set()
    for s in keep:
        if s in sprites:
            referenced |= {x for dl in sprites[s].values() for x in dl.values()}
    out = bytearray(d[:st])
    for cid in order:
        if cid not in keep and cid not in referenced:
            continue
        c, body = defs[cid]
        if cid not in keep:   # placed by another frame: an empty stand-in under the same id
            if c == 39:
                c, body = 39, body[:2] + struct.pack('<H', 1) + struct.pack('<HH', 1 << 6, 0)
            else:
                body = body[:2] + b'\0' + (b'\0\0' if c == 83 else b'') + b'\0\0\0\0'
        elif fit and cid in fit:
            body = refit(body, fit[cid])
        out +=struct.pack('<HI', c << 6 | 63, len(body)) + body
    names = [('pj_%s_%d' % (g, n), sid) for g, n, sid, _ in parts]
    exp = struct.pack('<H', len(names)) + b''.join(struct.pack('<H', s) + n.encode() + b'\0' for n, s in names)
    out += struct.pack('<HI', 56 << 6 | 63, len(exp)) + exp
    out += struct.pack('<H', 1 << 6) + struct.pack('<H', 0)
    struct.pack_into('<I', out, 4, len(out))
    struct.pack_into('<H', out, st - 2, 1)   # one root frame
    return bytes(out), len(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--modtool', required=True, help='folder with the Castle Mod Tool decrypt_asset.py')
    ap.add_argument('--pc-data', required=True, help="the PC game's data folder (has game/main.pak)")
    ap.add_argument('--out', required=True, help='where PJPARTS, FRESHnn and PAINTJR.swf go (the game folder)')
    ap.add_argument('--levels-out', help='where LEVEL15_NEW.swf goes (default: the levels folder beside --out)')
    ap.add_argument('--player', help="the original PLAYER.swf the game runs: the PC's parts are fitted to its size")
    a = ap.parse_args()
    sys.path.insert(0, a.modtool)
    import decrypt_asset as da
    game, out = Path(a.pc_data) / 'game', Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    player_new = decrypt(da, game / 'player_new.pak')
    fit = None
    if a.player:
        print('fitting the parts to %s:' % a.player)
        fit = fits(Path(a.player).read_bytes(), player_new)
    else:
        print('no --player: the parts keep the PC size (bigger helmets)')
    parts, n = make_parts(player_new, lambda g, f: f, fit)
    (out / 'PJPARTS.swf').write_bytes(parts)
    print('PJPARTS.swf: %d parts' % n)
    # the FRESH look: FRESHnn.swf, the PC's version of frame nn of every part clip (not the
    # weapons), loaded for the frames the players' clips show
    nfr = 0
    for f in range(1, 64):
        parts, n = make_parts(player_new, lambda g, _: None if g == 'weapon' else f, fit)
        if not n:
            break
        (out / ('FRESH%02d.swf' % f)).write_bytes(parts)
        nfr = f
    print('FRESH01.swf .. FRESH%02d.swf' % nfr)

    d = decrypt(da, game / 'main.pak')
    if d[:3] != b'FWS':
        sys.exit('main.pak: not an uncompressed SWF')
    nb = d[8] >> 3
    st = 8 + (5 + nb * 4 + 7) // 8 + 4
    found = {}
    for c, p, l in tags(d, st, len(d)):
        if c == 12:
            found.update(functions(d, p, p + l))
    missing = [f for f in FUNCTIONS if f not in found]
    if missing:
        sys.exit('not in this MAIN.swf: ' + ', '.join(missing))
    code = b''.join(found[f][0] + found[f][1] for f in FUNCTIONS) + b'\0'
    body = b'\0'                                     # an empty stage RECT (5 bits of 0)
    body += d[st - 4:st]                             # frame rate, then a frame count of 1
    body = body[:3] + struct.pack('<H', 1)
    body += struct.pack('<HI', 12 << 6 | 63, len(code)) + code
    body += struct.pack('<H', 1 << 6) + struct.pack('<H', 0)   # ShowFrame, End
    swf = b'FWS' + d[3:4] + struct.pack('<I', 8 + len(body)) + body
    (out / 'PAINTJR.swf').write_bytes(swf)
    print('PAINTJR.swf (%s) written to %s' % (', '.join(FUNCTIONS), out))
    # the PC's castle: its weapon rack has his pencil (w86)
    levels = Path(a.levels_out) if a.levels_out else out.parent / 'levels'
    levels.mkdir(parents=True, exist_ok=True)
    (levels / 'LEVEL15_NEW.swf').write_bytes(decrypt(da, Path(a.pc_data) / 'levels' / 'level15_new.pak'))
    print('LEVEL15_NEW.swf written to %s' % levels)


if __name__ == '__main__':
    main()
