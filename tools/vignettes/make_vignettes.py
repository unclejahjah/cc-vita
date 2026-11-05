#!/usr/bin/env python3
"""MENU_VIGNETTES.swf: the PC title's moving scenes behind the menu, from your own PC copy of the game.

    make_vignettes.py --modtool "Castle Mod Tool/castlecrashers-main" --pc-data "CastleCrashers/data" --out GAME_DIR/game

decrypts the PC's game/menu_vignettes.pak (castle.exe plays it under MENU.swf: eight scenes in one
2248-frame loop on a 1920x1080 stage) to MENU_VIGNETTES.swf, which the Vita's title then shows in
place of the original's painted scene (main.c title_screen, cc_title_vignettes). Without the file
the title stays as it was. The decryption is the Castle Mod Tool's (decrypt_asset.py), which must
be given; the output is game art, never commit it.
"""
import argparse, struct, sys, zlib
from pathlib import Path


def decrypt(da, pak):
    name, data = da.unzip_to_cok6_nrec(pak)
    raw = da.blowfish_decrypt(name, data)
    da.validate_checksum(raw)
    return da.decrypted_to_swf(raw)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--modtool', required=True, help='folder with the Castle Mod Tool decrypt_asset.py')
    ap.add_argument('--pc-data', required=True, help="the PC game's data folder (has game/menu_vignettes.pak)")
    ap.add_argument('--out', required=True, help='where MENU_VIGNETTES.swf goes (the Vita data game folder)')
    a = ap.parse_args()
    sys.path.insert(0, a.modtool)
    import decrypt_asset as da
    swf = decrypt(da, Path(a.pc_data) / 'game' / 'menu_vignettes.pak')
    if swf[:3] == b'CWS':   # the Vita reads only uncompressed SWFs
        body = zlib.decompress(swf[8:])
        swf = b'FWS' + swf[3:4] + struct.pack('<I', 8 + len(body)) + body
    if swf[:3] != b'FWS':
        sys.exit('menu_vignettes.pak: not a SWF the Vita can read (%r)' % swf[:3])
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'MENU_VIGNETTES.swf').write_bytes(swf)
    print(f'MENU_VIGNETTES.swf: {len(swf)} bytes, SWF {swf[3]}')


if __name__ == '__main__':
    main()
