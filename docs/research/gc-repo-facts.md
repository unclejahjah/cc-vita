# Facts from myuu-151's Castle-Crashers-Recomp / Castle-Crashers-GC (no licence: facts only, own words)

Read from a shallow clone in /tmp/claude-0/ccref (outside the project). No code copied, no tools run.
R = Castle-Crashers-Recomp, G = Castle-Crashers-GC. Line numbers are from the clone of 2026-10-20.
Neither repo mentions magic, bosses, arena arrows, orbs, ship or knock-down by name. The per-bug notes below
are INFERENCES from general VM/engine facts, marked as such.

## A. castle.exe's script VM is NOT Flash AVM1 (most likely cause of many bugs)
Source: R engine/as/interpreter.cpp, value.cpp, objects.cpp.
- Numbers are 32-bit float or int32, never double. A pushed double gets narrowed to float (interpreter.cpp ~246).
- `undefined` used in arithmetic counts as 0, not NaN: Subtract/Multiply/Divide/Less/Greater/And/Or treat undefined
  operands as 0 (interpreter.cpp ~596-655). Add2: undefined+undefined gives int 0, x+undefined gives x, undefined+x gives x (~393-410).
  Increment of undefined gives 1 (Decrement gives -1).
- Divide by 0 gives undefined, not Infinity. Modulo is integer modulo (both sides truncated), and x%0 gives undefined.
- Add2 of int+int is int with wraparound. Int+float gives int when the result is a whole number.
- ToInteger/ToNumber both truncate to int (undefined gives 0). Float to int truncates like SSE: NaN or out of range gives INT_MIN (value.cpp ~13, 155-163).
- Strings, Object, Array convert to bool as FALSE (value.cpp ~180-190). NaN is true.
- Equals: undefined==undefined only. Values of different counted types are never equal. Floats compare equal within 1e-5.
  The comparison is not symmetric because it switches on the left operand's type (interpreter.cpp ~136-155).
- BitRShift is an unsigned shift (acts like >>>).
- Math: floor/ceil/round return int. round truncates for negatives (-1.5 gives -1). min/max return the SECOND argument on a tie or NaN.
  pow uses an integer exponent loop and gives 1 for n<=0. random uses an MT19937 variant (rng.cpp). abs keeps -0.
- Array methods: only `splice` (2 and 3 argument forms, with odd semantics) and `length` work. push/pop/etc do NOTHING (objects.cpp ~80).
- There is only one register: StoreRegister/push-register ignore the register number (interpreter.cpp ~232).
- A null push leaves the slot unchanged. CallFunction/CallMethod take at most 12 args.
- DefineFunction: if a function with that name already exists on this runner, the first one is kept (the later definition is skipped).
  Functions come from a pool of 1000 (interpreter.cpp ~893-910).
- CallMethod: if the calling clip was removed or reset during the call, the rest of that script is ABORTED (~975).
- Fused opcodes (R tools/normalize_swf.py docstring): 0xA0-0xA4 are Push fused with the following GetVariable/GetMember/SetMember/DefineLocal.
  0x70 is ToBoolean fused with the following Not, and the pair leaves the boolean (NOT negated). In R the handler for 0x70 does the
  conversion and skips the next action (interpreter.cpp ~113, ~426-431).
INFERENCE for bug 1 (999 damage, magic stat maxing, all attacks at max damage): this is the classic symptom of Flash semantics where
castle.exe semantics are expected. With Flash rules, undefined becomes NaN. NaN then reaches a min/max clamp, and castle.exe's
"second argument on NaN" rule (or a Flash clamp) returns the cap: stat 25, damage 999. Check Math.min/max and undefined arithmetic first.
Also check that 0x70 is not treated as a plain Not.

## B. Movie clip / display semantics (R engine/player/natives.cpp, movieclip.cpp)
- `_alpha` reads 0-255 (the colour multiplier) but is SET as a percentage x2.55 (movieclip.cpp ~257, ~297-304).
- Supported properties: only _x _y _xscale _yscale _currentframe _alpha _visible _width _height(setter only for height) _rotation _name this _parent.
  _rotation is quantised to 1/128 of a degree.
