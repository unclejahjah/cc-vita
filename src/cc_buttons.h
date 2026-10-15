// PlayStation button prompts made from the game's own button art (see cc_buttons.c).
#ifndef CC_BUTTONS_H
#define CC_BUTTONS_H

#include "swf.h"

enum { CC_BTN_CROSS, CC_BTN_CIRCLE, CC_BTN_SQUARE, CC_BTN_TRIANGLE };
enum { CC_BTN_STYLE_COLOURED, CC_BTN_STYLE_DARK };   // the game's coloured balls / PlayStation's dark buttons

// Redraws a movie's exported button clips (A/B/X/Y balls, LT/RT triggers) as
// Cross/Circle/Square/Triangle and L/R. Call after swf_load, before tessellating.
// Returns how many clips it changed.
int cc_buttons_playstation(Movie *m, int style);
// MENU.swf's title: the painted "Steam Edition" badge covered and "Vita Edition" drawn in
// its place (fills added to the logo). 1 if this was that file. Call before tessellating.
int cc_menu_vita_badge(Movie *m);

#endif
