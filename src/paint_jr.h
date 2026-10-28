// Paint Jr., the PC game's 32nd character (Painter Boss Paradise update), as an add-on: the
// original MAIN, MENU and PLAYER.swf stay as they are. Two files from the user's own PC copy
// make him (tools/paintjr/make_paintjr.py):
//   game/PLAYER_NEW.swf  the PC's player file: every part clip has his frame (33). A player who
//                        picks him runs this file instead of PLAYER.swf (art and animations).
//   game/PAINTJR.swf     his three script functions from the PC's MAIN.swf (f_PaintJrMagicSplash,
//                        f_ProjectileHitPaintJrSplash, f_ProjectilePhase); his spell's art and
//                        sounds are already in the PC's FX.swf and SOUNDS.swf (magic type 33).
// Without both files the game has its 31 characters as before.
#ifndef CC_PAINT_JR_H
#define CC_PAINT_JR_H

#include "as.h"

#define CC_PAINTJR_P_TYPE 32      // his character number (helmet, shield, magic type 33)
// LOBBY.swf's portrait_top/mid/bot: character n shows frame n + 3, empty for him; the PC's newer
// portraits ("player_new", frames 104-135 for characters 1-32) end with his (0-based first one)
#define CC_NEW_PORTRAITS 103

// Whether the files are there (main.c / hosttest look before the game starts): PLAYER_NEW.swf
// (new_player) and PAINTJR.swf (paint_jr).
void cc_paintjr_set_available(int new_player, int paint_jr);
int cc_paintjr_p_type(void);          // CC_PAINTJR_P_TYPE when available, else 0
int cc_fresh_available(void);         // PLAYER_NEW.swf is there

// The FRESH look (Help & Options' Settings): every player runs PLAYER_NEW.swf (the PC's redrawn
// knights) and character select shows LOBBY.swf's newer portraits. Takes effect at the next
// spawn; off in Wi-Fi games (the files' scripts differ a little, and every Vita must run the same).
void cc_fresh_set(int on);
int cc_fresh(void);
// LOBBY.swf portrait_top/mid/bot on frame (0-based) of the character frame + 2: the frame to draw
// instead, or -1 (Paint Jr.'s always, everyone's with the FRESH look)
int cc_portrait_frame(int frame, int nframes);

// The script changes (spawn, magic); call before the VM runs MAIN.swf (cc_host_init does).
void cc_paintjr_init(AsVm *vm);

#endif
