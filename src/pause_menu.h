// The in-game pause menu as castle.exe draws it (its PauseMenu, 2012 build), over PAUSE.swf's
// art. Platform-free: the Vita app feeds it buttons and draws what it builds; hosttest renders it.
#ifndef CC_PAUSE_MENU_H
#define CC_PAUSE_MENU_H

#include "render.h"

// pages: the menu's own, the two books the app draws itself, and Help & Options' list and
// the help book (help_book.c) the app shows from it
enum { PG_MAIN, PG_CONTROLS, PG_ACHIEVEMENTS, PG_INFO, PG_CONFIRM, PG_HELP, PG_BOOK };
// buttons for pause_menu_update
enum { PM_UP = 1, PM_DOWN = 2, PM_ACCEPT = 4, PM_BACK = 8 };
// what pause_menu_update asks for
enum { PM_OPEN, PM_RESUME, PM_TO_MAP, PM_TO_TITLE };

typedef struct {
    int page, sel;
    int confirm;               // the "Are You Sure?" page is for: 0 Exit To Map, 1 Exit to Main Menu
    int fade, act, back_page;  // fade 1 in, -1 out (then act runs), 0 still
    float alpha, flash, time;
    int flash_on;
    float clock;               // the backdrop's animation, in game frames (30 a second)
    int on_map;                // opened on the world map: no Exit To Map line
    int help_only;             // opened from the title's Help & Options: Back on it closes
    int book, book_new;        // PG_BOOK: which help book (HB_*), and 1 until the app opens it
} PauseMenu;

void pause_menu_open(PauseMenu *pm, int on_map);
void pause_menu_open_help(PauseMenu *pm);    // the title's Help & Options: its list alone
void pause_menu_book_closed(PauseMenu *pm);  // the help book closed: back to the list
// One frame (dt seconds) with the buttons pressed this frame. *sound: -1 none, 0 the move
// sound, 1 the select one (PAUSE.swf's sound_toggle_b / sound_player_select).
int pause_menu_update(PauseMenu *pm, unsigned pressed, float dt, int *sound);
// The backdrop (PAUSE.swf's level frame), the page's words and its button prompts, through
// k (stage pixels -> screen pixels). The book pages get only the backdrop. Needs the game
// font (as_render_set_font) for the words.
void pause_menu_build(const PauseMenu *pm, Movie *pause_swf, float k, DrawList *out);

#endif
