# Knight attacks and jumping: spec from PLAYER.swf (2026-10-13)

This spec was extracted in the cloud from Jah's PLAYER.swf, using the frame scripts inside each animation. It's meant for implementing attacks and jumping in cc-vita.

Raw data:
- `player-animations.txt`: all 220 root labels, with sprite id, frame count and stop frame
- `player-move-scripts.txt`: the per-frame script calls for the core moves

## How the animations talk to the engine
Each move sprite's frame scripts set flags on the player (`_parent`) or call engine functions:

| Script on a frame | What the engine does (inferred) |
|---|---|
| `_parent.fp_PunchHit()` | Runs the hit check this frame (the active frame) |
| `hit` / `fin` frame labels | Start and end of the active window |
| `_parent.punching = false` | Combo window opens: the next attack input is accepted from here |
| `_parent.end_attack()` | The move is over and control returns (go to stand or walk) |
| `_root.f_ScreenShake`, `f_GroundSlamDust`, `f_SlideDust` | Effects only |
| `_root.s_Swing1.start()` etc. | Sound effects |
| sprite stops on its last frame | The animation holds there until the engine moves on |

The game runs at 30 fps, so 1 frame is 33 ms.

## Core moves (frames are 0-based, inside the move sprite)

| Move | Frames | Hit frame(s) | Combo window opens | Ends |
|---|---|---|---|---|
| punch1_1 (light 1) | 13 | 2 | 7 | 11 |
| punch1_2 (light 2) | 13 | 4 | 8 | 12 |
| punch1_3 (light 3) | 14 | 3 | 10 | 13 |
| punch1_4 (light 4, slide finisher) | 18 | 9 | 14 | 17 |
| punch2_1 (heavy) | 16 | 6 (sword-lock check on 5) | 11 | 15 |
| punch2_2 (triple swish) | 13 | 1, 5 | 6 | 11 |
| punch2_2b | 18 | 1, 8 | 9 | 16 |
| punch2_3 (ground slam) | 12 | 3 (+ screen shake) | 9 | 11 |
| uppercut | 15 | 2 | 8 (then spin) | 13 |
| slamdown | 15 | 4 | 8 | 13 |
| dashpunch | 13 | — (dash) | 6 | 11 |
| dashslam | 14 | — | 11 | 11 |
| jumpkick (air attack) | 3 | 1 | — | — |

## Jumping
- `jump` (12 frames) plays from frame 0 and stops at the `mid` label (frame 11). The frame 11 script checks `_parent.jumping`, `ladder` and `hanging`, so the pose holds in the air until landing.
- `land` (7 frames): on frame 5 the script resets `busy`, sets `body_y = body_table_y` and calls `fp_StandAnim`, which returns control.
- `jumpkick` is the air attack: hit on frame 1, then it stops on the `kick` pose.
- The jump arc itself (height and gravity) is in the engine, not the SWF. Tune it by eye against the original, starting from about 1 s of airtime.

## Combo mapping (still to confirm)
The input-to-move table lives in the exe, not the SWF. A likely starting point from the original game:
- X, X, X, X → punch1_1 → 1_2 → 1_3 → 1_4
- Y → punch2_1
- X, X, Y → punch2_2
- X, Y → uppercut
- air + X → jumpkick
- air + Y → slamdown

Treat these as placeholders until they're checked against the real game.
