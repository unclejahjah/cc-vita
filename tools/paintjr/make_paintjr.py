#!/usr/bin/env python3
"""Paint Jr.'s files for the Vita (src/paint_jr.h), from your own PC copy of the game.

    make_paintjr.py --modtool "Castle Mod Tool/castlecrashers-main" --pc-data "CastleCrashers/data" --out GAME_DIR/game

decrypts the PC's player_new.pak into PLAYER_NEW.swf and makes PAINTJR.swf: one DoAction with
Paint Jr.'s script functions copied out of the PC's MAIN.swf (main.pak), each with the constant
pool it was written against. The decryption is the Castle Mod Tool's (decrypt_asset.py), which
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
SELECTORS = (b'f_Helmet\0', b'f_Belt\0', b'f_Shield\0')   # part clips showing a character's frame
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


def strip_to_paint_jr(d):
    """PLAYER_NEW.swf with only Paint Jr.'s frame of the part clips: the shapes only the other
    characters' frames show are emptied (ids and timelines stay), which more than halves the
    meshes the Vita has to keep for him"""
    nb = d[8] >> 3
    st = 8 + (5 + nb * 4 + 7) // 8 + 4
    top, sprites, selector = [], {}, set()
    for c, p, l in tags(d, st, len(d)):
        top.append((c, p, l))
        if c == 39:
            sid = struct.unpack_from('<H', d, p)[0]
            sprites[sid] = placements(d, p + 4, p + l)
            for c2, q, l2 in tags(d, p + 4, p + l):
                if c2 == 1:
                    break
                if c2 == 12 and any(s in d[q:q + l2] for s in SELECTORS) and len(sprites[sid]) >= FRAME:
                    selector.add(sid)
    root = {}
    for c, p, l in top:   # the root timeline's placements
        if c == 26 and d[p] & 2:
            root[struct.unpack_from('<H', d, p + 3)[0]] = 1

    def children(sid):   # what a sprite shows: a part clip only its frame for him
        fr = sprites[sid]
        return set().union(*(set(dl.values()) for f, dl in fr.items() if sid not in selector or f == FRAME))

    keep, todo = set(), list(root) + [s for s in sprites if s not in selector]
    while todo:   # everything reachable without the other characters' frames
        c = todo.pop()
        if c in keep:
            continue
        keep.add(c)
        if c in sprites:
            todo += children(c)
    others, todo = set(), []
    for s in selector:
        for f, dl in sprites[s].items():
            if f != FRAME:
                todo += list(dl.values())
    while todo:
        c = todo.pop()
        if c in others:
            continue
        others.add(c)
        if c in sprites:
            todo += [x for dl in sprites[c].values() for x in dl.values()]
    blank = others - keep
    out, n = bytearray(d[:st]), 0
    for c, p, l in top:
        body = d[p:p + l]
        if c in SHAPES and struct.unpack_from('<H', d, p)[0] in blank:
            # an empty shape: no bounds, no styles, just the end record
            body = d[p:p + 2] + b'\0' + (b'\0\0' if c == 83 else b'') + b'\0\0\0\0'
            n += 1
        out += struct.pack('<HI', c << 6 | 63, len(body)) + body
    struct.pack_into('<I', out, 4, len(out))
    return bytes(out), n


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
    (out / 'PLAYER_NEW.swf').write_bytes(player_new)
    pj, n = strip_to_paint_jr(player_new)
    (out / 'PLAYER_PJ.swf').write_bytes(pj)
    print('PLAYER_PJ.swf: %d shapes of the other characters emptied' % n)

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
    print('PLAYER_NEW.swf and PAINTJR.swf (%s) written to %s' % (', '.join(FUNCTIONS), out))


if __name__ == '__main__':
    main()
