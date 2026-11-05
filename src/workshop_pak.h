// Steam Workshop characters, read as the PC game keeps them (steamapps/workshop/content/204360/
// <item id>/<name>): a "PKTB" file holding the item's painting.png and pbp.xml, each scrambled
// the way the game's own .pak files are (Blowfish, keyed by the entry and its size) and
// compressed. Read here as they are, so a Workshop file can be copied over unchanged.
#ifndef CC_WORKSHOP_PAK_H
#define CC_WORKSHOP_PAK_H

#include <stddef.h>
#include <stdint.h>

// Whether path looks like a Workshop item file (its header)
int cc_workshop_is(const char *path);
// Its painting.png (malloc'd, *png_len bytes) and pbp.xml text into xml (NUL-ended, "" if none);
// NULL if it can't be read.
uint8_t *cc_workshop_read(const char *path, size_t *png_len, char *xml, size_t xmln);

#endif
