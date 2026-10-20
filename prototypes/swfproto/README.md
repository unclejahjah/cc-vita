# swfproto

Prototype SWF reader and renderer for Castle Crashers' decompiled assets. It checks the asset pipeline before we port it to the Vita.

```
python3 -m pip install pycairo pillow
python3 render.py path/to/PLAYER.swf walk out/ --frames 16 --scale 2
```

- `swf.py` is the parser. It handles DefineShape 1-4, DefineSprite, PlaceObject2/3, RemoveObject, FrameLabel, ExportAssets, lossless bitmaps and `stop()` detection in frame scripts.
- `render.py` is a cairo reference renderer. It covers solid fills, gradients, bitmap fills, strokes, colour transforms and clip-depth masks.

## How PLAYER.swf is laid out
- The root timeline is a state table: each labelled frame (stand, walk, dash, jump, punch1_1 and so on, 200+ of them) places a per-state animation sprite at depth 5.
- Part sprites with `stop()` on frame 1 are selectors, for example knight colour or weapon. The engine picks the frame with gotoAndStop, and frame 0 is the Green Knight.
- A character frame is about 10 shapes and about 2.5k outline points. Tessellated once at load, that's trivial for the Vita GPU.

## Known gaps
- Jah's PLAYER.swf is 128 bytes shorter than its header says, so the last sprite (1941) is cut off. The parser stops cleanly at that point.
- Morph shapes, text, buttons and DefineShape4 line fills are not handled yet.
- Nested sprites all advance on a global tick. The engine drives some of them explicitly, so we need its state machine to match it exactly.

## Next: Vita port of this renderer
At load time, flatten curves and tessellate each fill path into triangles (earcut or libtess2), then cache the vertex buffers per shape. Each frame, walk the display list and issue GXM draws with a matrix and colour-transform uniform. Masks go through the stencil buffer.
