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
- `src/pause_menu.c, help_book.c, audio.c, trophies.c` (trophies opt-in, -DCC_TROPHIES=ON, on hold).
- `tools/` – hosttest.c, livearea/, extract_strings.py (strings_en.txt), extract_help.py,
  make_save100.py, make_trp.py. 2026-10-22 clean-up removed the old hand-written viewer
  (viewer/level/player/enemy.c), prototypes/ and the one-off tools/scripts.
- `tools/hosttest.c` – PC harness (`--as GAME_DIR MAIN.swf out frames every`). Env: CC_KEYS,
  CC_CALL (frame:fn:args, 'str #num path; a.b.method is called on a.b), CC_SET
  (frame:path.name=num), AS_ARGS (print calls of named script functions), AS_PRINT, AS_TRACE_EVERY,
  AS_GOTO, AS_CALLS, CC_STRINGS, CC_FONT_DIR, CC_PS_BUTTONS, CC_REPAIR_DEBUG.

## Build / device
VitaSDK in WSL; `cmake .. && make` in build/. Vita: ux0:data/CastleCrashers/{game,levels,fonts,sfx,
music}/, strings_en.txt, strings_vita_en.txt, strings_help_en.txt (Help & Options' words:
`tools/extract_help.py --exe castle.exe --out strings_help_en.txt`), settings.txt (volumes), save.dat. VPK to ux0:downloads via VitaShell FTP.
LiveArea: `tools/livearea/make_livearea.sh GAME_DIR FONTS_DIR OUT` renders the art from the game
files (title scene, logo + Vita Edition badge, The Behemoth bird, Orange Knight icon; layout and
template.xml copied from Jah's Punch Quest port), then `cmake -DLIVEAREA_DIR=OUT`. PNGs never in git.

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

## Done in the cloud (2026-10-20 afternoon; game data in the private project folder gamedata/cc-cloud.zip)
- Cyclops' Cave (LEVEL37) black screen: a full-screen radial vignette (shape 165, transparent
  centre, black edge) is one rectangle; gradient colours were only taken at vertices. Gradient
  triangles are now subdivided (render.c gradient_tri). LEVEL4 63k -> 87k triangles.
- Pirate Ship (LEVEL40) sea over the ship: cc_repair_level added bg1_c/bg3_c on top of the game.
  LEVEL40 is whole and names its layers b0-b3. Layer repair now only for truncated files.
- Vita Edition badge: the Steam lettering is white loops in MENU shape 601 (dropped), no box.
- Achievements: UnlockAchievement(port, id 1-12) -> achievements.dat, banner, pause menu page.
- Checked OK in hosttest: LEVEL36 boss_intro cutscene ends (f_SpawnBoss); regression of all
  levels with the 55 complete files (only new warning: LEVEL60 fp_HorseRide undefined, also on
  LEVEL18 p1.body; the FRESH levels call it, original MAIN lacks it: mounts may need a stub).
- Owlet (LEVEL18, animal 2 at animal_pt 3796,-230): collect needs |dx|<50, |dy|<20; the walkable
  strip behind the bush reaches y -244, so it is reachable. Shipping PDAG BSP (bsps/BSPn.PDAG:
  "GADP", 3 zero ints, a_BSP length, then a_BSP as float32 from offset 20, waypoints after) has
  the same walls there as the edge-clip BSP.
- Level map: 18 Forest Entrance, 4 Thieves' Forest, 36 Undead Cyclops, 37 Cyclops' Cave, 40 Pirate
  Ship, 24/46-49 arenas. Arena mode needs GetGameMode()==3 (always 0 here).
- Cloud harness: start MAIN.swf, CC_KEYS="150:83,300:2,450:49,600:2,750:83" reaches LEVEL2 on a
  fresh save; CC_CALL "900:f_ChangeLevel:'../levelN/levelN.swf" jumps to any level.

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
### Arena arrows + bombs not knocking down (Jah, 2026-10-20 16:00)
Both are the same developer-settings path as the magic bug, already fixed by pinning `console_version` in level scripts (20867b8):
- LEVEL24 (King's Arena) `f_Init` with `console_version` false forces `arena_mode = 1` (PvP arena setup: arrow weapon, PvP boundaries). With the pin, hosttest shows `arena_mode` 0, sword melee, player grounded.
- Knock-down in `f_Damage` needs `round(resists[type]/25) < 4`; player resist = 40 + defense/2, so the dev stats (9999) blocked every knock-down. A fresh character (resist 40.5) is juggled and knocked down in hosttest (`CC_CALL 1500:f_Damage:loader.game.game.p1|#4|#4|#1|#10|#-30`).

### Vita trophies (NoTrpDrm), 2026-10-20
- `src/trophies.c`: sceNpTrophy with Rinnegatamante's NoTrpDrm plugin (`*main` → `ur0:tai/NoTrpDrm.suprx` or `ux0:tai/`). Comm id `CCRV00001_00` (param.sfo NP_COMMUNICATION_ID + `sce_sys/trophy/CCRV00001_00/TROPHY.TRP`). Each newly earned in-game achievement unlocks trophy id-1 (also syncs ones earned before). Without the plugin the context fails and only the in-game list is used.
- ON HOLD (Jah 18:44): every build with the trophy code failed to start on the Vita ("Could not prepare the application to start"), even without the pack; default build has none (`-DCC_TROPHIES=ON` to try again). Suspects: the SceNpTrophy import itself, or the pack format.
- `tools/make_trp.py` builds the unsigned pack at build time (TRP v2 header, dev flag 1, plain XML SFMs, drawn placeholder icons; `-DTROPHY_ICONS=dir` for real ICON0.PNG / TROPnnn.PNG, never committed). The TRP layout is from the PS3 format and untested on hardware: if the setup dialog errors, try dev flag 0 or a pack from withLogic's Vita TRP Builder.

### Crash dump, loading screen, pause menu (2026-10-20 evening)
- Crash dump (19:06) = out of memory decoding sky2.swf (two 2048x2048 bitmaps): each bitmap is now a texture as soon as it's decoded (`swf_bitmap_loaded`), 32-bit ones are converted in place, a failed allocation drops the bitmap, and a load past half the heap frees unused files first. Decode dumps: gunzip the .psp2dmp, xyzz/vita-parse-core (py3-patched) with the ELF of the same commit.
- Loading screen: LOADING.swf runs over the game at depth 1048500 (`cc_loading_attach`); level loads are held (`vm->hold_load`) until it sets `g_bReadyToLoad`.
- Pause menu = castle.exe's PauseMenu (2012 build, per myuu-151's recomp: facts only) in `src/pause_menu.c`: PAUSE.swf frame 2 backdrop, game font at castle.exe's sizes (the shipped font is drawn at 60 px, the exe's sizes are for 20), lines 30 px apart, pulsing yellow pick, 0.2 s flash/fade, Abutton/Bbutton prompts. Pages: Paused (Resume Game, Help & Options, Information, Exit To Map, Exit to Main Menu), Information (Achievements), Are You Sure? (No/Yes). Words copied from Jah's castle.exe (the numbered table there isn't readable: .text is SteamStub-packed). Help & Options and Achievements still open our own book pages; Weapon/Animal Statistics books not done. Exit to Main Menu frees the game VM and its files and goes back to the title. `hosttest --pause PAUSE.swf out [backdrop.ppm]` renders the pages.
- Renderer bug fixed: `recolour` skipped the first vertex colour when it was opaque white (0xffffffff), so white shapes under a colour transform drew transparent black (PAUSE.swf's veil, and any white shape tinted by a script).

## Open bugs from Jah's Vita test of cc_vita_pause.vpk (2026-10-20 19:46), findings so far
- Black screen for a few seconds before character select. Before the render.c recolour fix
  (116b763) it showed MAIN.swf's blue background colour (51,153,255) instead. In hosttest
  (`--as MAIN.swf`) frames 0-100 are black, then character select shows at ~110. Hiding
  `fader`, `cinema_fader` or `loader.game` (AS_HIDE) at frame 50 does not change the frame, so
  what draws the black is not yet found. Next: find the clip, then check whether the original
  shows that long a black (likely MAIN's own start-up fade, which the PC plays behind its logos).
- Pause takes ~3 s to open: PAUSE.swf is loaded, tessellated and uploaded on the first Start
  (host_movie in pause_draw). Fix: load it (and pause_build_art) at game start. NOTE: the Vita
  has the ORIGINAL-build PAUSE.swf (245,204 B, from ..\cut-files-backup), the cloud zip has the
  FRESH one (482 KB): check the pause menu backdrop (root frame 2) with the original file.
- Animal orbs "missing everywhere"; Jah's example: Thieves' Forest (LEVEL4), the one in the
  water. The scripts place it: animal2, type 21, at animal_pt (5454,-894), state 150, frame
  4/24 ("lost"), drawn at the head of the river in hosttest, also with the Vita's own
  original-build LEVEL4.swf and ANIMALS.swf and with CC_PS_BUTTONS and CC_FREE_UNUSED on. The
  "orb" is the animal itself floating (body_y -35); ANIMALS.swf has no other orb art. Not yet
  reproduced: the Vita must differ (save data: a player whose pet is that animal suppresses it;
  or something Vita-only). Next: write warnings and load errors to a log file on the Vita
  (vita_warn only keeps the last one) and have Jah visit the spot, then FTP the log.
- Character select shows wrong player levels (still open, not investigated this round).
- Files on the Vita: the 55 in ..\restored_original_build.txt are the original-build (cut)
  copies from ..\cut-files-backup, then the 20 steam-complete files over them. The cloud zip's
  game-files has the FRESH copies of many of those: tests in the cloud must swap in the
  cut-files-backup copies (hosttest: build a game dir of symlinks with them copied over).
- hosttest: CC_VIEW="x,y" scrolls loader.game.game so level point x,y+240 is at screen
  (424,240)-ish: screen = 424 + x - vx, 480 + y + vy... e.g. CC_VIEW="5454,-654" centres
  (5454,-894).

### Fixes for Jah's 19:46 test (2026-10-20, cloud)
- Black screen before character select: LOADING.swf never reads g_bLoading; the executable plays
  its `loadingScreen` clip (frame 20 "loading" sets g_bReadyToLoad, "exit" ends it). Nothing played
  it, so every load waited out the 90-frame timeout on a black screen. Now `loading_play`
  (cc_host.c) plays it and sends it to "exit" once the file is in: the knight-and-gems screen
  shows for ~1 s. GetLocalSecond (its hint picker) added.
- Character select levels all 1: the uncut LOBBY.swf calls `_root.f_SafePlayerType`, which the
  original MAIN.swf lacks; a native one (identity for 1-31) is put on _root.
- Pause delay: PAUSE.swf and the pause book art load at game start (main.c).
- Animal orbs: still not reproduced (hosttest draws LEVEL4's Seahorse at the river head). The Vita
  now writes ux0:data/CastleCrashers/log.txt (previous run: log_prev.txt): each distinct warning
  once, files loaded/freed with heap use, and the level's animal* clips whenever they change.
  Next: Jah walks to the Seahorse spot, then FTP the log back.
- Background loads (20:55): files are parsed and tessellated on a "cc_load" thread (main.c
  vita_prefetch / bg_thread) while hold_load holds their loadMovie, so the game and the loading
  screen keep animating; upload stays on the main thread. The loading screen's exit waits until no
  load is pending for 2 frames; HideLoadingScreen hides the clip. The "Loading X.swf" text with the
  overlay on is gone. hosttest CC_PREFETCH=N fakes an N-frame background load.
- ANIMALS.swf was simply missing from the Vita's game folder (log.txt showed MISSING); uploaded
  20:45 (complete 64,676-byte copy). Orbs show now.

### Arena walls and blocks (2026-10-21, thread "Treasure animation and arena boundaries")
- Arenas (LEVEL24, 46-49) have four block layouts: the level's game clip shows layout k on frame
  2k+1, its edge clips on frame 2k+2, and asks for `bspNa`..`bspNd` (bsps/BSP24A..D.PDAG). Our
  f_BSPLoadLevel always did normal levels' "edges on frame 2, then frame 3", so the arena showed
  layout b's stone blocks over layout a's walls: players walked through blocks and out of the
  arena. Now a name ending in a-d builds from the next frame's edges and goes back to the layout
  (King's Arena story layout = the two giant cats). Checked in hosttest: p1 stops at BSP24A's
  corner (-905,-274) and bottom edge (y -68); LEVEL46/48 likewise.
- Barbarian Boss chest (LEVEL7 bosschest, sprite 227 -> 226): its gold fountain (sprite 222
  "gold") is masked by an invisObject whose only shape is a tag 148 box. Boxes drew nothing, so
  the mask was empty and the fountain invisible. As a mask a box now fills its extent (render.c
  box_quad). Test: CC_CALL "1000:loader.game.game.bosschest.gotoAndStop:'init" then three
  "f_HitGeneral:loader.game.game.bosschest", CC_VIEW="688,300".
- hosttest: CC_SNAP_FROM=frame skips earlier snapshots; AS_DUMP prints array items too.

### Sand Castle Interior stuck after the first fight (2026-10-21, thread "Sand Castle interior progress block")
- LEVEL42 = Sand Castle Interior (43 = Roof). Waypoint 2 spawns 2 Saracens (kills_goal 2) with
  fp_SpecialEvent = f_Cinema, which spawns 6 more (goal 6); then the leash opens to the exit.
- An enemy's death frame (PLAYER.swf) runs `_root.f_EnemyDie(_parent._parent)` then
  `_root.f_RemovePlayer(_parent._parent)`. f_EnemyDie frees the enemy (active false, "blank") and,
  on the last kill, runs f_Cinema, whose spawns reuse that freed e_humanN. Then f_RemovePlayer
  switched the new enemy off: one of the 6 never appeared, kills stopped at 5/6.
- In Flash the death frame's body clip is gone after "blank", so its `_parent` is undefined and
  f_RemovePlayer gets nothing. Now a removed clip's `_parent` (property and preloaded register) is
  undefined. Other levels (4, 13, 18, 29...) play the same in hosttest; only more harmless
  "gotoAndStop on undefined (in f_RemovePlayer)" warnings.
- hosttest: CC_AUTOKILL="from[:every]" has p1 kill every active e_humanN (f_Damage 9999) every N
  frames; with CC_KEYS holding right (`1100:39:8000`) it plays a level's waves through.
  CC_ENEMY_LOG=1 prints each enemy's active/alive/dead changes with kills/kills_goal.

### Flooded Temple white boxes (2026-10-21, thread "Flooded Temple white box and splashes")
- LEVEL32 = Flooded Temple (58 = its entrance). Every player and fish showed a big white box:
  their `waterbox` clip (PLAYER.swf, EFISH.swf) runs `_parent.setMask(this)` in level 32 so the
  body is cut off at the water line, and `setMask(null)` out of the water. setMask was a no-op,
  so the mask drew as plain white shapes. Now setMask works (as_lib.c mc_setmask, AsMc.mask /
  is_mask, as_render.c): the mask clip is not drawn itself and its clip is drawn through it via
  the stencil, wherever the mask sits in the tree. The temple_splash fx (f_Jumping landing,
  f_FX) were drawn under the same boxes; they show fine now.

## UI text fixes (2026-10-22)
- "Saving..." (castle.exe string 596) shows small at the bottom for 2 s after each WriteSaveGame
  (pause_menu_saving; hosttest CC_SAVING=1 draws it).
- SetTextNumeric shows whole numbers (Giraffey's +10% exp left fractions on the stat screen).
- The Vet's chalkboard (LEVEL16 f_Vet) shows ntext 180 + 3 * animal_type .. +2 (Golden Whale, type
  29: 619). Those numbers were missing from strings_en.txt (blank board); added on Jah's PC
  (strings/strings_en.txt; 266 was wrongly "We're sneaky like that.", a FRESH string).
- Enemy health: f_SetEnemyHealth (x1/1.2/1.4/1.6 by players, x10 Insane) runs as is; checked
  active_players 1, insane_mode/ez_enemies false, a Barbarian gets 50.

### Wizard crystals smashed early; ending crash log (2026-10-22, thread "Final cutscene crash")
- LEVEL50's crystals (gold1..4, f_SpawnGem) have unnamed clips whose scripts run
  `_parent.gotoAndStop("punch1_N"); _root.loader.f_CheckShardShatter(_parent)`. The goto removes the
  clip running the script, and since 615afba a removed clip's `_parent` was undefined, so
  f_CheckShardShatter got undefined, read health undefined (not > 0) and played the smash (sound,
  shrapnel) while the crystal stayed. Now (`as_mc_parent`) a removed clip keeps its parent until that
  parent goes to another frame or is removed itself: Sand Castle Interior still gets undefined (its
  enemy is sent on by the respawn), the crystals get theirs. hosttest: LEVEL42 waves pass, LEVEL50 no
  false shatters, `CC_SET 2000:loader.game.game.gold1.health=1` + `CC_CALL 2010:loader.f_Hit1:...gold1|#10|#1|#0|#0|#0` shatters it once.
- Ending crash (Blue Knight, Triangle in ENDING.swf): not reproduced in the cloud or on a retry. The log
  now goes to the card at once on loads, frees and warnings and every 2 s (with a heartbeat every 10 s);
  (The SELECT+DOWN jump to the ending was taken out in the 2026-10-22 clean-up.) The save is written only on reaching the map
  (f_FlushSaveData from MAP.swf), so a crash in the ending loses the Wizard win (Jah: keep it so).
- Lag on Jah's Vita (2026-10-22): the log's flush every 2 s stalled the game (~370 ms while the card was busy).
  The log is now written by a thread of its own (log_thread; log_flush hands the buffer over,
  log_flush_wait before quitting). The animal-orb log lines are gone.
- Preloading (2026-10-22): main menu -> character select (lobby.swf, ~7 s) and character select ->
  map (fx/player/animals in one 15 s frame: they're loaded straight, not through loadMovie's
  prefetch) stood still on the Vita. preload_step reads, in stages: MENU during the intro; LOADING,
  PAUSE, sounds, lobby on the title; fx, player,
  animals, map on the loading thread whenever it's idle and the heap is under 60%; such files keep
  keep=2 (not freed) until the game first asks for them. Tessellators are one per thread
  (render_thread_slot).

## DLC weapons and Golden Whale (2026-10-22)
- The packs' items are owned in every profile (cc_own_dlc_items, next to cc_own_dlc_characters):
  weapons 63 71 75 76 77 (Pink Knight Pack), 72 78 79 85 (Blacksmith), 84 (Hatty's Gem Sword),
  74 80 83 (PS3 Can't Stop Crying), animal 29 (Golden Whale). Unlock number = 50 + weapon:
  under 128 a bit of slots 12-27, else slots 28-35. Pelter/Dragonhead and the King/Necromancer
  weapons (64-66) are the PC Insane Store's (LEVEL39), not DLC. Checked in hosttest: LEVEL15's
  rack (f_InitWeapons) shows the DLC weapons unlocked, 2x4/Wooden Sword/store items still locked.

## Wi-Fi co-op (2026-10-22, branch claude/project-thread-pi6xr1)
A separate app: `cmake -DCC_NET=ON` builds "Castle Crashers Co-op" (title ID CCRV00003) next to the
main one. Same ux0:data/CastleCrashers files; its own save, save_coop.dat (copied from save.dat on
its first start). Main menu: the original's MULTIPLAYER button (panel depth 11) between Local Game
and Help & Options; Host a game / Join a game (hosts found by UDP broadcast on the same Wi-Fi).
- Lockstep (`src/net.c`, `src/netplay.c`): every Vita runs the whole game; each frame's buttons of
  every port go host -> everyone over UDP (TCP 47710 lobby, UDP 47711 frames, 47712 beacons).
  Buttons are read NET_DELAY (4) frames before they're used. Host = port 1, joiners 2-4. Players
  join at character select with Cross (f_CheckJoin reads each port's jump key: 83, 17, 98, 110).
- What had to be made the same on every Vita: random seed (host's), getTimer/0x34 and
  GetLocalSecond (from the frame count), the "instanceN" counter (per VM), cc_host statics reset
  per game, and how long a level file takes (netplay_prefetch: the load waits until every Vita
  says it has the file, `loads` in each input). Each input also carries a 16-bit check of the
  game state; a difference shows "Out of step with the other Vitas" and goes to the log.
- Pause doesn't stop the game in a Wi-Fi game; the pause menu's port state (2/1/3) goes out with
  the buttons. A Vita that leaves or is silent 45 s is dropped (g_nPort<n>State 0).
- Each Vita saves only its own player (cc_storage_save_as: port n -> its save's port 0) and
  counts only its own achievements; leaving reloads its save.
- PC test (hosttest): NET_MODE=host|join, NET_PORT, NET_WAIT, NET_ADDR, CC_PAD="frame:CCB bit[:hold]",
  NET_JITTER, CC_PREFETCH, NET_LOG, NET_TEST_DESYNC. E.g. host
  CC_PAD="150:14,300:1,450:0,600:1,750:14" and join CC_PAD="230:14,330:1" (P2 joins as Red).
  Checked: 2 and 3 copies stay in step through LEVEL4 fights with random lag, slow loads and
  file freeing on one side; a forced difference is caught; a leaver is dropped.
- Internet play (no port forwarding): hosting also starts `src/upnp.c` on a thread: SSDP M-SEARCH
  (multicast and the default route), the router's description, then AddPortMapping (TCP 47710,
  UDP 47711, lease 0, deleted again on leaving) and GetExternalIPAddress. The host's lobby shows
  "Game code XXXX-XXX" (net_code: the internet address + 3 check bits in 7 base-32 letters);
  "Join by game code" takes it in with the D-pad. A shared provider address (CGNAT, 100.64/10 or
  private) says "Wi-Fi only" with the reason. Joining connects on a thread (NET_CONNECTING).
  The delay is per game (net.delay, sent in START): 4 + the furthest joiner's lobby ping in
  frames (MSG_PING/PONG every 250 ms), at most 15. NET_VERSION 2. hosttest: NET_CODE, NET_DELAY,
  UPNP_ROUTER (and a pretend router was used to test it: SSDP + SOAP in Python).
