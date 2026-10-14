# Handoff: state of the Castle Crashers Vita port (2026-10-14)

Goal: Castle Crashers running 1:1 on the PS Vita (then local/online co-op), by running the
game's own ActionScript 1 from the user's PC copy on a small AVM1 engine written here.

## Layout
- `src/as.h, as_core.c` – AVM1 interpreter (SWF6 rules, custom opcodes 0x70 Not, 0xA0-0xA4 Push).
- `src/as_mc.c` – movie clips: timelines (uid diff), frame-script queue, clip events, loadMovie.
- `src/as_lib.c` – Object/Array/String/Math/MovieClip/Color/Sound built-ins.
- `src/as_render.c` – draws the clip tree; text fields (host bitmap font or embedded font).
- `src/cc_host.c` – the natives the PC exe gives the scripts (Key.isDown via cc_keys[], storage,
  flash globals, music, sessions...). Missing ones show up as "call to undefined function" warnings.
- `src/swf.c, render.c` – SWF parser (shapes, sprites, fonts, lossless bitmaps), tessellation, draw lists.
- `src/hostfont.c` – the exe's BMFont (data/fonts/synjUnicode_20) packed into a 512px atlas.
- `src/audio.c` – Vita mixer (mpg123): sfx/<linkage>.mp3 cached, music/<name>.mp3 streamed. Untested on hardware.
- `src/main.c` – Vita app. `src/viewer.c` (+level/player/enemy.c) – the older hand-written viewer, `-DCC_VIEWER=ON`.
- `tools/hosttest.c` – PC harness. `--as GAME_DIR MAIN.swf out frames every` runs the game headless and
  writes PPM frames. Env: CC_KEYS="frame:code[:hold],..", AS_PRINT, AS_DUMP, AS_TREE(+AS_TREE_AT),
  AS_HIDE, AS_CALLS, AS_WATCH, AS_TEXTDBG, CC_FONT_DIR.
- `tools/scripts/` – helper scripts (asrun.sh builds+runs hosttest; avm1.py disassembler; fmain.sh,
  findstr.sh, clipev.sh, py.sh...). Paths inside point at the original Windows/WSL machine – adjust.

## Build
VitaSDK (vdpm: vita2d, libjpeg-turbo; mpg123 ships with the SDK): `mkdir build && cd build && cmake .. && make`.
`sce_gap.ld` works around a vita-elf-create segment-overlap crash.

## Device layout (ux0 only)
ux0:data/CastleCrashers/{game,levels,fonts,sfx,music}/ from the user's own files; save.dat written by the app.

## Working (PC harness)
Boot → lobby/character select (widescreen) → map → Home (LEVEL13): intro, player walks, collision,
stairs, camera, waypoints, depth. Text with the host font, bitmaps.

## Known gaps / next steps
1. First on-device test of the engine build (performance, memory: heap 160 MB, meshes in CDRAM).
2. Menu strings: fields get `ntext = N`; the exe maps N to text but its code is SteamStub-encrypted
   (don't decrypt). Needs another legitimate source for the string table.
3. Verify audio on hardware; the music volume/options globals are not wired.
4. Remaining natives as warnings appear; enemies/combat under the real scripts beyond Home.
5. Co-op: ports 2-4 key codes are in f_GetButtons (see cc_host.c comment); online later.
