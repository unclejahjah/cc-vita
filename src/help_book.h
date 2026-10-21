// Help & Options' books, as the original shows them over PAUSE.swf's (and MENU.swf's) book
// art: How to Play (the "howtoplay" clip's seven pages), Controls (a PS Vita in the game's
// style in place of the original's controller) and Settings (music and sound effects
// volume on the "volume" bars). Platform-free like pause_menu.c: the app feeds it buttons
// and draws what it builds; hosttest renders it.
#ifndef CC_HELP_BOOK_H
#define CC_HELP_BOOK_H

#include "render.h"

enum { HB_HOWTO, HB_CONTROLS, HB_SETTINGS };
// buttons for help_book_update
enum { HB_UP = 1, HB_DOWN = 2, HB_LEFT = 4, HB_RIGHT = 8, HB_PREV = 16, HB_NEXT = 32, HB_BACK = 64 };
#define HB_HOWTO_PAGES 7
#define HB_VOLUME_STEPS 10

typedef struct {
    int book, page;            // page: How to Play's page, 0..6
    int row;                   // Settings: 0 music, 1 sound effects
    int volume[2];             // 0..HB_VOLUME_STEPS
    int changed;               // the volumes moved since the book opened
    float time;                // seconds open (the chosen row pulses)
    float turn;                // seconds left of a page turn
    int turn_dir;              // 1 forward, -1 back
    float alpha;               // fades in when opened
} HelpBook;

void help_book_open(HelpBook *hb, int book, int music, int sfx);
// One frame (dt seconds) with the buttons pressed this frame. Returns 0 once Back closes
// it. *sound: -1 none, 0 the move sound, 1 the select one, 2 a page turn.
int help_book_update(HelpBook *hb, unsigned pressed, float dt, int *sound);
// The book and its page through k (stage pixels -> screen pixels); the game font
// (as_render_set_font) draws the words. pause_swf: PAUSE.swf or MENU.swf (both carry the art).
void help_book_build(const HelpBook *hb, Movie *pause_swf, float k, DrawList *out);

#endif
