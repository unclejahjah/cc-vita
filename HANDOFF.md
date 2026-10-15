# Handoff: state of the Castle Crashers Vita port (2026-10-15)

Goal: Castle Crashers running 1:1 on the PS Vita (then local/online co-op), by running the
game's own ActionScript 1 from the user's PC copy on a small AVM1 engine written here.

## Layout
- `src/as.h, as_core.c` – AVM1 interpreter (SWF6 rules, custom opcodes 0x70 Not, 0xA0-0xA4 Push).
  Pinned variables (as_pin), native overrides of script functions (as_override), lazy GC.
- `src/as_mc.c` – movie clips: timelines (uid diff), frame-script queue, clip events, loadMovie.
  A placed clip's onClipEvent(load) runs after its own first frame, before its children's.
- `src/as_lib.c` – Object/Array/String/Math/MovieClip/Color/Sound built-ins.
- `src/as_render.c` – draws the clip tree; text fields (host bitmap font or embedded font).
- `src/cc_host.c` – the natives the exe gives the scripts (Key.isDown via cc_keys[], storage,
  flash globals, music, GetCheat=false, GetLocalGamerTag...), the console_version pins, level
  repairs (cc_repair_level, cc_repair_cut_short for EBARBOSS).
- `src/cc_native.c` – native BSP collision; f_GetDefaultWeapon (seeds new characters: default
  weapon, 1 point per stat, as the exe does).
- `src/cc_buttons.c` – Xbox button art -> PlayStation balls and Vita L/R shoulder slabs.
- `src/swf.c, render.c` – SWF parser (shapes, sprites, fonts, lossless bitmaps, tag 148 trigger
  boxes), tessellation, draw lists.
- `src/hostfont.c, strtab.c` – BMFont atlas; text table by ntext number (strings_en.txt, not in git).
- `src/audio.c` – mpg123 mixer: sfx/<linkage>.mp3, music/<name>.mp3.
- `src/main.c` – Vita app: loader, renderer, pad mapping, pause menu, overlay (SELECT+UP).
- `tools/hosttest.c` – PC harness (`--as GAME_DIR MAIN.swf out frames every`). Env: CC_KEYS,
  CC_CALL (frame:fn:args, 'str #num path), CC_SET (frame:path.name=num), AS_PRINT, AS_TRACE_EVERY,
  AS_GOTO, AS_CALLS, CC_STRINGS, CC_FONT_DIR, CC_PS_BUTTONS, CC_REPAIR_DEBUG.

## Build / device
VitaSDK in WSL; `cmake .. && make` in build/. Vita: ux0:data/CastleCrashers/{game,levels,fonts,sfx,
music}/, strings_en.txt, strings_vita_en.txt, save.dat. VPK to ux0:downloads via VitaShell FTP.

## Facts that cost time (read before debugging)
- Levels use a custom tag 148 for invisible trigger/hit boxes (id, s32 rect, 64x64 pixels).
- MAIN.swf ships developer stubs on _root (GetCheat on for most cheats). The exe says no cheats.
- console_version: the exe's value is true. Pinned true only inside f_LoadCharacterData,
  f_HudWaitEnd, f_Depth, f_RemovePlayer (death -> map), f_Walk (full tilt runs, light tilt walks:
  keys 37-40 run, 117-120 walk). Pinning it everywhere breaks the script front end; pinning
  f_MoveCharH/V broke movement.
- The decompiled "Raw Game Files CC/Game Files/Source Code/*Actionscript.txt" ignores opcode
  0x70, so many if-conditions there are inverted. Check P-Code.txt.
- About 60 of the user's .swf files are missing their last 125-128 bytes (PAUSE.swf half).
  Complete copies come from the user's own Castle Mod Tool extraction (pakoutput). Done so far:
  LEVEL8/14/15/19/27/39 (in level-fixes/). EBARBOSS is repaired in code.

## Done and checked on the PC (2026-10-15)
Sandwich (beefy) door rip, weapon pickup/banner/stats bubble, weapon frog swapping (>64-arg
calls), item switching (right stick), respawn after death, walk/run, no cheats + seeded new
characters, Barbarian Boss ending (chest, gold, princess, kiss, map), text table (282 ntexts),
Vita L/R art.

Also: character select levels (console branch), 100% Blue Knight save
(tools/make_save100.py -> ux0:data/CastleCrashers/save.dat; the user's old save is
save_backup_20261015.dat there and in ../saves/). Save layout per character at
64 + 48*(p_type-1): 128 flag, level-1, exp x4, weapon, animal, str, def, mag, agi, normal
levels x3, potions, bombs, sandwiches, relics, gold x4, insane flag, insane levels x3.
Global: 7 arenas, 8-11 animals, 12-27 items, 28-35 expansion items. p_type 1 green, 2 red,
3 blue, 4 orange.

## Next (in order)
1. Title screen and main menu (the exe draws them in the original; MENU.swf has the art).
2. Heat shimmer (AddShader "Heat", LEVEL30 sprite 7 etc.): distorts the screen behind a haze
   layer; needs a framebuffer copy. "RasterHack" (skies) is a seam fix, nothing to draw.
3. Remaining blank lobby text (476, 581, 805, 806, 815-825) - lobby is online-only.
4. Cut-short files: patch case by case (user parked a full re-extraction).
5. Performance (needs the Vita overlay numbers), then Wi-Fi co-op.
