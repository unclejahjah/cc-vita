# Asset inventory (2026-10-12)

Jah's working folder on their PC: `C:\Users\Alex_\Documents\VITA Casle Crashers` (the Steam install is read-only, so never write to it).

## Steam PC install (copied to `VITA Casle Crashers\Steam PC copy\original`, with SHA-1 list in `...\notes\original.sha1`)
- 126 MB total, 739 files. Contains castle.exe (32-bit Windows) and steam_api.dll.
- `data/**/*.pak` (233 files) are plain ZIPs with stored, uncompressed entries. Each one holds a single `*.COK6.NREC`, `*.PDAG.NREC` or `*.PNG.NREC` entry.
- The NREC payloads look encrypted or compressed (high-entropy headers), and the format is still unknown.
- Audio is `.xma` (Xbox XMA, 466 files). There are also `.xpu`/`.xvu` shaders and a 16 MB DDS font atlas.

## Jah's "Raw Game Files" (already decoded, origin is a community decompile)
- `game/` and `levels/`: 123 uncompressed SWF files (FWS v6/v10), one per character, enemy, level, sky and menu.
- Rough tag census: about 406k PlaceObject2, 191k ShowFrame, 11.8k DefineShape, 9.5k DefineSprite, 7.8k DoAction. These are vector timeline animations, with only small frame scripts.
- `bsps/`: 74 `*.PDAG` files, magic "GADP" (that is "PDAG" read as little-endian), followed by little-endian floats. Probably collision/BSP geometry.
- `Sound/`: 491 MP3 conversions of the OST and SFX, with the XMA originals kept alongside.

## Implications
- Asset decryption is not needed, because these SWFs and the MP3s cover the art and audio.
- Gameplay logic lives in castle.exe, not in the SWFs, so it has to be reimplemented.
- The Vita needs either a SWF vector renderer (tessellate shapes at load, GXM) or PC-side baking to sprite atlases.

## Renderer prototype (2026-10-12)
A pure-Python parser and cairo renderer (`/mnt/project-files/code/swfproto`) renders the Green Knight's walk, punch and jump correctly from PLAYER.swf. See that folder's README for findings.
