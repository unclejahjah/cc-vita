// Castle Crashers' executable-side functions for the ActionScript engine.
#ifndef CC_HOST_H
#define CC_HOST_H

#include "as.h"

// Key codes the scripts read with Key.isDown (see f_GetButtons); set by the platform.
extern uint8_t cc_keys[256];

void cc_host_init(AsVm *vm);

// A player's buttons as the game sees them, one bit each (the platform maps its pad to these);
// cc_port_keys sets them in cc_keys[] as that port's key codes (port 1..4, f_GetButtons).
enum {
    CCB_START, CCB_ACCEPT, CCB_BACK, CCB_SELECT, CCB_LEFT, CCB_UP, CCB_RIGHT, CCB_DOWN,
    CCB_WALK_LEFT, CCB_WALK_UP, CCB_WALK_RIGHT, CCB_WALK_DOWN, CCB_PUNCH1, CCB_PUNCH2, CCB_JUMP,
    CCB_PROJECTILE, CCB_SHIELD, CCB_MAGIC, CCB_L2, CCB_R2, CCB_COUNT
};
void cc_port_keys(int port, uint32_t buttons);

// Help & Options' Controls page lets the player move the six game actions between the six
// buttons. cc_bind[action] is the button it's on, numbered as the prompt icons' frames:
// 0 Square, 1 Triangle, 2 Cross, 3 Circle, 4 L, 5 R. The default (the original PlayStation
// layout) is the action's own number. Menus keep Cross to accept and Circle to go back.
enum { CC_ACT_LIGHT, CC_ACT_HEAVY, CC_ACT_JUMP, CC_ACT_ITEM, CC_ACT_BLOCK, CC_ACT_MAGIC, CC_ACTIONS };
extern int cc_bind[CC_ACTIONS];
void cc_bind_reset(void);
int cc_bind_is_default(void);

// Wi-Fi play: which port this Vita's player is (1..4; 0 when not in a Wi-Fi game) and every
// port's player name ("" for nobody). Achievements count for this Vita's player only.
// Local Game's modes, as GetGameMode() reports them
// (CC_MODE_VOLLEYBALL is the PS3's volleyball mini game, its mode 1: cc_volleyball_tick sends
// character select on to the PS3's court picker (VMENU.swf) and makes MAIN.swf play as the PS3's
// does in that mode; nothing of it is saved)
enum { CC_MODE_STORY = 0, CC_MODE_QUAFF = 1, CC_MODE_ARENA = 3, CC_MODE_VOLLEYBALL = 10 };
extern int cc_game_mode;
void cc_volleyball_tick(AsVm *vm);   // before every game tick
extern int cc_quit_requested;   // a mini game called QuitTo/ChangeMovie: back to the title
extern int cc_net_port;
extern char cc_net_names[4][32];

// Help & Options' Auto Potion: a hit that would end a player whose selected item is the
// health potion drinks one instead. The original does that (whatever's selected) only while
// another player is still up (f_CheckHealthPots: active_players > 1); on, the last one up
// drinks it too. In a Wi-Fi game it's the host's choice, the same on every Vita.
void cc_set_auto_potion(AsVm *vm, int on);

