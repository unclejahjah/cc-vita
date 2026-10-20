# cc-vita-text

Text and PlayStation button prompts for the Castle Crashers Vita engine.
To add it to the PC engine, see MERGE.md.

- `src/strtab.*`: string table. It loads `key = value` UTF-8 files from
  ux0:data/CastleCrashers, looks up the game's "fixme" text fields, and logs
  the ones still missing to strings_missing.txt.
- `src/prompts.*`: PlayStation glyph set. It covers Cross, Circle, Square,
  Triangle, L, R, Start, Select, the d-pad (plain, each direction,
  up/down, left/right) and both sticks. It also handles `{token}` names,
  action tokens that follow the control mapping (`{accept}` and `{light}` and
  so on), Xbox-name translation, and a procedurally drawn 512x192 atlas, so
  no image files ship.
- `src/textlayout.*`: lays out text with inline glyphs, word wrap and
  left, centre or right alignment. Fonts plug in through an advance-width
  callback.
- `src/prompts_vita.*`: uploads the atlas to vita2d and draws a glyph.
- `data/strings_vita_en.txt`: the Vita's own wording (Press Start, menu
  legends, controls, pause hint).
- `data/strings_en.txt`: an empty template for the game's own words, filled
  from Jah's copy and never shared.
- `tools/texttest.c`: desktop checks plus a 960x544 preview PNG.

```
gcc -O2 -Isrc -Ithird_party/stb -o texttest tools/texttest.c src/strtab.c src/prompts.c src/textlayout.c -lm
./texttest data /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf preview.png atlas.png
```
Previews: /mnt/project-files/renders/text-glyphs-preview.png and
/mnt/project-files/renders/playstation-glyphs.png.

`third_party/stb` (stb_truetype and stb_image_write, public domain) is only
used by the desktop test.
