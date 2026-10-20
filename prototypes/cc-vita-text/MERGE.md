# Merging cc-vita-text into the PC engine

This was written in the cloud on 2026-10-14 while the PC was off. It doesn't
change any file in the PC's cc-vita repo: it's four new modules plus these
steps. Desktop checks pass (`tools/texttest.c`), and all four modules compile
cleanly with the VitaSDK (`-Wall -Wextra`). It hasn't run inside the engine
yet.

> **Update 13:55: the game's words are readable.** Scripts set
> `field.ntext = N`, and castle.exe holds all 778 strings, in 10 languages, as
> a plain table. `tools/extract_strings.py` writes them out. Step 3 is now the
> `ntext` hook; the "fixme" path lookup stays only as a fallback. The
> title/main and pause menus are native code in castle.exe, so they need
> reimplementing, and menu text uses the BMFont `synjUnicode_20.fnt` plus its
> DDS page. See research/menu-text-sources.md.

## 0. Get the words (on the PC)
```
python3 tools/extract_strings.py --exe "<working folder>/Steam PC copy/original/castle.exe" --out strings
```
This writes strings_en.txt and the nine other languages (de, fr, es, it, ja,
ko, zh-Hant, pt, zh-Hans). Upload them to ux0:data/CastleCrashers/. They're
Jah's own game text: keep them out of git and never share them.

## 1. Add the files
Copy `src/strtab.*`, `src/prompts.*`, `src/textlayout.*` and
`src/prompts_vita.*` into cc-vita/src, then add the four .c files to the
executable's sources in CMakeLists.txt. They need no new libraries; vita2d
and libm are already linked.

Copy `data/strings_en.txt` and `data/strings_vita_en.txt` to
`ux0:data/CastleCrashers/`. The first one is an empty template: the game's
words are never committed or shared.

## 2. Startup (after vita2d_init)
```c
prompts_init();                                     // Vita control mapping
strtab_load(DATA_DIR "strings_en.txt");             // the game's words (Jah's own)
strtab_load(DATA_DIR "strings_vita_en.txt");        // our Vita wording, wins
prompts_map_from_strtab();                          // optional icon characters
strtab_set_missing_log(DATA_DIR "strings_missing.txt");
if (file_exists(DATA_DIR "show_keys")) strtab_set_show_keys(1);   // debug
prompts_vita_init();                                // glyph atlas texture
```
The atlas is drawn procedurally, which takes about 20 ms on a desktop and
probably 100-200 ms on the Vita, once. `show_keys` is an empty flag file:
with it present, every blank field shows its key on screen, which is the
quickest way to see which field is which. It doesn't need a button combo.

## 3. Fill the text fields
**Main hook:** when a script sets `ntext` on a TextField (a property write
the engine currently ignores), set the field's text to:
```c
const char *s = strtab_number(n);   // NULL -> leave blank (and logged)
```
The world map, lobby and other SWF screens use this.

**Fallback:** where the engine sets up or draws a DefineEditText field, if the field's text
is still exactly `"fixme"` (it hasn't been set by a script), call the
following once and cache the result on the field instance:
```c
const char *s = strtab_field(swf_name /* "MAIN", "LOBBY", "WORLDMAP"... */,
                             target_path /* "_root.a.b.field" */,
                             edittext_variable_name, "fixme");
// NULL: no words yet, so keep drawing nothing (current behaviour)
```
Only "fixme" fields are looked up. Fields the scripts fill (numbers, names)
are left alone, so they never land in the missing log.

**Native menus:** the menu code calls `strtab_number(label_id)` for each
item's label. The button prompts attach the SWF clips `Abutton`, `Bbutton`,
`Xbutton`, `Ybutton`, `Ltrigger` and `Rtrigger`. To get PlayStation art in
the game's style, redraw those clips with the PlayStation symbols. The
procedural glyphs here are only a stand-in, and they serve inline `{tokens}`
in text.

If the world map turns out to use static text (DefineText/DefineText2 tags,
not EditText), that's a separate engine gap. Static text carries its own
glyph indices into the DefineFont, so it needs drawing, not strings. Check
whether `swf.c` parses those tags.

## 4. Draw text with glyphs
Replace the per-character loop in the EditText drawer with the layout:
```c
TextMetrics m = {
    .advance = font_advance,       // DefineFont3 advance * size / 1024 (or /20480 for font3 units)
    .ud = field_font,
    .line_height = ascent + descent + leading,
    .glyph_size = cap * 1.6f,      // cap ~ 0.7 * ascent
    .glyph_raise = cap * 0.5f,
    .glyph_pad = size * 0.1f,
};
TextRun runs[128];
TextBlock tb = text_layout(text, &m, field->wordwrap ? field_width : 0,
                           field->align /* 0 L, 1 C, 2 R: same as Flash */, field_width,
                           runs, 128);
text_draw_runs(text, &m, runs, tb.nruns, left, top, draw_text_run, draw_glyph, ctx);
```
- `draw_text_run` is the engine's existing glyph drawing for the bytes
  `[s, s+len)` at that pen position and baseline.
- In `draw_glyph`, if text is drawn through the field's matrix with the
  textured-triangle path (the bitmap spans in render.c), call
  `prompts_glyph_quad(g, x, y_center, h, field_matrix, quad)` and emit two
  textured triangles with the atlas texture (`prompts_vita_init()` returns
  it). If text is drawn in screen space, `prompts_vita_draw(g, x, y_center, h,
  RGBA8(255,255,255,alpha))` is enough.
- Lay text out when it changes, not every frame.

## 5. Button names from natives
Any stubbed engine native that the PC game uses to name a key or button (for
example to build "Press Enter" style hints) should return a token instead:
`"{accept}"`, `"{back}"`, `"{start}"`, `"{light}"`, `"{heavy}"`, `"{magic}"`,
`"{block}"`, `"{jump}"` or `"{projectile}"`. The layout turns these into
PlayStation glyphs wherever they end up. Xbox-style names in strings (`{a}`,
`{b}`, `{x}`, `{y}`, `{lb}`, `{rb}`, `{lt}`, `{rt}`) also map to the
PlayStation buttons.

## 6. Filling strings_en.txt
After one pass through the menus, `strings_missing.txt` lists every blank
field as `KEY = `, with its variable name in a comment. Words go after the
`=`, with button tokens instead of key names. See
research/menu-text-sources.md for where the words come from.
