# cc-vita

A native Castle Crashers port for the PS Vita. A small AVM1 engine runs the game's own ActionScript from your own PC copy of the game; the natives castle.exe gives the scripts are written by hand, and shapes are tessellated and drawn on the GPU.

No game data is included, and none may ever be committed (see `.gitignore`). At runtime the app reads the SWF files, fonts, sounds and text from your own copy of the game.

Repo layout: `src/` engine, `tools/` desktop host test and helper scripts, `HANDOFF.md` current state (read it first), `docs/research/` research notes.

## Build (VitaSDK)

```
mkdir build && cd build
cmake ..
make
```

This produces `cc_vita.vpk`. Install it with VitaShell. The game's files go in `ux0:data/CastleCrashers/` (layout in `HANDOFF.md`, "Build / device"). Options: `-DLIVEAREA_DIR=dir` for the LiveArea art made by `tools/livearea/make_livearea.sh`, `-DCC_TROPHIES=ON` for trophies (on hold).

## Desktop test

`tools/hosttest.c` runs the same engine on a PC and software-rasterises frames to PPM, so changes can be checked without a Vita:

```
gcc -O2 -Ithird_party/libtess2/Include -o hosttest tools/hosttest.c src/swf.c src/render.c \
  src/as_core.c src/as_mc.c src/as_lib.c src/as_render.c src/cc_host.c src/cc_native.c \
  src/cc_buttons.c src/hostfont.c src/strtab.c src/pause_menu.c src/help_book.c \
  third_party/libtess2/Source/*.c -lm -lz
./hosttest --as GAME_DIR MAIN.swf out FRAMES EVERY
```

Its environment switches (CC_KEYS, CC_CALL, ...) are listed in `HANDOFF.md`.

## Notes
- Masks (clip layers) use the stencil buffer: each mask is drawn with alpha 0 to increment the stencil, the masked content draws where the stencil equals the nesting level, and the mask is drawn again to decrement it.
- `sce_gap.ld` works around a VitaSDK quirk: `vita-elf-create` fails with "segment 1 overlaps" when the code happens to end just below a 64 KB boundary.
- `third_party/libtess2` is vendored from memononen/libtess2 (SGI Free Software License B). See `VERSION` for the commit.