// The executable-side settings the scripts read with GetFlashGlobal.
AsVal cc_flash_global(AsVm *vm, const char *name);
void cc_set_flash_global(AsVm *vm, const char *name, AsVal v);
void cc_native_init(AsVm *vm);   // native versions of hot game functions (cc_native.c); called by cc_host_init
// For files shown without MAIN.swf (the title menu): the _root functions their prompt icons
// call (f_GetButtonArt), defined on that root.
void cc_native_root_functions(AsVm *vm, AsObj *root);
// The title's menu panel (MENU.swf "buttonchoices"), trimmed to the Vita's three items, at x, y
// (stage px); NULL if the file has no panel. cc_menu_highlight enlarges item 0..2.
// CC_MENU_WIFI (the original's online button, as Wi-Fi Game) only with cc_menu_wifi set.
enum { CC_MENU_PLAY, CC_MENU_OPTIONS, CC_MENU_QUIT, CC_MENU_WIFI, CC_MENU_ITEMS };
extern int cc_menu_wifi;
AsMc *cc_menu_attach(AsVm *vm, float x, float y);
// Local Game's mode menu (MENU.swf "modemenu") on _root at (x, y), its hand on Castle
// Crashers; cc_modemenu_point moves the hand from one item to another (0 Castle Crashers,
// 1 Arena, 2 All You Can Quaff, 3 Volleyball: under Castle Crashers, where the right page's
// scribbles were); cc_modemenu_tick eases the hand down to Volleyball and back
#define CC_MODE_ITEMS 4
void cc_modemenu_tick(AsVm *vm, AsMc *menu);
AsMc *cc_modemenu_attach(AsVm *vm, float x, float y);
void cc_modemenu_point(AsVm *vm, AsMc *menu, int from, int to);
void cc_menu_highlight(AsVm *vm, AsMc *panel, int item);
// The power button that stands for Quit, as triangles in screen pixels through screen (stage
// twips -> screen); lit when chosen. Returns the vertices written (up to max; it needs about 1200).
typedef struct { float x, y; uint8_t rgba[4]; } CcVert;
int cc_menu_power_button(AsMc *panel, Mat screen, CcVert *out, int max);
// The "BETA" sticker on the Multiplayer button: set CC_BETA_STICKER to 0 to take it off.
// cc_menu_beta_place gives its matrix (sticker twips -> screen, through screen as above);
// 0 when there's nothing to draw. The shape is cc_menu_beta_sticker (cc_buttons.h).
#define CC_BETA_STICKER 1

// The public games list's server (tools/lobby/, src/lobby.h): "" for none.
// ux0:data/CastleCrashers/lobby.txt overrides it.
// (plain http: the https one couldn't be reached from Jah's Vita, 2026-10-27; the list is no secret)
#define CC_LOBBY_URL "http://cc-vita-lobby.castlecrashersvita.workers.dev"
int cc_menu_beta_place(AsMc *panel, Mat screen, Mat *out);
// The chosen button's highlight frame: hidden in the panel, the app draws it last (over the
// power button) by rendering it on its own.
AsMc *cc_menu_highlight_clip(AsMc *panel, Mat *m);

// Save data per controller port (ReadStorage/WriteStorage); dirty when the game wrote to it.
double *cc_storage(int port);
void cc_own_dlc_characters(void);   // the DLC characters are unlocked in every profile
void cc_own_dlc_items(void);        // and the DLC packs' weapons and Golden Whale
extern int cc_storage_dirty;
extern int cc_save_requested;           // WriteSaveGame was called: write the save file
int cc_storage_save(const char *path);  // 1 on success
int cc_storage_load(const char *path);  // 1 if a save was read
// Wi-Fi play: a port's profile packed to send (bytes written, -1 if it doesn't fit), put
// back on another Vita, everything cleared, and saving with from_port as this Vita's player.
int cc_storage_pack(int port, uint8_t *out, int max);
void cc_storage_unpack(int port, const uint8_t *in, int len);
void cc_storage_clear(void);
int cc_storage_save_as(const char *path, int from_port);

// In-game achievements: the game calls UnlockAchievement(port, id) with the Xbox/Steam ids
// 1..12. Unlocked ones are a bit set kept in a file of their own (not in the game's save).
#define CC_ACHIEVEMENTS 12
const char *cc_achievement_name(int id);        // id 1..12
const char *cc_achievement_how(int id);         // what it asks for
unsigned cc_achievements(void);                 // bit id set: unlocked
int cc_achievement_take_new(void);              // an id unlocked since the last call, or 0
void cc_achievements_load(const char *path);    // also where unlocks are written

// Puts back layer placements a cut-short level file lost; returns how many. Call after swf_load.
int cc_repair_level(Movie *m);

// Music the platform plays (name: the file's base name without extension, e.g. "MUDHOLES").
extern void (*cc_music_play)(const char *name, int loop);
extern void (*cc_music_stop)(void);

// the game's loading screen (LOADING.swf) over everything, played around level changes
void cc_loading_attach(AsVm *vm);
int cc_loading_cover(AsVm *vm);     // Exit to Main Menu: 1 once the loading screen covers the game
void cc_loading_reveal(AsVm *vm);   // the title starts under it, and it plays its exit
int cc_loading_busy(AsVm *vm);      // the loading screen is showing
#endif
