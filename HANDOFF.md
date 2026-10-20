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
  CC_CALL (frame:fn:args, 'str #num path; a.b.method is called on a.b), CC_SET
  (frame:path.name=num), AS_ARGS (print calls of named script functions), AS_PRINT, AS_TRACE_EVERY,
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
- console_version also reads true in every script of a level file (as_pin_in_levels, Movie.is_level):
  each level's f_Init writes it false and then gives p1 developer stats (magic/strength/punch 9999,
  magic_chain 2000; Final Battle enemies 3 health). That was the 999-damage / magic-maxing /
  endless-cast bug (fixed 2026-10-20). Loaded saves reset stats over 25 to 1, as the exe does.
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

## Done on 2026-10-15 (later)
Title + main menu (MENU.swf panel, 3 items, cc_menu_attach), "Vita Edition" badge
(cc_menu_vita_badge), prompt icons by action code (native f_GetButtonArt), map names and stat
numbers (console pins f_Goal, f_GuyInit, f_InitStatScreen, f_StatScreen), character select
levels, DLC characters 29-31 owned (cc_own_dlc_characters), general cut-short death-frame
repair (EWIZARD, enemies), heat shimmer (AddShader "Heat" -> render target wobble, Vita only),
no "Loading X.swf" text. 63 .pak files to re-extract are in
Desktop\Castle Mod Tool\castlecrashers-main\pak (pak_list.txt); outputs go to pakoutput.

## Jah's to-do list (2026-10-20), with first notes
Save-related (the 100% test save made by tools/make_save100.py sets every shared unlock byte,
port 0 slots 7-35, to "owned", and Blue Knight stats to 25). Jah confirmed it also happens on a
fresh character, but that character still sits on the same save.dat, so first replace save.dat
(Jah's own backup save_backup_20261015.dat, or none) and re-test before debugging:
- Animal orbs missing (animals 8-11 marked owned), key items unobtainable (items 12-35 owned).
  Jah also wants "some key items removed" (ask which: probably online/DLC-only ones).
- DONE 2026-10-20: Magic 999 damage / magic maxing itself / some attacks max damage / magic
  animation not ending (level developer stats, see Facts).
- Character levels wrong on character select.
Engine/file bugs:
- Bosses don't die and vanish: UFO (the small alien UFOs, LEVEL41 f_MakeUFO), Corn boss.
  CAUSE FOUND 2026-10-20: cut-short files. 58 files in "Raw Game Files-2026..." end before their
  End tag (check: the tag stream, not the header size, which is unreliable). UFO.swf ends inside
  its 104-frame death sprite; ECORN.swf inside its 278-frame death sprite (sprite 94), so the boss
  freezes on an empty "die" frame and stays an enemy. Complete copies: Jah's own
  "Steam PC copy/original/data/*/*.pak", decrypted with the Castle Mod Tool code (Jah approved,
  ask him before running it again) into ..\steam-complete\{game,levels} (55 files: the 58 minus
  MAIN, MENU, PLAYER, whose originals stay). That build is byte-identical to every uncut file
  in use. Tested on the PC with them: UFO dies (death anim, f_EnemyDie, removed); Corn boss
  dies (death anim, f_BringHorn/f_HornBrought, boss_fight ends). NOT yet: a full regression
  sweep with all 55 swapped in (FX, SKY*, MOUNT2, ENDING*, ~40 LEVELs), then FTP to the Vita
  (only after Jah says "ftp ready"). Test recipe: CC_CALL "1100:loader.f_SpawnCornBoss:#700|#400,
  1150:loader.game.game.e_cornboss.gotoAndStop:'spin2" (the boss-intro cutscene does the spin2),
  CC_SET health=1, then CC_CALL f_Damage on it. UFO: "1100:f_MakeUFO:#600|#400", CC_SET
  ufo1.health=0. Level functions live on _root.loader.
  Cut list includes LEVEL36 (Cyclops cave) and ENDING*: likely the Cyclops and final-boss items.
- Arenas turn all attacks into arrows.
- Cyclops cave: black screen but gameplay continues; Cyclops resurrection cutscene never ends.
- Ship level: ship hidden below the waves (depth/layer order).
- Knock-down physics missing from some attacks.
- Title: "Vita Edition" badge sits in a visible box and overlaps the logo; centre the Quit button.
Research:
- github.com/myuu-151/Castle-Crashers-GC (a GameCube port): check for reusable findings.
- Vita trophies from the Steam/PS3 list (homebrew trophy system research).
Files now on the Vita: uncut (FRESH) files except MENU, MAIN, PLAYER (originals).
Jah asked for future work to start in a new thread to save usage; this list is the starting point.

## Next (in order)
1. Parity sweep with the 100% Blue Knight save: shops, animal orbs, Insane Mode, Arena,
   mini-games, items (bombs, horn, boomerang, shovel), HUD, relics.
2. Endless ice magic in the final battle: not reproduced on the PC; waiting for details.
3. Build mix-up (2026-10-15): the pakoutput re-extraction (..\complete-files) is mostly the newer
   FRESH/Remastered build, not the original. Only 14 of its files are the original bytes plus the
   missing tail: CHICKEN EBAT ESLIME ETOWER MOUNT3 SKY4 TEST UFODUST LEVEL8/14/15/19/27/39. Those are
   used. The other 55 (list in ..\restored_original_build.txt, incl. MENU MAIN MAP SELECT PLAYER
   PAUSE) were put back to the original-build (truncated) copies from ..\cut-files-backup. FRESH
   files broke the title menu (different MENU layout), hitched character select and crashed the
   stat screen. Never mix builds: a file is usable only if its first bytes equal the original's.
   The cut-short repairs are active again for the truncated originals.
3b. Memory (2026-10-15): the stat screen crashes were the 160 MB newlib heap running out
   (dumps in ..\crashdumps; read with scratchpad core.py/mods.py: cc_vita's base from MODULE_INFO,
   PC - base + 0x81000000 for addr2line). Loaded SWFs were kept forever; now free_unused_movies
   (main.c) frees files no live clip/function uses once the heap is half full, and the sfx cache
   is 24 MB. Overlay shows heap use. hosttest CC_FREE_UNUSED=1 does the same (ASan-checked).
4. Remaining blank lobby text (online-only screens).
5. Performance (needs Vita overlay numbers), then Wi-Fi co-op.