#!/usr/bin/env python3
"""Paint Jr.'s files for the Vita (src/paint_jr.h), from your own PC copy of the game.

    make_paintjr.py --modtool "Castle Mod Tool/castlecrashers-main" --pc-data "CastleCrashers/data" --out GAME_DIR/game

decrypts the PC's player_new.pak and makes from it PJPARTS.swf (Paint Jr.'s own pieces of the part
clips, drawn over the original PLAYER.swf's: see PARTS) and FRESH01.swf.. (the FRESH look: one
file per frame of the part clips), and PAINTJR.swf: one DoAction with Paint Jr.'s script
functions copied out of the PC's MAIN.swf (main.pak), each with the constant pool it was written
against. The decryption is the Castle Mod Tool's (decrypt_asset.py), which
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


def make_parts(d, frame_of):
    """the PARTS sprites, each showing only frame_of(group, its frame in PARTS) (None: left out)"""
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
        out += struct.pack('<HI', c << 6 | 63, len(body)) + body
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
    ap.add_argument('--out', required=True, help='where PLAYER_NEW.swf and PAINTJR.swf go (the game folder)')
    a = ap.parse_args()
    sys.path.insert(0, a.modtool)
    import decrypt_asset as da
    game, out = Path(a.pc_data) / 'game', Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    player_new = decrypt(da, game / 'player_new.pak')
    parts, n = make_parts(player_new, lambda g, f: f)
    (out / 'PJPARTS.swf').write_bytes(parts)
    print('PJPARTS.swf: %d parts' % n)
    # the FRESH look: FRESHnn.swf, the PC's version of frame nn of every part clip (not the
    # weapons), loaded for the frames the players' clips show
    nfr = 0
    for f in range(1, 64):
        parts, n = make_parts(player_new, lambda g, _: None if g == 'weapon' else f)
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


if __name__ == '__main__':
    main()
