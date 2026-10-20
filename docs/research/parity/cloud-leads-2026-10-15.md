# Leads for the PC session (cloud work, 2026-10-15, PC off)

Source: the MIT fan guide copy in /mnt/project-files/research/cc-resource (Modding/A1rMods.md quotes
decompiled main.swf frame 2 code). Everything below is a lead to confirm against the
"Raw Game Files CC\Source Code" dumps on the PC. Nothing was changed in cc-vita.

## 1. Damage is game script, not exe
`f_Damage(zone, damage_pow, damage_type, damage_flags, recoil_x, recoil_y)` lives in main.swf frame 2.
It subtracts `damage_pow` from `zone.health`, rolls criticals with `random(zone.hitby.weapon_critical) == 0`
and elemental procs with `random(zone.hitby.weapon_magic_chance) == 0` when `weapon_magic_type > 0`.
Attack power comes from fields like `zone.punch_pow_medium`, set from level/stats in the scripts.
So damage should already match the original once the inputs are right. Check on PC:
- the `random(n)` native returns an integer in 0..n-1 (Flash semantics). If it returns a float or 0..n,
  crit/elemental rates are wrong (a float never equals 0 -> no crits; 0..n -> slightly off).
- the level-40 dev test character is gone (fresh save), since that was the likely cause of high magic damage.
- spot test: level 1 Red Knight light hit on first Barbarian vs a PC playthrough video/number.

## 2. Weapon pickup prompt
Pickup is script code: `f_PickupItem(zone)` and `f_GetPickup(zone, u_temp)` (both check
`hud_pt.p_type == 11` for the Alien). The original shows the weapon stat bubble and waits for a button,
so auto-pickup on Vita means the engine is feeding the wrong input state. Check in the dumps which
input native/button flag f_PickupItem tests (likely a "pressed this frame" flag or a button id from the
exe controller map) and whether our input native reports that button held every frame or maps it to
an always-true value. Fixing the native should bring back the prompt and stat preview with no new UI.

## 3. Sandwich ("beefy") doors
Sandwich use is `f_Punch` case 11: `if (zone.beefies > 0 and !zone.beefy) { zone.beefies -= 1; ... }`,
then the player is in `zone.beefy` form. Beefy doors show text "You aren't beefy enough to break this door!".
Places that need beefy form (fan guide Gameplay/Items.md, Levels/*):
- Industrial Castle: the beefy door
- Tall Grass Field: boulder before the cliff (opens a cave; then bombs on the cracked boulder -> Burly Bear orb)
- Lava World: volcano
None of these are in the cut-short level files, so this is an engine bug, not missing data.
Check: the beefy timer (frame/time native), the door's hit test against `zone.beefy`, and that the
beefy hit uses the right attack type. Sandwiches cannot be used in Abandoned Mill, Rapids Ride, Catfish (by design).

## 4. Item switching rules (original)
Cannot switch items at 0 health or during the sandwich animation. "NADA" shows when bombs/potions/sandwiches
run out. You start with nothing selected until you switch.

## 5. Weapon stats table for parity checks
/mnt/project-files/research/parity/weapons.tsv: 84 weapons with Str/Mag/Def/Agi, crit/special,
level required, where to get it (from Hazard's guide). Use to check the pickup stat bubble and the
Blacksmith/shop lists once they show on the Vita.

## Still needs the PC
Everything above needs the Source Code dumps and the engine source (cc-vita on the PC; the cloud copy is
an old prototype). Also on the PC: upload LEVEL61-65, the six cut-short levels (Jah's extraction step),
sky/heat screen effects, blank online-lobby strings, and the "right or left bumper" wording (Jah to decide).
