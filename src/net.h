// Wi-Fi co-op: Vitas on the same network play one game together. Every Vita runs the whole
// game; they only swap each player's buttons for every game frame ("lockstep"), so their
// games stay the same frame by frame. One Vita hosts (port 1); up to three join (ports 2-4).
//
// The lobby runs over TCP (who's in, everyone's profile, the start); the game's frames over
// UDP, each packet repeating the ones not yet acknowledged. A Vita's buttons for frame t are
// read net.delay frames earlier, so they can reach everyone before frame t runs (the host's
// a little, the others' their round trip to the host).
//
// Over the internet: the host's Vita asks its router to open the game's ports (UPnP) and shows
// a game code (its internet address); the others join by typing it in. The delay grows with
// the furthest player's round trip, measured in the lobby.
#ifndef NET_H
#define NET_H

#include <stdint.h>

#define NET_PLAYERS 4
#define NET_DELAY 4            // frames (at 30 a second) between reading the buttons and using them, on Wi-Fi
#define NET_SPARE 3            // frames to spare on top of the round trip (the host's delay)
#define NET_MAX_DELAY 15       // the most, however far away someone is (half a second)
#define NET_CODE_LEN 7
#define NET_NAME 32
#define NET_VERSION 3          // both sides must have the same: the frame format and the game code

// One player's part of a frame.
typedef struct {
    uint32_t buttons;          // CCB_ bits
    uint8_t state;             // 0: no change, else g_nPort<n>State becomes state - 1
    uint8_t loads;             // background file loads this Vita has finished (mod 256)
    uint16_t check;            // the game state net.slowest + 1 frames before (0: none)
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

enum { NET_OFF, NET_HOSTING, NET_SCANNING, NET_CONNECTING, NET_JOINING, NET_LOBBY, NET_PLAYING, NET_LOST };

typedef struct {
    int state;
    int port;                  // this Vita's player: 1..4
    char names[NET_PLAYERS][NET_NAME];   // the lobby's players ("" for nobody)
    uint32_t seed;             // the game's random seed, from the host
    int delay;                 // frames between reading this Vita's buttons and using them (from the start)
    int first;                 // the host's delay: frames 0..first-1 have no buttons
    int slowest;               // the joiners' delay (the longest)
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

// the game: frame t can run once net_frame(t) is non-NULL. Before running frame t, give this
// Vita's input for frame t + net.delay with net_submit.
const NetFrame *net_frame(uint32_t t);
void net_submit(uint32_t t, const NetInput *in);
// the first frame whose checks differed between Vitas (0: none so far)
extern uint32_t net_desync_tick;

#endif
