// The public games list: a small server on the internet (tools/lobby/) keeps the games open
// to everyone. A host that chose Host a Public Game tells it "a game here" every little while
// (the server takes the address it sees the Vita at, so it lists only the sender's own game);
// the Public Games page asks it for the list. Joining is then straight to the host, as by
// game code. Runs on its own thread: the server can be a second or more away.
//
// The server's address: ux0:data/CastleCrashers/lobby.txt (its first line) if it's there,
// else CC_LOBBY_URL (cc_host.h); LOBBY_URL in hosttest. Empty: no public games.
#ifndef LOBBY_H
#define LOBBY_H

#include <stdint.h>

#define LOBBY_MAX 24             // games in the list

typedef struct {
    char name[32];
    uint32_t addr;               // IPv4, network order
    int players;
} LobbyGame;

enum { LOBBY_OFF, LOBBY_WORKING, LOBBY_OK, LOBBY_FAILED };

int lobby_available(void);       // 0: no server set

// host: keep this game in the list (call each lobby frame; players: how many are in), or
// take it off (game started, closed, or no longer open to the internet)
void lobby_publish(const char *name, int players);
void lobby_unpublish(void);
int lobby_listed(void);          // LOBBY_OK once the server has it; LOBBY_FAILED: lobby_why()

// the list: ask again (each call is a new request; at most one at a time)
void lobby_refresh(void);
int lobby_list_state(void);
int lobby_games(LobbyGame *out, int max);   // the last list
const char *lobby_why(void);                // what went wrong last

#endif
