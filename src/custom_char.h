// Custom characters painted on The Behemoth's Painter Boss Paradise template (an optional add-on;
// the base game is untouched without one). The PC DLC's template is a 2048x2048 painting.png with
// a box per body part; here a painting replaces one of the 31 characters' looks, as the PC's
// "replacement" characters do, and keeps that character's moves and magic.
//
// ux0:data/CastleCrashers/custom/<character>/painting.png (+ the PC's pbp.xml for its name), where
// <character> is green, red, blue, orange, gray or a character number 1..31. With
// custom/stencil.png (the blank template, pbp_template from the PC game's pbp_template.pak), the
// guide lines left in the painting are cleared, like the PC game does with its stencil.
#ifndef CC_CUSTOM_CHAR_H
#define CC_CUSTOM_CHAR_H

#include "swf.h"

// Looks in dir (ending in '/') for a painting; 1 if one was loaded. The painting becomes a
// Bitmap that swf_bitmap_loaded (if set) turns into the platform's texture.
int cc_custom_load(const char *dir);
int cc_custom_p_type(void);          // the character it replaces, 0 = none
const char *cc_custom_name(void);    // pbp.xml's displayname, or the folder name

// A part clip of PLAYER.swf (its sprite, 0-based frame) on a player of p_type: the painted box to
// draw instead of its own frame, or NULL to draw it as usual.
const BitmapFill *cc_custom_part(const Movie *m, const Sprite *s, int frame, int p_type);

#endif
