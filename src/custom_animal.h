// Custom animal orb (an optional add-on; the game is unchanged without one): a 30th pet after
// the game's 29, drawn from a picture of the player's own. It is owned in every profile and waits
// in the Animal Ark next to the Golden Whale's spot; walk up to it to take it, the trash can sends
// it back, as with the others.
//
// ux0:data/CastleCrashers/custom/animal/, as with a custom character:
//   painting.png  the pet painted on the animal template (tools/custom_animal/make_template.py:
//                 1024x1024, paint inside the box, background left transparent)
//   stencil.png   the blank template: pixels still equal to it (its guide lines) are cleared
// or instead pet.png, any picture with a transparent background. Either is trimmed to what's
// painted and drawn the size of a pet. Optionally custom/animal/pet.xml, one tag:
//   <animal displayname="Blobby" description="Blobby spits slime at enemies."
//           strength="0" defense="0" agility="0" magic="0" like="0"
//           every="10" heal="5" gold="1" size="1" facing="right" outline="0"
//           wagpivot="300,700" wag="12" wagspeed="0.8"/>
//   description  the Vet's chalkboard (three short lines, wrapped here)
//   strength...  stat points it gives, as the stat pets do (-9..9)
//   like         another pet (1-29, the game's animal numbers) whose ability it also has
//   every        its own ability: every this many seconds it...
//   heal         ...heals its owner this percent of their health (the Troll pet's is 1)
//   gold         ...drops this many coins (1-10; the Golden Whale's drops 1)
//   xp           percent more experience for its owner (Giraffey's is 10)
//   size         how big it is drawn, 1 = a pet's usual size
//   facing       "right" (default) or "left": the way the picture looks
//   outline      a dark line this many texels wide drawn around it (1-8; the picture is boxed
//                down to 128 texels, so a painting's own thin outline vanishes at a pet's size)
//   wagpivot     "x,y": where the tail joins the body, in the picture's pixels (template pixels
//                for painting.png). The tail is its own layer, tail.png on the template (or
//                pet_tail.png, the size of pet.png), drawn behind the pet and swung about this
//   wag          how far the tail swings each way, in degrees (default 12)
//   wagspeed     seconds for one swing there and back (default 0.8)
#ifndef CC_CUSTOM_ANIMAL_H
#define CC_CUSTOM_ANIMAL_H

#include "as.h"
#include "swf.h"

#define CC_ANIMALS 29              // the game's own
#define CC_CUSTOM_ANIMAL 30        // the custom pet's animal number

// Looks in dir (ending in '/') for pet.png; 1 if one was loaded. The picture becomes a Bitmap
// that swf_bitmap_loaded (if set) turns into the platform's texture, so call after setting it.
int cc_animal_load(const char *dir);
int cc_animal_type(void);              // CC_CUSTOM_ANIMAL while loaded and active, else 0
const char *cc_animal_name(void);

// Off in Wi-Fi games: both machines run the same frames, and the other one may have no pet (or
// another one). A player whose pet is the custom one then has none.
void cc_animal_set_active(int on);

// The game script changes (Ark, ability, Vet); call before the VM runs MAIN.swf (cc_host_init).
void cc_animal_init(AsVm *vm);

// The picture to draw instead of clip mc (as_render.c), placed by *box (a matrix from the unit
// square to mc's own coordinates), or NULL to draw mc as usual. With a tail layer, *tail is set
// too: draw it first, placed by *tail_box (wagging with the game's frame count, tick).
const BitmapFill *cc_animal_art(AsMc *mc, uint32_t tick, Mat *box, const BitmapFill **tail, Mat *tail_box);

// The Vet's chalkboard lines for the custom pet (text field vm->text_field, string n), or NULL.
const char *cc_animal_text(AsVm *vm, int n);

#endif
