// Custom characters painted on The Behemoth's Painter Boss Paradise template (an optional add-on;
// the game is unchanged without one). The PC DLC's template is a 2048x2048 painting.png with a
// box per body part. Each becomes a character of its own on character select, after the 31 (and
// Paint Jr.), with its own save record. It fights like a base character (its magic, starting
// weapon and colour), Green Knight unless pbp.xml says otherwise.
//
// ux0:data/CastleCrashers/custom/, each optional (up to CC_CUSTOM_MAX in all, in this order):
//   painting.png, pbp.xml, stencil.png    the first one, as before
//   <name>/painting.png, pbp.xml, ...     one more per folder, by folder name (not animal/: the
//                                         custom orb, custom_animal.h)
//   Steam Workshop item files (workshop_pak.h) in custom/ or custom/workshop/
// pbp.xml is the PC's <character displayname="..."/>; base="N" picks the base character 1..31
// (Vita only: the PC's chartype/magictype aren't read). stencil.png is the blank template
// (pbp_template from the PC game's pbp_template.pak): pixels still equal to it (guide lines) are
// cleared, as the PC does; custom/stencil.png serves every painting without its own.
//
// Memory: each painting is made into a half-size (1024x1024) copy once, kept in custom/.cache/
// (remade when its file changes). Only the small boxes (HUD face, portrait, weapon) stay loaded
// for every character; the whole painting is read back from the cache while a player of it is
// drawn, at most CC_CUSTOM_RESIDENT at a time.
#ifndef CC_CUSTOM_CHAR_H
#define CC_CUSTOM_CHAR_H

#include <stdio.h>

#include "as.h"
#include "swf.h"

#define CC_CHARACTERS 31          // the game's own
#define CC_CUSTOM_P_TYPE 33       // the first custom character's number (32 is Paint Jr., paint_jr.h)
#define CC_CUSTOM_MAX 4           // 33..36 (Jah: 4 slots; weapon_look.c has room for 64 character numbers)
#define CC_CUSTOM_RESIDENT 4      // whole paintings in video memory at once (4 MB each)

// Looks in dir (ending in '/') for paintings; the number loaded. Small boxes become Bitmaps that
// swf_bitmap_loaded (if set) turns into the platform's textures.
int cc_custom_load_all(const char *dir);
// The platform's way to make a whole painting's texture straight from its cache file (at the
// RGBA pixels; bm->w, h set), and to free one. Without the first the pixels are read into
// bm->rgba and given to swf_bitmap_loaded (hosttest: kept loaded).
extern int (*cc_custom_texture_from_file)(Bitmap *bm, FILE *f);
extern void (*cc_custom_free_texture)(Bitmap *bm);
// Once per drawn frame, before drawing: a painting drawn this frame or the last is never let go.
void cc_custom_new_frame(void);
// Off in a Wi-Fi game (lockstep: the others may have no painting, or another one).
void cc_custom_set_active(int on);
int cc_custom_count(void);            // custom characters loaded (0 when off)
int cc_custom_slot(int p_type);       // 0.. for a custom character's number, else -1
int cc_custom_base(int p_type);       // the character it fights like, 0 if p_type isn't one
const char *cc_custom_name(int slot); // pbp.xml's displayname, or "CUSTOM"

// The game script changes for the characters after the 31 (character count, colour; with a
// painting, its spawn); call before the VM runs MAIN.swf (cc_host_init does).
void cc_custom_init(AsVm *vm);

// A part clip of PLAYER.swf (its sprite, 0-based frame) on a player of p_type: the painted box to
// draw instead of its own frame, or NULL to draw it as usual.
const BitmapFill *cc_custom_part(const Movie *m, const Sprite *s, int frame, int p_type);

// The painting's weapon for weapon looks (weapon_look.h): whether s is PLAYER.swf's weapon part
// clip, and the first custom character's painted weapon in its coordinates (NULL without one, or off)
int cc_custom_is_weapon_part(const Movie *m, const Sprite *s);
const BitmapFill *cc_custom_weapon(void);

// Clips outside PLAYER.swf that show a character's face, found by their instance names
// (as_render.c): the HUD's face and the character select portrait of custom character p_type.
enum { CC_ICON, CC_PORTRAIT };
const BitmapFill *cc_custom_named(int which, int p_type);

#endif
