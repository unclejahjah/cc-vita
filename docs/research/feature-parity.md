# Castle Crashers Vita: full feature checklist (match the original)

Jah, 2026-10-14: "add everything from the original I dont want anything missed".

The Vita engine runs the game's own ActionScript, so most game logic comes for free.
What is missing is mostly the functions the PC exe provides natively (menus, saves,
XP/stats glue, sound, etc.). Each item is ticked only after it is checked on the Vita
against the original. Status key: [x] works on Vita, [~] partly, [ ] not done / unchecked.

## Core play
- [~] Movement, jump, block, dash, stairs, ladders, death zones
- [~] Combos: XXXX, Y/YY/YYY, XY uppercut, XXY, XXYY, dash attacks, air X / Y / XY, juggles (unlock by level)
- [ ] Magic: XXXY magic combo, projectile (Circle), magic jump, element per character, magic stat scaling
- [ ] Damage formula: level, Strength / Magic / Defense / Agility, weapon stats (Jah: currently far too high)
- [ ] Enemy toss / bounce / juggle physics match (partly done)
- [ ] Revive teammates, death and respawn rules
- [ ] Bow (projectile) and arrows; bombs and other usable items (Square + R item menu)
- [ ] Switching items: item selection/cycling works like the original (Jah 17:03: needs a fix)

## Progression
- [ ] XP from hits and kills; level-ups (Jah: never levels up)
- [ ] Stat points screen (Strength, Magic, Defense, Agility) and stat caps
- [ ] Level 99 cap; combo unlocks at levels 4, 8, 16, 32
- [ ] Per-character saves: level, stats, weapon, animal orb, completed levels, gold
- [ ] Insane Mode after finishing the game (stronger enemies, separate progress)

## Items and loot
- [ ] Weapons: drop, button prompt and stats preview before pickup (Jah), all weapons and their stats
- [ ] Weapon frog and other hidden weapons
- [ ] Gold and gems drop; gold total carries over
- [ ] Food: sandwich (big/strength form, breaks special doors: Jah reports broken), turkey legs, other food heals
- [ ] Shops: buy weapons, items, potions; shop menus
- [ ] Animal orbs: all orbs, how each is found, each orb's ability and stat bonus
- [ ] Animal Handler unlocks / orb collection screen
- [ ] Hidden items: shovel and dig spots, sandwiches, keys, chests, secret areas

## World and levels
- [ ] World map: all nodes, paths, level unlocks, shops, returning to finished levels
- [ ] Every level playable start to finish (with the area-to-area transition fixed: initLoadedFxClip blue screen)
- [ ] All bosses with their phases and cutscenes
- [ ] Cutscenes and story scenes between levels
- [ ] Barbarian Boss arena / Arena battles between players
- [ ] Final scenes, credits, ending and the kiss fight between players

## Characters
- [ ] Four starting knights (Red, Blue, Green, Orange) with their magic
- [ ] Gray Knight and all unlockable characters (barbarian, thief, etc.) with their unlock conditions
- [ ] Character select: locked/unlocked display, per-character level

## Modes and extras
- [ ] Main story (Normal)
- [ ] Insane Mode
- [ ] Arena (versus)
- [ ] All You Can Quaff mini-game
- [ ] Back Off Barbarian mini-game
- [ ] Volleyball (if present in the PC version's data)
- [ ] Achievements / trophies-equivalent progress (local only)

## Multiplayer
- [ ] Up to 4 players (game supports 4 controller ports)
- [ ] Local co-op on one Vita with extra Bluetooth pads, if feasible
- [ ] Vita-to-Vita Wi-Fi co-op (custom netcode)

## Presentation
- [~] Menu text and button prompts (cc-vita-text merge in progress)
- [ ] Title / main / pause menus (native in exe, reimplement)
- [~] Sound effects (work); [ ] music (silent on Vita)
- [ ] Options: volume, controls, language (10 languages in castle.exe)
- [ ] HUD: health, magic, XP bar, gold, portraits for up to 4 players
- [ ] Performance: steady 60 fps in fights (script engine is the bottleneck)

## How to use this list
When the PC is on: for each item, find where the original does it (scripts in the SWFs
or a native in castle.exe), implement any missing native, then test on the Vita and tick.
Some names above come from general knowledge of the game and may not match the PC data
exactly; confirm against the SWFs and drop anything not in Jah's version.
