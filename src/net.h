// Wi-Fi co-op: Vitas on the same network play one game together. Every Vita runs the whole
// game; they only swap each player's buttons for every game frame ("lockstep"), so their
// games stay the same frame by frame. One Vita hosts (port 1); up to three join (ports 2-4).
//
// The lobby runs over TCP (who's in, everyone's profile, the start); the game's frames over
// UDP, each packet repeating the ones not yet acknowledged, sent and taken in on a thread of
// their own as soon as they're there (not once a picture). A Vita's buttons for frame t are
// read net.delay frames earlier, so they can reach everyone before frame t runs: the host's
// a frame or so, the others' about their round trip to the host.
//
// The joiners' delays follow the network as the game goes: the host tells each how many frames
// its buttons are early (or that it's waiting for them), and a joiner a frame or more early for
// a while takes one frame off; one the host waits for adds one. A joiner that runs further
// behind the host than it has to catches up a frame at a time.
//
// Over the internet: the host's Vita asks its router to open the game's ports (UPnP) and shows
// a game code (its internet address); the others join by typing it in. The delays at the start
// come from the furthest player's round trip, measured in the lobby.
#ifndef NET_H
#define NET_H

#include <stdint.h>

#define NET_PLAYERS 4
#define NET_MIN_DELAY 1        // frames (at 30 a second) between reading the buttons and using them: the least
#define NET_SPARE 2            // frames on top of the round trip a joiner starts with
#define NET_MAX_DELAY 15       // the most, however far away someone is (half a second)
#define NET_CODE_LEN 7
#define NET_NAME 32
#define NET_VERSION 5         // both sides must have the same: the frame format and the game code

// One player's part of a frame.
typedef struct {
    uint32_t buttons;          // CCB_ bits
    uint8_t state;             // 0: no change, else g_nPort<n>State becomes state - 1
    uint8_t loads;             // background file loads this Vita has finished (mod 256)
    uint16_t check;            // the game state NET_MAX_DELAY + 1 frames before (0: none)
} NetInput;

typedef struct {
    uint8_t active;            // bit p: port p + 1 is in the game
    NetInput in[NET_PLAYERS];
} NetFrame;

typedef struct {
    char name[NET_NAME];
    uint32_t addr;             // IPv4, network order
    int players;
    int age;                   // polls since its last beacon
} NetHost;

enum { NET_RULE_AUTO_POTION = 1 };   // net.rules bits (the host sets them before net_start)
enum { NET_OFF, NET_HOSTING, NET_SCANNING, NET_CONNECTING, NET_JOINING, NET_LOBBY, NET_PLAYING, NET_LOST };

typedef struct {
    int state;
    int port;                  // this Vita's player: 1..4
    char names[NET_PLAYERS][NET_NAME];   // the lobby's players ("" for nobody)
    uint32_t seed;             // the game's random seed, from the host
    int rules;                 // settings every Vita must play by, from the host: NET_RULE_*
    int delay;                 // frames between reading this Vita's buttons and using them (now)
    int first;                 // the host's delay: frames 0..first-1 have no buttons
    int slowest;               // the joiners' delay at the start: their frames 0..slowest-1 have none
    int mode;                  // what the host picked to play: GetGameMode()'s answer (cc_host.h CC_MODE_*)
    char error[96];            // why the last join or game ended
} NetInfo;

extern NetInfo net;

int net_init(void);            // the network stack (Vita: modules, sceNet); 0 if there's none
const char *net_local_name(void);   // this Vita's user name

// lobby
int net_host(const uint8_t *profile, int len);                  // start hosting; 0 on failure
int net_scan(void);                                              // look for hosts
int net_hosts(NetHost *out, int max);                            // the hosts heard lately
// join (NET_CONNECTING, then NET_JOINING once through); 0 on failure (net.error)
int net_join(uint32_t addr, const uint8_t *profile, int len);
int net_start(void);           // host: everyone starts the game now
void net_leave(void);          // back to NET_OFF, telling the others
// host: the game open to the internet? 1 yes (addr: its address), 0 still asking the router,
// -1 no (why: what went wrong)
int net_internet(uint32_t *addr, const char **why);
void net_code(uint32_t addr, char *out);   // the game code for an address (out: NET_CODE_LEN + 1)
uint32_t net_code_addr(const char *code);  // and back: 0 if it isn't one
// the profiles at the start, by port (len 0: nobody there); valid once NET_PLAYING
const uint8_t *net_profile(int port, int *len);

// once a frame (lobby and game): send and receive
void net_poll(void);

// the game: frame t can run once net_frame(t) is non-NULL. Before running frame t, call
// net_adapt(t) (it may move net.delay by one), then give this Vita's input for each frame from
// net_submitted() up to t + net.delay with net_submit (none, one, or two of them).
const NetFrame *net_frame(uint32_t t);
void net_adapt(uint32_t t);
uint32_t net_submitted(void);
void net_submit(uint32_t t, const NetInput *in);
// 1 (once): this Vita has been further behind the host than it needs for a while: run a frame
// more now
int net_catch_up(void);
// the first frame whose checks differed between Vitas (0: none so far)
extern uint32_t net_desync_tick;

#endif
