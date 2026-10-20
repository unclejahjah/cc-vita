# Bug: most prompts show the Xbox left trigger (Jah, 2026-10-15 03:36)

## RESULT (2026-10-15, checked on Jah's PC SWFs, read-only)
**Cause: event order when a clip is placed.** Every prompt icon is an
8-frame sprite: 1 Square-slot (Xbox X), 2 Triangle-slot (Y), 3 Cross-slot (A),
4 Circle-slot (B), 5 Ltrigger, 6 Rtrigger, 7 stick, 8 extra. Its frame-1 script
(FX.swf 660/684, PLAYER.swf 312; in MENU/PAUSE/LOBBY a load+enterFrame clip
action on buttonicon's child) runs
`_root.f_GetButtonArt(this, _parent.u_temp)`, and main.swf's f_GetButtonArt does
`switch(u_temp) case 1..4: gotoAndStop(n); default: gotoAndStop(5)`.
The PARENT sets u_temp in its own `onClipEvent(load) { u_temp = 4; }`
(e.g. PLAYER.swf sprite 316 places 314 "body" with load flag 0x1 ->
`u_temp = 4`; MENU 18, PAUSE 11, LOBBY 5, FX 1 such placements).

In the engine (cc-vita/src/as_mc.c, timeline placement ~line 204 and
duplicate ~line 389) `as_mc_goto(vm, mc, 0, 1)` runs first, which queues the
CHILDREN's frame-1 scripts, and only then `enqueue(mc, Q_EVENT, EV_LOAD)`.
So the icon reads `_parent.u_temp` while it is still undefined, hits
`default`, and stops on frame 5 = left trigger. In-game icons only run that
script once, so they stay wrong; menu ones re-run on enterFrame and recover.
Flash (and the PC game) run the parent's load event before its children's
frame-1 actions.

**Fix:** make a newly placed clip's EV_LOAD run before its descendants'
frame-1 scripts, e.g. note `int mark = qn;` before `as_mc_goto`, then insert
the EV_LOAD item at `mark` (after the clip's own Q_FRAME item if it is first,
to keep its own order) instead of appending. Apply in both places that
enqueue EV_LOAD. strict_equals is fine (u_temp and case values are both ints).

**Art:** frames 1-4 place the raw shapes shared with Xbutton/Ybutton/Abutton/
Bbutton, so cc_buttons' style A restyle reaches them. Frames 7 (stick) and
8 are not restyled yet. Mapping on Vita: 1 Square, 2 Triangle, 3 Cross,
4 Circle, 5 L, 6 R.

## Earlier hypotheses (superseded)

Jah reports that on the current Vita build, most input prompts show the Xbox
left-trigger art instead of the right PlayStation button.

This note was written in the cloud while the PC was off. The engine code and
SWFs are only on the PC, so these are ranked hypotheses with a check for
each. The PC session owns the fix.

## Why "left trigger" points at one cause
If every prompt shows the same wrong icon, the icon clip is most likely one
multi-frame clip (one frame per button), and the frame never changes from
frame 1. Frame 1 is then the left trigger, and it stayed Xbox because the
option A art only replaced the six menu clips (Abutton, Bbutton, Xbutton,
Ybutton, Ltrigger, Rtrigger). Things that leave a clip on frame 1:

1. **The frame comes from a castle.exe native or global the engine stubs
   (most likely).** For example, the script asks the exe which frame or label
   shows "the jump button", gets `undefined`, and `gotoAndStop(undefined)`
   lands on frame 1. Check by grepping the Source Code dumps for
   `gotoAndStop(` on icon or button clips and tracing where the argument comes
   from. Fix: implement that native with the Vita mapping: jump/accept
   Cross, light Square, heavy Triangle, use item Circle, block L, magic R,
   pause Start.
2. **Frame label case.** SWF6-era ActionScript matches frame labels without
   case, so `gotoAndStop("a")` finds a label "A". If the engine compares
   labels case-sensitively, the call fails and the clip stays on frame 1.
   Check whether the label lookup is case-sensitive, and make it
   case-insensitive for SWF versions 6 and below (that's Flash's own rule).
3. **Numeric strings.** In AS1/AS2, `gotoAndStop("3")` means frame 3. If the
   engine treats every string as a label, it fails and stays on frame 1. Fix:
   a string that parses as an integer is a frame number.
4. **Keyboard vs pad.** The PC game picks keyboard or controller prompts
   through the exe (Recomp names a native `SetKeyboardMapping`). If the
   engine reports keyboard or no pad, scripts may pick a frame set that
   doesn't exist. Check that the engine reports a controller.

## After the frame is right
That multi-frame icon clip still holds Xbox art: A, B, X, Y, LB, RB, LT, RT,
Back, Start and so on. Every frame needs the option A treatment, the same
coloured balls with PlayStation symbols already used for the six menu
clips:
- A becomes Cross (blue), B Circle (red), X Square (pink), Y Triangle (green).
- LB/LT become L and RB/RT become R. Mind the Vita mapping: block is L and
  magic is R. Item switching is the rear touchpad, which has no button art,
  so use a small touchpad icon or text.
- Back becomes Select and Start stays Start.

## Quick test
Show a prompt for each action (weapon pickup, shop buy, door, tutorial
lines) and log the clip name, the gotoAndStop argument and the frame it
resolves to. Any unresolved argument points straight at hypothesis 1, 2 or 3.
