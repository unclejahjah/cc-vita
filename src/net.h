// Wi-Fi co-op: Vitas on the same network play one game together. Every Vita runs the whole
// game; they only swap each player's buttons for every game frame ("lockstep"), so their
// games stay the same frame by frame. One Vita hosts (port 1); up to three join (ports 2-4).
//
// The lobby runs over TCP (who's in, everyone's profile, the start); the game's frames over
// UDP, each packet repeating the ones not yet acknowledged. A Vita's buttons for frame t are
// read NET_DELAY frames earlier, so they can reach everyone before frame t runs.
#ifndef NET_H
#define NET_H

#include <stdint.h>

#define NET_PLAYERS 4
#define NET_DELAY 4            // frames (at 30 a second) between reading the buttons and using them
#define NET_NAME 32
#define NET_VERSION 1          // both sides must have the same: the frame format and the game code

// One player's part of a frame.
typedef struct {
    uint32_t buttons;          // CCB_ bits
    uint8_t state;             // 0: no change, else g_nPort<n>State becomes state - 1
    uint8_t loads;             // background file loads this Vita has finished (mod 256)
    uint16_t check;            // this Vita's game state NET_DELAY + 1 frames before (0: none)
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

enum { NET_OFF, NET_HOSTING, NET_SCANNING, NET_JOINING, NET_LOBBY, NET_PLAYING, NET_LOST };

typedef struct {
    int state;
    int port;                  // this Vita's player: 1..4
    char names[NET_PLAYERS][NET_NAME];   // the lobby's players ("" for nobody)
    uint32_t seed;             // the game's random seed, from the host
    char error[96];            // why the last join or game ended
} NetInfo;

extern NetInfo net;

int net_init(void);            // the network stack (Vita: modules, sceNet); 0 if there's none
const char *net_local_name(void);   // this Vita's user name

// lobby
int net_host(const uint8_t *profile, int len);                  // start hosting; 0 on failure
int net_scan(void);                                              // look for hosts
int net_hosts(NetHost *out, int max);                            // the hosts heard lately
int net_join(uint32_t addr, const uint8_t *profile, int len);    // 0 on failure (net.error)
int net_start(void);           // host: everyone starts the game now
void net_leave(void);          // back to NET_OFF, telling the others
// the profiles at the start, by port (len 0: nobody there); valid once NET_PLAYING
const uint8_t *net_profile(int port, int *len);

// once a frame (lobby and game): send and receive
void net_poll(void);

// the game: frame t can run once net_frame(t) is non-NULL. Before running frame t, give this
// Vita's input for frame t + NET_DELAY with net_submit.
const NetFrame *net_frame(uint32_t t);
void net_submit(uint32_t t, const NetInput *in);
// the first frame whose checks differed between Vitas (0: none so far)
extern uint32_t net_desync_tick;

#endif