- gotoAndStop/Play: frames past the end clamp to the last frame; gotoAndStop(undefined or no args) goes to frame 1; an unknown label does nothing (natives.cpp ~293-307).
- hitTest(x,y,true) (shape flag) always returns FALSE; hitTest(x,y,false) and hitTest(clip) are bounding-box tests (~328-344).
- removeMovieClip works only on depth >= 0, and also accepts a target argument. duplicateMovieClip returns nothing and is skipped if the depth is taken.
  swapDepths(number) swaps with any occupant.
- unloadMovie/start: no-ops. setMask hides the mask clip itself.
- Clip events: only onClipEvent(load) and onClipEvent(enterFrame) are run; everything else is ignored (movieclip.cpp ~588-628).
  DoInitAction and sounds are ignored.
- Frame loop order: the next sibling is read after the current one runs, so children inserted during the pass also run. A child that removes itself
  ends the pass. Once a goto happens in the pass, the remaining children run only if they were placed on that frame (~689-712).
- A PlaceObject "move" of the same character updates the clip in place: a MovieClip loses its colour transform and its name.
  A clip moved by script keeps its matrix only if the tag names it with the same name (~497-570).
- A loaded movie (loadMovie) uses the host's _root and runs in the host's context (player.cpp ~373).
- Clip pool: 10500 clips. attachMovie logs "no such export" / "no clip free" when it fails.

## C. Natives castle.exe provides (R natives.cpp ~24-116, ~346-690)
Engine ids 0x2d-0x44 and 0x83-0xb9 (AddShader, FadeIn/Out, music, Read/WriteStorage, ChangeMovie, QuitTo, Get/SetFlashGlobal,
SetFlashController, GetGameMode, ReadSaveGame/WriteSaveGame, SetScores, Show/HideLoadIcon, ReturnToArcade, ...).
- Constant answers: GetWidescreen true, IsPalMode false, GetLanguage 0, GetRegion 0, GetGore 1, GetTextLocalizationScale 100, GetCheat false,
  IsFullGame true, IsNetGame/IsRankedMatch false.
- GetGameMode: 0 arcade, 1 quaff, 3 arena (game.h ~89). If no game has started, it returns the last mode.
- Game natives that OVERRIDE functions the scripts define themselves ("before the scripts' own functions"):
  f_BSPCheckLastHitIndex/Type/Slope, f_BSPHitTest, f_BSPLoadLevel, f_GetClosestWaypoint, f_GetWPHit/X/Y, f_SetWPHit,
  f_LocalToGame (0x99), f_Quantize (0x9c, rounds to 1/256), f_Depth (0xeb), f_LargeObjectRanges (0xec).
- f_Depth(zone, u_depth): looks up `abs_bottom` under _root.loader.game.game. It sets zone.current_depth = (2000 - int(abs_bottom._y - int(u_depth)))*1000 + zone.depth_mod,
  then does swapDepths to that value. If abs_bottom is missing, it does nothing (natives.cpp ~637-670).
- f_LargeObjectRanges(zone): reads zone.zone (a box clip). It writes x, y, w, h (half sizes, using |scale|*0.01) and top/bottom/left/right floats into the box.
  If xscale <= 0, x is mirrored (x - box._x) (~596-635).
- Flash globals the engine reads/writes: g_bNoPause, g_bLoading, g_bReadyToLoad, g_bNoLoadingScreen, g_nPDLC, g_nPort1State..4, g_nPort1..4, g_bLoadIcon;
  plus the level's `quit` clip and its `quitto` variable (game.cpp ~19-30).
INFERENCE bug 4 (ship below the waves) and part of bug 2: if our port runs the scripts' own f_Depth/f_LargeObjectRanges instead of these natives,
or if abs_bottom is not found, depth sorting and large-object hit boxes break. Large objects means bosses, the ship, and boss hit zones.
INFERENCE bug 5 (knock-down): hitTest shape-flag returning false, and f_BSPHitTest answering only when all 4 args are defined, are worth checking.

