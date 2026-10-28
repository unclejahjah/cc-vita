// Paint Jr., the PC game's 32nd character (Painter Boss Paradise update), and the FRESH look, as
// add-ons: the original MAIN, MENU and PLAYER.swf stay as they are, and every player runs the
// original PLAYER.swf. Files from the user's own PC copy (tools/paintjr/make_paintjr.py):
//   game/PJPARTS.swf     his pieces of the part clips from the PC's player file (player_new.pak):
//                        helmet, body and arm parts, shields and his pencil (weapon 86), drawn
//                        instead of the part clips' own frame (2.5 MB of meshes: the whole PC file
//                        needs 73, too much beside PLAYER.swf's 33).
//   game/PAINTJR.swf     his three script functions from the PC's MAIN.swf (f_PaintJrMagicSplash,
//                        f_ProjectileHitPaintJrSplash, f_ProjectilePhase); his spell's art and
//                        sounds are already in the PC's FX.swf and SOUNDS.swf (magic type 33).
//   game/FRESHnn.swf     the FRESH look: the PC's version of frame nn of every part clip.
// Without the first two the game has its 31 characters as before.
#ifndef CC_PAINT_JR_H
#define CC_PAINT_JR_H

#include "as.h"
#include "swf.h"

#define CC_PAINTJR_P_TYPE 32      // his character number (helmet, shield, magic type 33)
// LOBBY.swf's portrait_top/mid/bot: character n shows frame n + 3, empty for him; the PC's newer
// portraits ("player_new", frames 104-135 for characters 1-32) end with his (0-based first one)
#define CC_NEW_PORTRAITS 103

// Whether the files are there (main.c / hosttest look before the game starts): FRESH01.swf
// (fresh_files), PJPARTS.swf (pj_parts) and PAINTJR.swf (paint_jr).
void cc_paintjr_set_available(int fresh_files, int pj_parts, int paint_jr);
int cc_paintjr_p_type(void);          // CC_PAINTJR_P_TYPE when available (and active), else 0
void cc_paintjr_set_active(int on);   // off in a Wi-Fi game (the others may not have his files)
int cc_fresh_available(void);         // the FRESHnn.swf files are there
// The app's way to keep a file loaded for the game: the part files' sprites are held on to
extern void (*cc_movie_keep)(Movie *m);

// The FRESH look (Help & Options' Settings): the players' part clips draw the PC's redrawn
// version of the frame they show (FRESHnn.swf, loaded as they're first needed) and character
// select shows LOBBY.swf's newer portraits. Off in Wi-Fi games (the others may not have them).
void cc_fresh_set(int on);
int cc_fresh(void);
// A part clip of PLAYER.swf (its sprite, 0-based frame) on a player of p_type: 1 and the frame of
// PJPARTS.swf or FRESHnn.swf to draw instead (his pieces, the pencil for anyone holding weapon 86,
// the FRESH look's), else 0
int cc_paintjr_part(const Movie *m, const Sprite *s, int frame, int p_type, Movie **mv, const Frame **fr);
// Whether that sprite is one of the part clips at all (cheap: asked for every clip drawn)
int cc_paintjr_is_part(const Movie *m, const Sprite *s);
// LOBBY.swf portrait_top/mid/bot on frame (0-based) of the character frame + 2: the frame to draw
// instead, or -1 (Paint Jr.'s always, everyone's with the FRESH look)
int cc_portrait_frame(int frame, int nframes);

// The script changes (magic); call before the VM runs MAIN.swf (cc_host_init does).
void cc_paintjr_init(AsVm *vm);

#endif
