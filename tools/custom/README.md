# Custom characters: fitting the template to PLAYER.swf

The Painter Boss Paradise template (`pbp_template.pak` in the PC game, a 2048x2048 PNG) has a box
per body part with dashed guide lines (or, for the beefy arms, the art itself) drawn at about twice
the game's size. `src/custom_char.c`'s PARTS table says which part clip of the original PLAYER.swf
each box paints, and where: these scripts found those numbers. Nothing here holds game art; run
them on your own files (PBP_TEMPLATE=template.png).

- `partsel.py PLAYER.swf`: the part clips, by the selector their first frame calls (f_Helmet, f_Belt,
  f_Shield, f_Weapon). The engine finds them the same way, by order, since ids differ between builds.
- `fit.py PLAYER.swf box sprite frame k,k,...`: outline fit (chamfer distance) for dashed guides.
- `fitk.py PLAYER.swf box:sprite ...`: the same over scales 1.9-2.6 (heads come out at about 2.2).
- `fitarm.py PLAYER.swf box:sprite ...`: filled-silhouette fit with rotation and mirroring (the arm
  boxes hold the arms turned by 0 or 90 degrees).
- `overlay.py PLAYER.swf out.png box:sprite:frame:k:ox:oy ...`: guide in black over the part in red.
- `testpaint2.py PLAYER.swf template.png src/custom_char.c out.png`: paints every part's own art,
  purple, into its box with the table's numbers. Loaded as a custom character, it should look like
  the knight it replaces, in purple (hosttest: CC_CUSTOM_DIR=dir/ with dir/1/painting.png).

Box numbers here are the template's 39 boxes in reading order; custom_char.c's BOX table leaves
out the three portrait boxes (33-35 here), so the last three arm boxes are 33-35 there.
