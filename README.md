# cc-vita

This is the start of a native Castle Crashers port for the PS Vita. Right now it's a renderer test: it plays any animation from PLAYER.swf on the GPU.

No game data is included, and none may ever be committed (see `.gitignore`). At runtime the app reads the SWF files from your own copy of the game, so running or testing needs those files from the PC install.

Repo layout: `src/` engine, `tools/` desktop host test and analysis scripts, `HANDOFF.md` current state, `docs/research/` research notes, `prototypes/` older cloud-built pieces (already merged or superseded).

## Build (VitaSDK)

```
mkdir build && cd build
cmake ..
make
```

This produces `cc_vita.vpk`. Install it with VitaShell, then copy your `game/PLAYER.swf` to `ux0:data/CastleCrashers/PLAYER.swf` and your `levels/*.swf` to `ux0:data/CastleCrashers/levels/`.

The app has two modes; SELECT+TRIANGLE switches between them.
- Level mode (the default): the stick or D-pad walks the knight. SQUARE is a light attack that chains into the 4-hit combo, TRIANGLE is a heavy attack, CROSS jumps (SQUARE in the air jump-kicks), and R blocks. SELECT+Left/Right changes level, and SELECT+Down shows the collision edges.
- Animation viewer: Left/Right changes animation, and L/R zooms.

In both modes SELECT+Up toggles the 60 fps cap (vblank wait) and START quits.

Combos follow `f_PunchSet1` (X) and `f_PunchSet2` (Y) in MAIN.swf. Each press picks the next move from the current `punch_group`/`punch_num`: XXXX, Y/YY/YYY, XY uppercut (level 8), XXY (16), XXYY (32), dash+X/Y (4), and in the air X slashes, Y slams down, and X then Y does an uppercut. `combo_unlocks[i]` needs character level 1, 2, 4, 8, 16, 32 or 50. Players have no jump kick; that clip only appears in `f_JumpKickInit`, which isn't on the player's attack path.

Enemies (`src/enemy.c`) are the player rig with different parts, as in the game. The body holds four unnamed part clips, stopped on frame 0, that the game switches with `gotoAndStop`: weapons (112 frames), heads (59), arms (32) and torsos (31). Frame k of the head, arm and torso clips is character k (5 grey knight, 6 barbarian, 7 thief). `Actor.part_cid/part_frame` pins those clips per character. A player swing hits once per enemy, from frame 2 until its fin frame, within 75 px in front and 22 px in depth. Light hits stagger, heavy hits knock back, the uppercut launches, air hits juggle, and slams or a defeat knock the enemy down. Enemies line up beside the knight and swing (`punch1_1`), which hits at its fin frame unless he blocks facing them. Waves of 3 spawn ahead of the knight.

`src/player.c` holds the knight's state machine (idle, walk, attack, jump, land, block). Each animation is a root label whose `body` clip plays it. The clip's stop() frame ends the animation, and its `fin` label is the first frame from which the next light hit may start; a press before that is queued. Jumps use the game's player values: `speed_launch` −20 px/frame and `gravity` 2 at 30 frames a second. Like the game, only the `body` child is lifted by `body_y`, so the shadow stays on the ground. `Actor.body_frame` can pin the body clip to one frame (the jump kick's "kick" frame), as `gotoAndStop` does. The stats line updates once a second and shows that second's average and peak build time and peak triangle count.

## Levels
`src/level.c` draws a level's parallax layers (`bg3_c`, `bg2_c`, `bg1_c`, `game`, `bg0_c`) using the camera rules from the game's `f_UpdateCamera` in MAIN.swf. Each container sits at (424, 480) on the 848×480 stage and is scaled around that point. Its child scrolls by camera × factor: 0.1, 0.25, 0.5 and 1.0 for the play layer, and 1.5 for the foreground. The vertical camera is offset by −100. The knight spawns at `p1_start`, and the camera stops at `edge_left`/`edge_right`. Editor markers that the game's scripts would hide (doorN portals, spawn points, waypoints, cutscene "guide" cameras, `invisObject`s) are removed from the play layer. Shapes entirely off screen are culled.

Collision works the way the game's engine does it. One frame of the play layer, the one the level script steps to before `f_BSPLoadLevel`, holds edge clips. Each edge clip has children named `bsp1` at (0,0) and `bsp2` at (64,0), and a one-line script `n_type = N` that sets its type: 1–4 for walls, 100/101 for stairs, 300 for ladders, and 400 for death boxes, going by the MAIN.swf functions that use each type. `level_hit_test` matches `f_BSPHitTest`: it returns the fraction of a move before the first edge, or 0 for no hit. `level_move` moves horizontally and then vertically, each time stopping 0.001 short of the hit, like `f_MoveCharH/V`, so characters slide along walls. Stairs follow the game's ground-type state machine. A character in plain mode that crosses a 100/101 edge, sideways (`f_PlainMCH`) or up/down (`f_PlainMCV`), passes through and switches to stairs mode. In stairs mode (`f_StairsMCH`, `f_InsideMCV`), moves are free until they cross any edge, which passes through and switches back to plain mode. Sideways moves on the stairs also lift the drawn body (`body_y`) along the stair line below the feet. Depth sorting still uses the feet. Every other edge type blocks for now. A few levels (8, 14, 19, 27, 39) have no edges. In the level viewer, SQUARE draws the edges. Custom tag 148 isn't collision; it's a 64×64 texture (`PIXL`) that follows a bitmap tag.

Draw order follows the game's `f_Depth`. Characters, and play-layer clips whose first-frame script calls `f_AddNeutral(this)` or `f_AddObject(this)`, are moved to depths around 1,000,000 ordered by y. `f_Depth(this, N)` sorts a clip as if it stood at y = N. Everything else keeps its timeline depth, which is far lower. The parser spots these calls by decoding the Push and ConstantPool actions in each clip's first frame. `level_render` then draws the static play-layer art first, followed by the sorted objects and the actors merged by y.

Not handled yet: the sky (`SKY*.swf` uses bitmaps), and the special edge types (stairs, ladders, death boxes).

## Layout
- `src/swf.c` is the SWF parser, a C port of `code/swfproto/swf.py`. It reads shapes, sprites, timelines and `stop()` scripts.
- `src/render.c` handles rendering. It tessellates each shape once, using libtess2 with the even-odd rule, and caches the result. Each frame it flattens the display list into a single triangle list, applying transforms and colour transforms on the CPU. That list goes to the GPU in one `vita2d_draw_array` call.
- `src/main.c` is the Vita app.
- `tools/hosttest.c` is a desktop build of the same parser and renderer. It software-rasterises frames to PPM so you can check output without a Vita:
  `gcc -O2 -Ithird_party/libtess2/Include -o hosttest tools/hosttest.c src/swf.c src/render.c third_party/libtess2/Source/*.c -lm`
- `third_party/libtess2` is vendored from memononen/libtess2 (SGI Free Software License B). See `VERSION` for the commit.

Masks (clip layers) use the stencil buffer. Each mask is drawn with alpha 0 to increment the stencil, the masked content draws where the stencil equals the nesting level, and the same mask triangles then decrement it again. `render.c` records this as a short list of draw commands alongside the vertex buffer. `hosttest` applies the same rules in software.

`sce_gap.ld` works around a VitaSDK quirk: `vita-elf-create` fails with "segment 1 overlaps" when the code happens to end just below a 64 KB boundary.

## Not done yet
- Gradients are evaluated per vertex, so large gradients can look banded.
- No bitmap fills (PLAYER.swf has none).
- Nested sprites advance on one global tick. The game engine drives some of them explicitly.
