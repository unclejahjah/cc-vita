# Castle Crashers menu text and button art: where it lives (updated 2026-10-15)

> **Result on Jah's PC (2026-10-14 19:25):** Jah's castle.exe is the
> Remastered/FRESH build, and the 10-language table described below wasn't
> there. The extractor picked up a colour/item name table instead. The PC
> session matched 282 text numbers to the exe's English words by hand. The
> extractor now refuses tables that don't look like menu text. Jah chose button
> style A (the game's coloured balls redrawn with PlayStation symbols), made
> by the PC session from the game's own clips, and it's in the current build.

## Headline: the PC game's words are NOT locked
The earlier assumption that castle.exe keeps its words encrypted looks wrong.
An open-source reimplementation of the 2012 Steam build,
[myuu-151/Castle-Crashers-Recomp](https://github.com/myuu-151/Castle-Crashers-Recomp),
reads them straight from the exe file. It's unlicensed, so we use what it
documents and none of its code. What it documents:

- **Text is shown by number.** Scripts set `someField.ntext = 742`, and the
  exe shows string 742. "fixme" is only the field's placeholder.
- **The words are a plain table in castle.exe.** It has 778 records, each a
  u32 string number followed by 10 pointers to UTF-16 strings, one per
  language: en, de, fr, es, it, ja, ko, zh-Hant, pt, zh-Hans. That's 10
  languages for free. String 742 is "We recommend a controller to play!".
  SteamStub-style DRM encrypts the code section, not this data.
- **Menu text uses a bitmap font, not the SWF fonts.** That font is
  `synjUnicode_20.fnt`, an AngelCode BMFont (binary v3) whose glyphs are in an
  uncompressed 32-bit DDS page. That's the 16 MB DDS in the inventory.
- **The title/main menu and pause menu are native code in castle.exe, not
  SWF.** The exe builds the pages itself, draws their text, and attaches
  Flash clips from the SWFs for backgrounds and button prompts. A 1:1 port has
  to reimplement this menu code (MainMenu, its pages, items and prompts).
- **Button prompts are SWF clips, attached by linkage name:** `Abutton`,
  `Bbutton`, `Xbutton`, `Ybutton`, `Ltrigger` and `Rtrigger`. Each has a label
  string drawn to its right, in grey at 0.7 scale. This is the game's own
  button art, and on PC it's Xbox-style.

Our tool: `code/cc-vita-text/tools/extract_strings.py`. It finds the table by
its shape rather than a fixed offset, so other builds work too, and writes
`strings_<lang>.txt` in our format (`742 = ...`). It's tested on a synthetic
exe, not yet on Jah's. Run it on the PC against the COPY of castle.exe in the
working folder.

## Original vs Remastered (Jah wants the ORIGINAL menus, 2026-10-14 15:00)
- Steam got the Remastered content as a free update on 2015-10-14
  ([Escapist](https://www.escapistmagazine.com/castle-crashers-remastered-is-a-free-update-for-pc/),
  [fandom](https://castlecrashers.fandom.com/wiki/Castle_Crashers_Remastered)).
  That update added the Back Off Barbarian minigame, which replaced All You
  Can Quaff (Volleyball was also removed), plus an uncapped frame rate, an
  "Ultra" HD texture option (64-bit only), Russian, and unspecified "updates
  to UI flow". The Xbox One version followed in 2015, and PS4 and Switch in
  2019.
- So Jah's Steam copy is probably the updated build. The Recomp project calls
  the current Steam build "the 2012 build", though, and Jah's castle.exe is
  32-bit, so it's unclear how much of the menu changed. Check on the PC:
  1. Does extract_strings.py find 10 languages (the original table) or 11
     (Russian added, so Remastered)? It reports this.
  2. Do the SWFs have Back Off Barbarian, or All You Can Quaff and
     Volleyball? Look in the asset list or file names.
  3. Is castle.exe's sha256 f39c3e6f...490d, the build Recomp targets?
- Remastered-only parts to leave out of the Vita menus: the Back Off Barbarian
  entry, the Ultra/HD texture option, the Russian language, and any online or
  matchmaking items (no online on the Vita anyway). All You Can Quaff and
  Volleyball can only come back if their files exist in one of Jah's copies.
- If Jah's files turn out to be Remastered, original sources to consider:
  an older manifest of Steam depot 204361 (owners can fetch old builds with
  Steam's `download_depot` console command, but the manifest ID has to be
  found), or the PS3 or XBLA copies (the original edition), as above.
- **"FRESH" (Jah spotted it in the PC version) is the 2025 Painter Boss
  Paradise refresh.** The Behemoth's launch post calls it "a FRESH new version
  of Castle Crashers"
  ([blog, 2025-08-06](https://blog.thebehemoth.com/2025/08/06/castle-crashers-painter-boss-paradise-is-now-available-on-steam/)).
  According to [GameSpot](https://www.gamespot.com/articles/castle-crashers-first-new-dlc-in-over-a-decade-is-live-now/1100-6533754/):
  - Free for everyone in the base game: 8 new main menu background vignettes,
    and new Back Off Barbarian character art.
  - Only with the paid DLC: new art for every classic character and weapon,
    a completely redesigned world map, a new character (Paint Junior), and
    Workshop custom knights. The DLC can be switched off, which returns to the
    classic art.
  So today's Steam copy is the 2012 port plus the 2015 Remastered update plus
  the 2025 FRESH refresh. For the original menus, avoid the new menu
  background vignettes and any FRESH, redesigned map or new-art SWFs, and use
  the classic files where both exist. On the PC: list which SWFs or paks are
  FRESH or DLC, check whether the classic menu backgrounds and world map are
  still in the files, and check which world map the Vita build loads.
- The button art is the same idea in both editions: the Abutton and
  related clips get PlayStation symbols drawn in the game's style.

## PlayStation button art that matches the game
Options, in order of preference:
1. **Redraw the six prompt clips in the game's style (recommended).** Take the
   Xbox clips from Jah's SWFs (Abutton and the rest), keep their outline,
   shading and size, and replace the letter with the PlayStation symbol:
   A to Cross, B to Circle, X to Square, Y to Triangle, LT to L and RT to R.
   The engine attaches clips by name, so the new art drops into the same
   place in every menu. It's made on the PC from Jah's files and never shared.
2. **The PS3 version's own clips.** The PS3 build ran the same engine, so its
   SWFs very likely have the same linkage names with PlayStation art inside.
   That's an inference, not checked. Getting them needs PS3HEN (all models;
   firmware 4.83-4.93 needs the HFW update first;
   [ConsoleMods: PS3HEN](https://ConsoleMods.org/wiki/PS3:PS3HEN)), then the
   game folder copied over FTP and its .pak files decrypted. The PS3 .pak
   format hasn't been checked.
3. **PS4 or Switch:** these are the Remastered edition, and dumping them needs
   a jailbroken PS4 or a hackable Switch. Not worth it while option 1 exists.
4. **Xbox 360 (XBLA):** the art is Xbox-style like the PC, so it's no help
   for PlayStation glyphs.

In-text prompts also matter. If any of the 778 strings name buttons ("Press A"),
the extracted file shows it; they get rewritten with tokens like `{accept}` in
strings_vita_en.txt, which overrides by number.

## What this changes in the plan
- Menu words: run extract_strings.py on the PC. No screenshots needed.
- World map and other SWF screens: hook `ntext` (see cc-vita-text/MERGE.md).
- Title/main menu: needs a native menu reimplementation in the Vita engine,
  because castle.exe draws it itself.
- Font: load synjUnicode_20.fnt and its DDS page for menu and field text.

## Earlier notes (kept)
- PC .pak files are ZIPs holding Blowfish-encrypted COK6 NREC blobs that
  decrypt to SWF ([ethteck/castlecrashers](https://github.com/ethteck/castlecrashers)).
- [Eigenvoid: Unpacking Castle Crashers Remastered](https://eigenvoid.dev/posts/unpacking-castle-crashers-remastered-game-files)
- [A1rPun/castle-crashers-resource](https://github.com/A1rPun/castle-crashers-resource):
  saves and Cheat Engine, nothing on text.
- [Velocity](https://consolemods.org/wiki/Xbox_360:Velocity) opens Xbox 360 packages.
