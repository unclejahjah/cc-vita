# Trophies for Vita homebrew

## Bottom line
Real NP trophies *can* fire in unsigned homebrew, but only with a user-installed plugin (**NoTrpDrm**) plus a trophy pack (`trophy.trp`) and NP communication ID/signature that are normally Sony-issued. Homebrew either reuses an existing official game's pack/IDs (legally/ethically murky, and wrong trophy names) or builds a custom TRP that only works because signature checks are patched out. Such trophies won't legitimately sync to PSN (and syncing them risks account flags). Ship an **in-game achievements screen + toast popup** as the default; optionally add a NoTrpDrm path later.

## What exists
- **sceNpTrophy** (VitaSDK `psp2/np/trophy.h` style API: init, create context with NP comm ID + signature, register, unlock). Needs `sce_sys/trophy/<NPWRxxxxx_00>/TROPHY.TRP` in the app and a valid NP Communication ID + 160-byte comm signature. Unsigned homebrew lacks these, so contexts fail on stock HEN. (Details from general knowledge; verify against VitaSDK headers.)
- **NoTrpDrm** (Rinnegatamante) - https://github.com/Rinnegatamante/NoTrpDrm - "enables official trophies pack usage with sceNpTrophy for homebrews"; disables the NP Communication Signature check and the TRP file signature check. Installed under `*main` in `ux0:tai/config.txt`. Tested on retail.
- **NoTrophyMsg** (TheOfficialFloW VitaTweaks) - hides the "trophies can't be earned" message; cosmetic only. https://github.com/TheOfficialFloW/VitaTweaks/releases/tag/NoTrophyMsg
- Plugin index: https://consolemods.org/wiki/Vita:Homebrew_Plugins_List
- Several Rinnegatamante ports reportedly use NoTrpDrm for optional trophies (unverified which; check VitaDB).
- Trophy *unlockers* / editors exist for official games (e.g. via save/trophy-data editing); not relevant for awarding ours.

## Uncertainty
- Building a custom TRP (packing/encryption tools, which comm ID to use) is not documented in sources found; likely requires reusing an official game's NP comm ID. Not verified.
- PSN sync behavior with fake trophies unverified - assume risky.

## Recommended implementation
1. Achievements table in the save file (bitmask + progress counters, e.g. dig count for Treasure Hunter).
2. Toast popup top-right (icon + name, ~3 s, queue), styled like the Vita system popup.
3. Achievements menu from the title/pause screen (locked/unlocked, % done).
4. Thin `achievement_unlock(id)` layer so a NoTrpDrm/sceNpTrophy backend can be added behind a build flag later.