## D. Save layout -> bug 7 (character select wrong levels), and the stats part of bug 1 (R save/storage.cpp, menu/main_menu.cpp ~373-420)
- Save is 0x60c bytes. Character records start at 0x40, 30 records of 0x30 bytes.
  Offsets: +0 flags (0x80 unlocked), +1 LEVEL MINUS 1, +2 experience as 4-byte big-endian, +6 weapon, +7 animal,
  +8..+11 strength/defense/magic/agility, +12..14 level unlocks, +15 potions, +16 bombs, +17 sandwiches.
- A fresh save sets every stat to 1 (not 0), puts the default weapon at +6, unlocks the first 4 knights, and sets settings bytes 0..4 = 1,8,9,9,1.
- sanitize: a level byte > 0x62 is clamped to 0x62; a stat > 25 is reset to 1.
- Character-select book shows level = byte+1. The XP bar uses top = (L+18)*L*10.
  Health shown = (L+23)*3 + defense*28. Level 99 start XP = 190*(L-1) + 10*(L-1)*(L-2).
- ReadStorage(port[,pos]) returns 0 for a negative port or a failed read. The read cursor is SHARED and advances.
  WriteStorage takes (port,value) or (port,pos,value).
INFERENCE: wrong levels on select means an off-by-one (level-1 stored) or a cursor not advanced or shared. A stat of 0 or garbage could feed the NaN/clamp path in A.

## E. Engine-level gotchas that caused "level never finishes / things missing"
- G docs/hardware-bugs.md ~85-110: the function pool of 1000 filled because clearing a clip did not release its functions. Later levels then got only f_Init:
  no players, portals or effects, a blank map, and black screens. Their engine logs "pool of 1000 functions is full".
  INFERENCE: a strong candidate for bosses not dying, cutscenes never ending, and missing orbs. Missing functions make scripts silently do nothing.
  This fits if the bugs appear later in a session.
- R player.cpp ~155-200: when a movie unloads, a clip still referenced from main (e.g. the hud's player_pt) must stay emptied and not be reused at once.
  Reusing it broke a second visit to a level (players, shadows and bottom edge lost).
- G triage.md: signs a bug is in the engine include `attachMovie(...): no such export` and `loadMovie ...: dropped` (a black screen). Relevant to the Cyclops cave black screen (inference).
- Loading flags: the loading screen hides only when g_bReadyToLoad==1 or g_bNoLoadingScreen!=0. g_bLoading is cleared after 3 updates (game.cpp ~150-182).
  INFERENCE: a wrong flag here can leave a black overlay while gameplay continues (Cyclops cave).

## F. Renderer / memory / testing (G docs)
- Masks without stencil: masks are drawn into depth. Masked content is drawn half a level farther with a GEQUAL test, because equal-depth tests failed from rounding on hardware (hardware-bugs.md, last section).
- Matrix: castle.exe divides raw matrix values by 65535 (not 65536), and applies only the terms present (movieclip.cpp ~86). Its own matrix units: a,d x100 and b,c x20.
- SWFs have "holes" (cut-out bitmap/shape data); tag lengths get patched (R swf/movie.cpp ~175).
  To save memory, bitmaps are compacted in place, not into a second buffer (the 1.42 MB level failed otherwise, ~273-290).
- Memory (G docs/memory-roadmap.md, streamlining.md): 24 MB console, 3-5 MB free in big levels, a shape cache kept in ARAM, shapes pre-built behind the loading screen (shapes-made-ahead.md),
  shape tessellation tolerance 6 twips. Catch out-of-memory in shape building, or a failing shape is retried every frame.
- Testing: replay recorded sessions and dump per tick, then compare against the original game (G triage.md, hardware-testing.md; R README `--replay`, `--dump`).
  Testing switches: CASTLE_LEVEL, CASTLE_PORTAL, CASTLE_WARP, CASTLE_MAX.

## Not found
No specific notes on: arena arrows (bug 3), animal orb spawning (6), UFO/corn/wizard/barbarian bosses, or Cyclops cutscenes.
Best guesses: A (VM semantics), C (natives), E (function pool).
