# Castle Crashers on PS Vita: feasibility (2026-10-12)

## Assumptions
- Jah owns a legitimate copy (assumed: Steam PC version).
- Vita is homebrew-enabled (HENkaku/Enso) and VitaSDK is the toolchain.
- Nothing copyrighted gets distributed: any port reads the user's own game files at runtime.

## What exists today
- No official or community Vita port was found in searches.
- Official platforms: Xbox 360 (2008), PS3 (2010), PC/Mac via Steam (2012), PS4/Xbox One, Switch (Remastered, 2019). No Android or iOS release (from my knowledge; searches turned up nothing to contradict it).
- The engine is The Behemoth's proprietary C++ engine. No source, no open reimplementation found.

## Routes, ranked

| Route | Verdict | Why |
|---|---|---|
| .so-loader (Android wrapper) | Not possible | Needs an Android ARM build; none exists. |
| Switch binary loader | Not realistic | ARM64 binary, Vita is 32-bit ARMv7; Nintendo OS calls everywhere. |
| PS3 / x86 PC emulation on Vita | Not possible | Vita CPU (4x Cortex-A9 @ 444 MHz, 512 MB RAM) is far too weak. |
| Streaming (Moonlight/Sunshine from Jah's PC) | Works now | vita-moonlight exists; game runs on PC, Vita is the screen + controls. Needs a PC and good Wi-Fi. |
| Clean-room native engine reading the PC data files | Possible, very large | OpenMW/OpenXcom-style. Requires reverse engineering asset formats (archives, Flash-derived vector/sprite animation, levels, scripts) and rewriting gameplay. Months to years. |

Hardware fit for a native engine: 512 MB RAM + 128 MB VRAM is close to Xbox 360 memory, but the CPU is much weaker, so assets would likely need pre-baking (rasterised sprite atlases, compressed audio) by a PC-side converter run on Jah's own files.

## Multiplayer
- Online via Steam/PSN: not available to homebrew. Steam lobbies cannot be reached from Vita.
- Local couch co-op on one Vita: possible with Bluetooth controllers via ds34vita / VitaControl plugins (up to 4 pads). Best fit for Castle Crashers' 4-player design. A PS TV makes this nicer.
- Vita-to-Vita: homebrew can use plain UDP over Wi-Fi (simplest) or the ad-hoc APIs in VitaSDK (`sceNetAdhocMatching`). Would need our own netcode in a native engine.
- Streaming route: one Vita client per PC session (inferred); other players would use controllers on the PC.

## Recommendation
1. Play it on Vita now: Sunshine on the PC + Moonlight on the Vita.
2. Native port as the long-term project, in stages:
   a. Inventory the PC install (file list, sizes, magic bytes) from Jah's own copy.
   b. Write a PC-side extractor/converter for the archive format.
   c. Build a minimal VitaSDK renderer that shows one converted level and one animated character.
   d. Then movement/combat for one level, then local co-op with paired pads, then Wi-Fi UDP netplay.

## Sources
- https://docs.vitasdk.org/adhoc__matching_8h.html
- https://consolemods.org/wiki/Vita:Ds34vita
- https://github.com/hydr8gon/VitaControl
- https://www.thegamer.com/castle-crashers-remastered-switch-ps4-multiplayer-dlc/
- https://shacknews.com/article/59670/castle-crashers-headed-to-ps3
