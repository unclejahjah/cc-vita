# Vita lag evidence from Jah's video (2026-10-14 13:48)

Source: 21 s phone video of the "CC Vita Renderer Test" build with the Select+Up stats overlay on.
Frames: /mnt/project-files/renders/vita-video-2026-10-14/ (overlay crops at 1 s steps, one full frame).
Readings are from a 480x360 phone video, so digits are approximate (±1 on the first digit is possible).

## What the video shows
1. 0-2 s: world map, overlay shows ~60 fps with tiny script time. Map has no text (known bug).
2. 2-6.5 s: "Loading level1_1.swf...", a brief scene, then another "Loading ....swf..." screen.
3. 6.5-21 s: first castle level (drawbridge/log), 2-3 knights fighting ~8 green enemies. Hit sparks, blood, HUD portrait + bars work. Sound effects audible in combat.

## Overlay during the fight (line 1: fps / script / build / tris / draws / cpu-coloured / heap)
| t (s) | fps | script ms | build ms | tris | draws | cpu-coloured |
|---|---|---|---|---|---|---|
| 7  | ~2 (spawn spike) | ~412 | 1.2 | ~73k | 187 | 0 |
| 8  | 12 | ~46 | 1.4 | ~70k | ~200 | 0 |
| 9  | 19 | ~38 | 1.0 | ~72k | ~190 | ~60 |
| 10 | 20 | ~30 | 2.1 | ~72k | 226 | 0 |
| 11 | 21 | ~33 | 1.0 | ~72k | ~220 | 0 |
| 12 | 15 | ~39 | 1.0 | ~70k | ~210 | 132 |
| 13 | 23 | ~33 | 1.0 | ~73k | 224 | 0 |
| 14 | 27 | ~32 | 2.1 | ~71k | ~210 | 0 |
| 15 | 27 | ~31 | 1.9 | ~72k | ~212 | 72 |
| 16 | 22 | ~34 | 1.8 | ~71k | ~210 | ~200 |
| 17 | 19 | ~32 | 1.9 | ~71k | 227 | ~200 |
| 18 | 28 | ~30 | 2.1 | ~72k | 220 | 0 |
| 19 | 25 | ~33 | 1.9 | ~71k | 230 | 0 |
| 20 | 21 | ~33 | 2.0 | ~70k | ~215 | 0 |

Line 2 every frame: "11 files, 52.0 MB GPU, call to undefined method gotoAndStop on undefined (in f. Magic)".

## Conclusions
- **The lag is the ActionScript interpreter, not the renderer.** Script time is 30-46 ms per frame in combat (the 60 fps budget is 16.7 ms); render build is 1-2 ms. Script time alone caps the fight at ~22-30 fps, which matches the fps shown.
- Triangle count (~70k) and draws (~220) stay flat whether enemies are bunched or not; the GPU side is fine.
- fps dips (12-19) line up with cpu-coloured > 0 (hit flashes / colour transforms done on CPU) and with heavy-hit moments, but the main cost is the steady ~30 ms of script.
- One ~400 ms frame when the level starts / enemies spawn (t≈7 s): likely first-time script or clip setup; a one-off hitch.
- The script error "call to undefined method gotoAndStop on undefined (in f. Magic)" fires during play; a `Magic` function is calling gotoAndStop on a missing clip. It may be cheap, but if it logs every frame it is worth checking, and the magic attack is probably broken.

## Suggested next steps for the lag fix (needs the PC build)
1. Profile per-function script time inside the interpreter (top N ActionScript functions by ms per frame), especially enemy AI / per-frame onEnterFrame handlers.
2. Usual interpreter wins: cache property/variable lookups (string hashing per access), avoid allocating per op, precompile bytecode to a faster form, skip onEnterFrame for off-screen or dead clips.
3. Move colour transforms (cpu-coloured) to a shader.
4. Fix the gotoAndStop-on-undefined error in Magic.

## Audio (confirmed by Jah, 13:51)
Sound effects all play; level music is missing. The music bug was routed to the feasibility thread.
