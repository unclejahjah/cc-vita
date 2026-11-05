// PlayStation button prompts made from the game's own button art (see cc_buttons.c).
#ifndef CC_BUTTONS_H
#define CC_BUTTONS_H

#include "swf.h"

enum { CC_BTN_CROSS, CC_BTN_CIRCLE, CC_BTN_SQUARE, CC_BTN_TRIANGLE };
enum { CC_BTN_STYLE_COLOURED, CC_BTN_STYLE_DARK, CC_BTN_STYLE_GREY };   // the game's coloured balls / dark / grey PlayStation buttons

// Redraws a movie's exported button clips (A/B/X/Y balls, LT/RT triggers) as
// Cross/Circle/Square/Triangle and L/R. Call after swf_load, before tessellating.
// Returns how many clips it changed.
int cc_buttons_playstation(Movie *m, int style);
// MENU.swf's title: the painted "Steam Edition" badge covered and "Vita Edition" drawn in
// its place (fills added to the logo). 1 if this was that file. Call before tessellating.
// cc_menu_badge_outline set: the words get a dark rim (no cloud behind them under the PC's scenes).
int cc_menu_vita_badge(Movie *m);
extern int cc_menu_badge_outline;
// The "BETA" sticker for the Multiplayer button (CC_BETA_STICKER in cc_host.h), built once from
// MENU.swf's font (1 if m was that file); then cc_menu_beta_sticker gives it (NULL before).
int cc_menu_beta_sticker_build(const Movie *m);
Shape *cc_menu_beta_sticker(void);

// A PS Vita in the game's drawing style for the controls page (body 300 pixels wide, centred
// on 0,0, in twips). spots: where each control is, in the same space; the face and shoulder
// buttons are left for the caller to draw with the game's button clips.
enum { CC_VITA_L, CC_VITA_R, CC_VITA_DPAD, CC_VITA_LSTICK, CC_VITA_RSTICK, CC_VITA_TRIANGLE, CC_VITA_CIRCLE,
       CC_VITA_CROSS, CC_VITA_SQUARE, CC_VITA_SELECT, CC_VITA_START, CC_VITA_PS, CC_VITA_SPOTS };
int cc_vita_drawing(Shape *s, float spots[CC_VITA_SPOTS][2]);
// The Vita's D-pad with its left or right arm lit (page-turn prompts), 34 pixels across,
// centred on 0,0, in twips.
int cc_dpad_icon(Shape *s, int right);

#endif
