// Custom character painted on The Behemoth's Painter Boss Paradise template (an optional add-on;
// the game is unchanged without one). The PC DLC's template is a 2048x2048 painting.png with a
// box per body part. Here it becomes a character of its own: a 32nd slot on character select,
// after the 31, with its own save record. It fights like a base character (its magic, starting
// weapon and colour), Green Knight unless pbp.xml says otherwise.
//
// ux0:data/CastleCrashers/custom/painting.png, and optionally:
//   custom/pbp.xml       the PC's <character displayname="..."/>; base="N" picks the base
//                        character 1..31 (Vita only: the PC's chartype/magictype aren't read)
//   custom/stencil.png   the blank template (pbp_template from the PC game's pbp_template.pak):
//                        pixels still equal to it (guide lines) are cleared, as the PC does
#ifndef CC_CUSTOM_CHAR_H
#define CC_CUSTOM_CHAR_H

#include "as.h"
#include "swf.h"

#define CC_CHARACTERS 31          // the game's own
#define CC_CUSTOM_P_TYPE 32       // the custom character's number

// Looks in dir (ending in '/') for a painting; 1 if one was loaded. The painting becomes a
// Bitmap that swf_bitmap_loaded (if set) turns into the platform's texture.
int cc_custom_load(const char *dir);
// Off in a Wi-Fi game (lockstep: the others may have no painting, or another one).
void cc_custom_set_active(int on);
int cc_custom_p_type(void);          // CC_CUSTOM_P_TYPE once loaded, else 0
int cc_custom_base(void);            // the character it fights like
const char *cc_custom_name(void);    // pbp.xml's displayname, or "CUSTOM"

// The game script changes for the 32nd character (character count, spawn, colour); with a
// painting loaded, call before the VM runs MAIN.swf (cc_host_init does).
void cc_custom_init(AsVm *vm);

// A part clip of PLAYER.swf (its sprite, 0-based frame) on a player of p_type: the painted box to
// draw instead of its own frame, or NULL to draw it as usual.
const BitmapFill *cc_custom_part(const Movie *m, const Sprite *s, int frame, int p_type);

// Clips outside PLAYER.swf that show a character's face, found by their instance names
// (as_render.c): the HUD's face and the character select portrait.
enum { CC_ICON, CC_PORTRAIT };
const BitmapFill *cc_custom_named(int which);

#endif
