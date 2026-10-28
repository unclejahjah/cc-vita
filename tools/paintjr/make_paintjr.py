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
    (out / 'PLAYER_NEW.swf').write_bytes(decrypt(da, game / 'player_new.pak'))

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
