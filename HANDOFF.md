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

## Done on 2026-10-15 (later)
Title + main menu (MENU.swf panel, 3 items, cc_menu_attach), "Vita Edition" badge
(cc_menu_vita_badge), prompt icons by action code (native f_GetButtonArt), map names and stat
numbers (console pins f_Goal, f_GuyInit, f_InitStatScreen, f_StatScreen), character select
levels, DLC characters 29-31 owned (cc_own_dlc_characters), general cut-short death-frame
repair (EWIZARD, enemies), heat shimmer (AddShader "Heat" -> render target wobble, Vita only),
no "Loading X.swf" text. 63 .pak files to re-extract are in
Desktop\Castle Mod Tool\castlecrashers-main\pak (pak_list.txt); outputs go to pakoutput.

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
5. Performance (needs Vita overlay numbers).
6. Multiplayer: finish what the 2026-10-20 netplay work left (see below).

## Multiplayer (2026-10-20, built in the cloud: no game files, no VitaSDK there)
Lockstep: every Vita runs the whole game; only buttons travel. Tick N runs once all ports'
buttons for N are in. A local press is used 3 ticks later (100 ms) to hide the round trip.
- `src/netplay.c/.h` – the protocol (no sockets): lobby, save data exchange, seed, inputs and
  frames with resends, timeouts, desync check (a hash every 30 ticks, compared by the host).
- `src/net_vita.c` – UDP over Wi-Fi (the host broadcasts a beacon; clients find it, or use
  join=<IP> from ux0:data/CastleCrashers/net.txt for internet play with UDP 47310 forwarded)
  and ad hoc (P2P socket, same code). Compile-checked against vita-headers only, not built
  with the real toolchain, not run on hardware. Ad hoc has no "join a network" call in
  vita-headers; it refuses unless sceNetCtlAdhocGetState says a link is up. Experimental.
- `src/cc_net.c` – game glue: every port's save data from its owner (cc_storage_pack/unpack),
  the host's seed into rand_state, vm->fixed_clock (getTimer and opcode 0x34 count ticks),
  per-port buttons into cc_keys, the hash (rand_state + actions run).
- `src/cc_host.c` – cc_port_keys[port][button] + cc_press. Saving in a network game writes
  this console's player as the file's port 0 and keeps the file's ports 1-3 as they were.
- `src/main.c` – Play menu after the title (this Vita / host / join, Wi-Fi or ad hoc), lobby
  screen, gated tick loop, a red line when the connection drops or the games go out of step.
  The pause menu is off in network games (it would freeze one console only).
- `tools/nettest.c` – 4 consoles over localhost UDP with packet loss: same ticks, same
  inputs, saves intact, a planted desync caught. Passes at 0-50% loss.
- hosttest: `CC_NET=host:PORT:CLIENTS` / `CC_NET=join:ADDR:PORT` (+ CC_NET_DELAY). CC_KEYS
  are that console's buttons. Two/three hosttests on a test SWF ran 120 ticks in step with
  identical traces. Build line: hosttest.c + swf render level player enemy as_core as_mc
  as_lib as_render cc_host cc_native cc_buttons hostfont strtab netplay cc_net net_posix
  + libtess2, -lm -lz.

To do (in order):
1. Ports 2-4 key codes: copy them from f_GetButtons (MAIN.swf) into cc_port_keys[1..3]
   (cc_host.c). Until then the other players' buttons press nothing.
2. Build the VPK with VitaSDK; fix anything the real toolchain finds.
3. Two hosttests on the real MAIN.swf with CC_NET, then two Vitas on Wi-Fi: menu, character
   select joins, a level, the map, saving. Any "out of step" message: find what differs
   (the hash is coarse: add positions/levels to cc_net_after_tick to narrow it down).
4. A network pause/quit (send it as a button so every console acts on the same tick).
5. Ad hoc on hardware; then local multiplayer on one console with extra pads (PS TV,
   ds34vita: sceCtrlPeekBufferPositiveExt2 ports 1-4 -> cc_press(port, ...)).