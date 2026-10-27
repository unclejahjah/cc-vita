// Help & Options' books, as the original shows them over PAUSE.swf's (and MENU.swf's) book
// art: How to Play (the "howtoplay" clip's seven pages), Controls (a PS Vita in the game's
// style in place of the original's controller) and Settings (music and sound effects
// volume on the "volume" bars, and Auto Potion On / Off). Platform-free like pause_menu.c:
// the app feeds it buttons and draws what it builds; hosttest renders it.
#ifndef CC_HELP_BOOK_H
#define CC_HELP_BOOK_H

#include "render.h"

enum { HB_HOWTO, HB_CONTROLS, HB_SETTINGS, HB_ACHIEVEMENTS };   // ACHIEVEMENTS: from Information
// buttons for help_book_update
enum { HB_UP = 1, HB_DOWN = 2, HB_LEFT = 4, HB_RIGHT = 8, HB_PREV = 16, HB_NEXT = 32, HB_BACK = 64, HB_RESET = 128 };
#define HB_HOWTO_PAGES 7
#define HB_VOLUME_STEPS 10

typedef struct {
    int book, page;            // page: How to Play's page, 0..6
    int row;                   // Settings: 0 music, 1 sound effects, 2 auto potion; Controls: the chosen button
    int volume[2];             // 0..HB_VOLUME_STEPS
    int auto_potion;           // 1: a hit that would end you drinks a health potion instead
    int changed;               // the settings or buttons (cc_bind) changed since the book opened
    float time;                // seconds open (the chosen row pulses)
    float turn;                // seconds left of a page turn
    int turn_dir;              // 1 forward, -1 back
    float alpha;               // fades in when opened
    unsigned achieved;         // Achievements: bit id set when unlocked (cc_achievements)
} HelpBook;

void help_book_open(HelpBook *hb, int book, int music, int sfx, int auto_potion);
// Controls: up/down choose a button, left/right change the action on it (the button that had
// that action takes this one's), HB_RESET puts the original layout back. It sets cc_bind.
// One frame (dt seconds) with the buttons pressed this frame. Returns 0 once Back closes
// it. *sound: -1 none, 0 the move sound, 1 the select one, 2 a page turn.
int help_book_update(HelpBook *hb, unsigned pressed, float dt, int *sound);
// The book and its page through k (stage pixels -> screen pixels); the game font
// (as_render_set_font) draws the words. pause_swf: PAUSE.swf or MENU.swf (both carry the art).
void help_book_build(const HelpBook *hb, Movie *pause_swf, float k, DrawList *out);

// A page of the same book for other menus (the Wi-Fi lobby): a title, lines down the middle
// (one chosen, pulsing like Settings' rows; the rest white), a big code in boxes (one box
// chosen), a small note near the bottom, and the Cross / Circle prompts.
#define HB_PAGE_LINES 8
typedef struct {
    const char *title;
    const char *line[HB_PAGE_LINES];
    int nlines, chosen;        // chosen: -1 none
    const char *code;          // NULL none; '-' in it is a gap
    int code_chosen;           // -1 none
    int code_line;             // the code goes where this line would be
    const char *note;
    const char *accept, *back; // the prompts' words (NULL: no prompt)
    float time, alpha;
    float turn;                // seconds left of a page turn over it (0 none; HB_TURN_SECONDS long)
    int turn_dir;              // 1 forward, -1 back
} HbPage;
#define HB_TURN_SECONDS 0.45f
void help_book_build_page(const HbPage *pg, Movie *pause_swf, float k, DrawList *out);

#endif
