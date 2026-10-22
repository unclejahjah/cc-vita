// Wi-Fi co-op: the lobby (TCP) and the game's frames (UDP). See net.h.
#include "net.h"
#include "upnp.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#ifdef __vita__
#include <psp2/apputil.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/system_param.h>
#else
#include <time.h>
#endif

#define TCP_PORT 47710         // the host's lobby
#define UDP_PORT 47711         // the host's frames
#define BEACON_PORT 47712      // "a game here", broadcast by the host
#define RING 256               // frames kept (lockstep keeps everyone within net.slowest of each other)
#define MAX_SEND 32            // frames or inputs in one packet
#define TIMEOUT_MS 45000       // nothing heard for this long: that Vita is gone (a file can take 15 s to load)
#define MAX_PROFILE (3 * 65536)

enum { MSG_HELLO = 1, MSG_WELCOME, MSG_REFUSE, MSG_ROSTER, MSG_START, MSG_BYE, MSG_PING, MSG_PONG };

NetInfo net;
uint32_t net_desync_tick;

static int base_port(int k) {   // NET_PORT moves all three (two hosts on one PC)
    const char *e = getenv("NET_PORT");
    return (e ? atoi(e) : TCP_PORT) + k;
}

static uint32_t now_ms(void) {
#ifdef __vita__
    return (uint32_t)(sceKernelGetProcessTimeWide() / 1000);
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
#endif
}

// ---- sockets ----

static void nonblocking(int s) {
#ifdef __vita__
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NONBLOCK, &on, sizeof on);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

// a failed send/recv on a TCP connection that means it's gone (anything else: try again later;
// the Vita's errors don't always come back as EAGAIN)
static int conn_gone(void) {
    return errno == ECONNRESET || errno == ENOTCONN || errno == EPIPE || errno == ECONNABORTED ||
           errno == ETIMEDOUT || errno == EBADF;
}

static void set_addr(struct sockaddr_in *a, uint32_t ip, int port) {   // ip in network order
    memset(a, 0, sizeof *a);
#ifdef __vita__
    a->sin_len = sizeof *a;
#endif
    a->sin_family = AF_INET;
    a->sin_port = htons(port);
    a->sin_addr.s_addr = ip;
}

// what failed last, for the message on screen (step and error number)
static char g_why[48];
static int fail(const char *step, int r) {
    snprintf(g_why, sizeof g_why, "%s %d/%d", step, r, errno);
    return -1;
}
static int g_init_net = 1, g_init_ctl = 1;   // sceNetInit / sceNetCtlInit results (1: not run)

static int udp_open(int port, int broadcast) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return fail("udp", s);
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    if (broadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    struct sockaddr_in a;
    set_addr(&a, htonl(INADDR_ANY), port);
    int r = bind(s, (struct sockaddr *)&a, sizeof a);
    if (r < 0) { close(s); return fail("udp bind", r); }
    nonblocking(s);
    return s;
}

static void tcp_tune(int s) {
    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    nonblocking(s);
}

// a TCP connection with its unsent and not yet complete messages
typedef struct {
    int s;
    uint8_t *tx, *rx;
    int ntx, captx, nrx, caprx;
    int consume;               // the message handed out last: taken off rx on the next call
    int closed;
} Conn;

static void conn_close(Conn *c) {
    if (c->s >= 0) close(c->s);
    free(c->tx);
    free(c->rx);
    memset(c, 0, sizeof *c);
    c->s = -1;
}

static void conn_send(Conn *c, int type, const void *a, int na, const void *b, int nb) {
    int len = 1 + na + nb;
    if (c->ntx + 4 + len > c->captx) {
        c->captx = (c->ntx + 4 + len) * 2;
        c->tx = realloc(c->tx, c->captx);
    }
    uint8_t *p = c->tx + c->ntx;
    p[0] = len & 255; p[1] = len >> 8 & 255; p[2] = len >> 16 & 255; p[3] = len >> 24;
    p[4] = type;
    if (na) memcpy(p + 5, a, na);
    if (nb) memcpy(p + 5 + na, b, nb);
    c->ntx += 4 + len;
}

static void conn_flush(Conn *c) {
    while (c->s >= 0 && c->ntx > 0) {
        int n = send(c->s, c->tx, c->ntx, 0);
        if (n <= 0) {
            if (n < 0 && conn_gone()) c->closed = 1;
            return;
        }
        memmove(c->tx, c->tx + n, c->ntx - n);
        c->ntx -= n;
    }
}

// the next whole message (type, payload); 0 if none yet. The payload stays valid until the next call.
static int conn_recv(Conn *c, int *type, uint8_t **msg, int *len) {
    if (c->consume) {
        memmove(c->rx, c->rx + c->consume, c->nrx - c->consume);
        c->nrx -= c->consume;
        c->consume = 0;
    }
    for (;;) {
        if (c->nrx >= 4) {
            int l = c->rx[0] | c->rx[1] << 8 | c->rx[2] << 16 | c->rx[3] << 24;
            if (l < 1 || l > 4 * MAX_PROFILE + 4096) { c->closed = 1; return 0; }
            if (c->nrx >= 4 + l) {
                *type = c->rx[4];
                *msg = c->rx + 5;
                *len = l - 1;
                c->consume = 4 + l;
                return 1;
            }
        }
        if (c->s < 0 || c->closed) return 0;
        if (c->caprx - c->nrx < 65536) {
            c->caprx = c->caprx * 2 + 65536;
            c->rx = realloc(c->rx, c->caprx);
        }
        int n = recv(c->s, c->rx + c->nrx, c->caprx - c->nrx, 0);
        if (n == 0) { c->closed = 1; return 0; }
        if (n < 0) {
            if (conn_gone()) c->closed = 1;
            return 0;
        }
        c->nrx += n;
    }
}

// ---- little-endian packing ----

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void put_input(uint8_t *p, const NetInput *in) {
    put32(p, in->buttons);
    p[4] = in->state;
    p[5] = in->loads;
    p[6] = in->check & 255;
    p[7] = in->check >> 8;
}
static void get_input(const uint8_t *p, NetInput *in) {
    in->buttons = get32(p);
    in->state = p[4];
    in->loads = p[5];
    in->check = p[6] | p[7] << 8;
}
#define INPUT_BYTES 8
#define FRAME_BYTES (1 + NET_PLAYERS * INPUT_BYTES)

// ---- state ----

typedef struct {
    Conn c;
    int used;                  // in the lobby or the game
    int dropped;               // gone: its port is let go in the next frame built
    struct sockaddr_in udp;    // where its frames go (learnt from its first input)
    int have_udp;
    NetInput in[RING];         // its inputs
    uint32_t in_have;          // its inputs 0..in_have-1 are here
    uint32_t acked;            // it has frames 0..acked-1
    uint32_t heard;            // ms of the last packet
    uint32_t pings[8];         // ms there and back, the last few lobby pings
    int npings;
    uint8_t *profile;
    int nprofile;
} Peer;

static int g_ready;
static int s_listen = -1, s_udp = -1, s_beacon = -1;
static Peer peers[NET_PLAYERS];    // host: index = port - 1 (0 is the host itself, unused)
static Conn g_host;                // client: the connection to the host
static struct sockaddr_in g_host_udp;
static uint32_t g_host_heard, g_last_beacon, g_last_ping;
static NetHost g_found[8];
static int g_nfound;
static uint8_t *g_my_profile;
static int g_my_nprofile;
static uint8_t *g_profiles[NET_PLAYERS];
static int g_nprofiles[NET_PLAYERS];

static NetFrame frames[RING];
static uint32_t frames_have;        // frames 0..frames_have-1 are known
static NetInput mine[RING];         // this Vita's inputs
static uint32_t mine_have;          // submitted 0..mine_have-1
static uint32_t mine_acked;         // client: the host has 0..mine_acked-1
static uint8_t g_active;            // host: ports in the game now

int net_init(void) {
    if (g_ready) return 1;
#ifdef __vita__
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0) return 0;
    static uint8_t mem[1024 * 1024] __attribute__((aligned(4096)));
    SceNetInitParam p = {mem, sizeof mem, 0};
    // (an error here is usually "already done"; sockets fail later if not, and say these)
    g_init_net = sceNetInit(&p);
    g_init_ctl = sceNetCtlInit();
#endif
    for (int i = 0; i < NET_PLAYERS; i++) peers[i].c.s = -1;
    g_host.s = -1;
    g_ready = 1;
    return 1;
}

const char *net_local_name(void) {
    static char name[NET_NAME];
    if (name[0]) return name;
#ifdef __vita__
    SceAppUtilInitParam ip = {0};
    SceAppUtilBootParam bp = {0};
    sceAppUtilInit(&ip, &bp);
    if (sceAppUtilSystemParamGetString(SCE_SYSTEM_PARAM_ID_USERNAME, (SceChar8 *)name, sizeof name) < 0) name[0] = 0;
#else
    if (getenv("NET_NAME")) snprintf(name, sizeof name, "%s", getenv("NET_NAME"));
#endif
    if (!name[0]) snprintf(name, sizeof name, "PS Vita");
    return name;
}

static void close_all(void) {
    if (s_listen >= 0) close(s_listen);
    if (s_udp >= 0) close(s_udp);
    if (s_beacon >= 0) close(s_beacon);
    s_listen = s_udp = s_beacon = -1;
    for (int i = 0; i < NET_PLAYERS; i++) {
        conn_close(&peers[i].c);
        free(peers[i].profile);
        memset(&peers[i], 0, sizeof peers[i]);
        peers[i].c.s = -1;
    }
    conn_close(&g_host);
    for (int i = 0; i < NET_PLAYERS; i++) { free(g_profiles[i]); g_profiles[i] = NULL; g_nprofiles[i] = 0; }
}

static void set_profile(const uint8_t *profile, int len) {
    free(g_my_profile);
    g_my_profile = malloc(len + 1);
    memcpy(g_my_profile, profile, len);
    g_my_nprofile = len;
}

static void reset_game(void) {
    memset(frames, 0, sizeof frames);
    memset(mine, 0, sizeof mine);
    // the first frames have no buttons: nobody read any yet (net.first for the host's,
    // net.delay for this Vita's)
    frames_have = net.first;
    mine_have = net.delay;
    mine_acked = 0;
    net_desync_tick = 0;
}

// ---- host ----

static void roster(void) {
    char names[NET_PLAYERS][NET_NAME] = {{0}};
    snprintf(names[0], NET_NAME, "%s", net_local_name());
    for (int p = 1; p < NET_PLAYERS; p++)
        if (peers[p].used) memcpy(names[p], net.names[p], NET_NAME);
    memcpy(net.names, names, sizeof names);
    for (int p = 1; p < NET_PLAYERS; p++)
        if (peers[p].used) conn_send(&peers[p].c, MSG_ROSTER, names, sizeof names, NULL, 0);
}

int net_host(const uint8_t *profile, int len) {
    if (!net_init()) { snprintf(net.error, sizeof net.error, "The network isn't available"); return 0; }
    net_leave();
    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    set_addr(&a, htonl(INADDR_ANY), base_port(0));
    int on = 1, r = 0;
    if (s_listen < 0) fail("tcp", s_listen);
    else setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    if (s_listen >= 0 && (r = bind(s_listen, (struct sockaddr *)&a, sizeof a)) < 0) fail("tcp bind", r);
    if (s_listen >= 0 && r >= 0 && (r = listen(s_listen, 4)) < 0) fail("listen", r);
    if (s_listen < 0 || r < 0) {
        snprintf(net.error, sizeof net.error, "Couldn't open a game (%s; init %x %x)", g_why, g_init_net, g_init_ctl);
        close_all();
        return 0;
    }
    nonblocking(s_listen);
    s_udp = udp_open(base_port(1), 0);
    s_beacon = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_beacon >= 0) { setsockopt(s_beacon, SOL_SOCKET, SO_BROADCAST, &on, sizeof on); nonblocking(s_beacon); }
    if (s_udp < 0) {
        snprintf(net.error, sizeof net.error, "Couldn't open a game (%s; init %x %x)", g_why, g_init_net, g_init_ctl);
        close_all();
        return 0;
    }
    set_profile(profile, len);
    memset(net.names, 0, sizeof net.names);
    net.port = 1;
    net.state = NET_HOSTING;
    net.error[0] = 0;
    roster();
    upnp_open(base_port(0), base_port(1));   // and from the internet, by the game code
    return 1;
}

static void host_lobby(void) {
    uint32_t t = now_ms();
    if (s_beacon >= 0 && t - g_last_beacon >= 500) {   // "a game here" twice a second
        g_last_beacon = t;
        uint8_t b[4 + 2 + NET_NAME] = {'C', 'C', 'B', NET_VERSION};
        int n = 0;
        for (int p = 0; p < NET_PLAYERS; p++) n += p == 0 || peers[p].used;
        b[4] = n;
        snprintf((char *)b + 6, NET_NAME, "%s", net_local_name());
        struct sockaddr_in a;
        set_addr(&a, htonl(INADDR_BROADCAST), base_port(2));
        sendto(s_beacon, b, sizeof b, 0, (struct sockaddr *)&a, sizeof a);
    }
    for (;;) {   // new arrivals: a free port, once they've said hello
        int s = accept(s_listen, NULL, NULL);
        if (s < 0) break;
        int p = 1;
        while (p < NET_PLAYERS && peers[p].c.s >= 0) p++;
        if (p == NET_PLAYERS) { close(s); continue; }
        memset(&peers[p], 0, sizeof peers[p]);
        peers[p].c.s = s;
        peers[p].heard = t;
        tcp_tune(s);
    }
    int ping = t - g_last_ping >= 250;   // how far away each player is: the game's delay
    if (ping) g_last_ping = t;
    int changed = 0;
    for (int p = 1; p < NET_PLAYERS; p++) {
        Peer *pe = &peers[p];
        if (pe->c.s < 0) continue;
        if (ping && pe->used) { uint8_t b[4]; put32(b, t); conn_send(&pe->c, MSG_PING, b, 4, NULL, 0); }
        int type, len;
        uint8_t *m;
        while (conn_recv(&pe->c, &type, &m, &len)) {
            if (type == MSG_HELLO && len >= 4 + NET_NAME && !pe->used) {
                if (get32(m) != NET_VERSION) {
                    const char *why = "The host's Castle Crashers is a different version";
                    conn_send(&pe->c, MSG_REFUSE, why, strlen(why) + 1, NULL, 0);
                    conn_flush(&pe->c);
                    pe->c.closed = 1;
                    break;
                }
                memcpy(net.names[p], m + 4, NET_NAME);
                net.names[p][NET_NAME - 1] = 0;
                pe->nprofile = len - 4 - NET_NAME;
                pe->profile = malloc(pe->nprofile + 1);
                memcpy(pe->profile, m + 4 + NET_NAME, pe->nprofile);
                pe->used = 1;
                uint8_t port = p + 1;
                conn_send(&pe->c, MSG_WELCOME, &port, 1, NULL, 0);
                changed = 1;
            } else if (type == MSG_PONG && len >= 4) {
                pe->pings[pe->npings++ % 8] = t - get32(m);
            } else if (type == MSG_BYE) {
                pe->c.closed = 1;
            }
        }
        if (pe->c.closed) {
            conn_close(&pe->c);
            free(pe->profile);
            changed |= pe->used;
            memset(pe, 0, sizeof *pe);
            pe->c.s = -1;
        }
    }
    if (changed) roster();
}

// a player's usual round trip: the middle of the last pings (one slow frame on either Vita
// while loading the menu isn't the network)
static uint32_t ping_ms(const Peer *pe) {
    int n = pe->npings < 8 ? pe->npings : 8;
    uint32_t v[8];
    memcpy(v, pe->pings, sizeof v);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { uint32_t x = v[j]; v[j] = v[j - 1]; v[j - 1] = x; }
    return n ? v[n / 2] : 0;
}

int net_start(void) {
    if (net.state != NET_HOSTING) return 0;
    net.seed = now_ms() * 2654435761u ^ 0x5bd1e995u;
    g_active = 1;
    uint32_t rtt = 0;
    for (int p = 1; p < NET_PLAYERS; p++) {
        g_active |= peers[p].used << p;
        if (peers[p].used && ping_ms(&peers[p]) > rtt) rtt = ping_ms(&peers[p]);
    }
    // A joiner's buttons go to the host and back (in a frame) before that frame runs: the
    // furthest player's round trip in frames, and some to spare. The host's own only have to
    // reach the others, and the joiners settle that far behind the host by themselves, so the
    // host's need just the spare.
    net.slowest = NET_SPARE + (rtt * 30 + 999) / 1000;
    if (net.slowest < NET_DELAY) net.slowest = NET_DELAY;
    if (net.slowest > NET_MAX_DELAY) net.slowest = NET_MAX_DELAY;
    net.first = NET_SPARE;
    if (getenv("NET_DELAY")) net.first = net.slowest = atoi(getenv("NET_DELAY"));
    net.delay = net.first;
    // the start: seed, delay, who's in, names, then each port's profile
    int total = 4 + 2 + 1 + sizeof net.names;
    for (int p = 0; p < NET_PLAYERS; p++) total += 4 + (p == 0 ? g_my_nprofile : peers[p].used ? peers[p].nprofile : 0);
    uint8_t *b = malloc(total), *q = b;
    put32(q, net.seed); q += 4;
    *q++ = net.first;
    *q++ = net.slowest;
    *q++ = g_active;
    memcpy(q, net.names, sizeof net.names); q += sizeof net.names;
    for (int p = 0; p < NET_PLAYERS; p++) {
        const uint8_t *pr = p == 0 ? g_my_profile : peers[p].profile;
        int n = p == 0 ? g_my_nprofile : peers[p].used ? peers[p].nprofile : 0;
        put32(q, n); q += 4;
        if (n) memcpy(q, pr, n);
        q += n;
        g_profiles[p] = malloc(n + 1);
        if (n) memcpy(g_profiles[p], pr, n);
        g_nprofiles[p] = n;
    }
    for (int p = 1; p < NET_PLAYERS; p++)
        if (peers[p].used) {
            conn_send(&peers[p].c, MSG_START, b, total, NULL, 0);
            conn_flush(&peers[p].c);
            memset(peers[p].in, 0, sizeof peers[p].in);
            peers[p].in_have = net.slowest;
            peers[p].acked = 0;
            peers[p].heard = now_ms();
        }
    free(b);
    if (s_beacon >= 0) { close(s_beacon); s_beacon = -1; }
    reset_game();
    for (int t = 0; t < net.first; t++) frames[t].active = g_active;
    net.state = NET_PLAYING;
    return 1;
}

static uint16_t frame_check(const NetFrame *f, uint32_t t) {   // the checks differ: out of step
    uint16_t c = 0;
    for (int p = 0; p < NET_PLAYERS; p++) {
        if (!(f->active >> p & 1) || !f->in[p].check) continue;
        if (c && f->in[p].check != c && !net_desync_tick) net_desync_tick = t;
        c = f->in[p].check;
    }
    return c;
}

static void host_game(void) {
    uint32_t t = now_ms();
    uint8_t buf[1500];
    struct sockaddr_in from;
    socklen_t fl;
    for (;;) {   // inputs
        fl = sizeof from;
        int n = recvfrom(s_udp, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        if (n < 0) break;
        if (n < 13 || memcmp(buf, "CCI", 3)) continue;
        int p = buf[3] - 1;
        if (p < 1 || p >= NET_PLAYERS || !peers[p].used || peers[p].dropped) continue;
        Peer *pe = &peers[p];
        pe->udp = from;
        pe->have_udp = 1;
        pe->heard = t;
        uint32_t acked = get32(buf + 4), first = get32(buf + 8);
        int count = buf[12];
        if (acked > pe->acked && acked <= frames_have) pe->acked = acked;
        for (int i = 0; i < count && 13 + (i + 1) * INPUT_BYTES <= n; i++) {
            uint32_t k = first + i;
            if (k != pe->in_have || k >= frames_have + RING - 1) continue;
            get_input(buf + 13 + i * INPUT_BYTES, &pe->in[k % RING]);
            pe->in_have++;
        }
    }
    for (int p = 1; p < NET_PLAYERS; p++) {   // gone: closed, or silent too long
        Peer *pe = &peers[p];
        if (!pe->used || pe->dropped) continue;
        int type, len;
        uint8_t *m;
        while (conn_recv(&pe->c, &type, &m, &len))
            if (type == MSG_BYE) pe->c.closed = 1;
        if (pe->c.closed || t - pe->heard > TIMEOUT_MS) pe->dropped = 1;
    }
    // frames everyone has given their part of
    for (;;) {
        uint32_t k = frames_have;
        if (k >= mine_have) break;
        int ready = 1;
        for (int p = 1; p < NET_PLAYERS; p++)
            if ((g_active >> p & 1) && !peers[p].dropped && peers[p].in_have <= k) ready = 0;
        if (!ready) break;
        NetFrame *f = &frames[k % RING];
        memset(f, 0, sizeof *f);
        f->in[0] = mine[k % RING];
        for (int p = 1; p < NET_PLAYERS; p++) {
            if (!(g_active >> p & 1)) continue;
            if (peers[p].dropped) {   // let the game know it's gone, once
                g_active &= ~(1 << p);
                f->in[p].state = 1;   // g_nPort<n>State 0: dropped out
                continue;
            }
            f->in[p] = peers[p].in[k % RING];
        }
        f->active = g_active;
        frame_check(f, k);
        frames_have++;
    }
    // to each: the frames it hasn't got, and how many of its inputs are here
    for (int p = 1; p < NET_PLAYERS; p++) {
        Peer *pe = &peers[p];
        if (!pe->used || pe->dropped || !pe->have_udp) continue;
        uint32_t first = pe->acked < (uint32_t)net.first ? (uint32_t)net.first : pe->acked;
        if (first + RING / 2 < frames_have) first = frames_have - RING / 2;
        int count = frames_have > first ? frames_have - first : 0;
        if (count > MAX_SEND) count = MAX_SEND;
        memcpy(buf, "CCF", 3);
        buf[3] = 0;
        put32(buf + 4, pe->in_have);
        put32(buf + 8, first);
        buf[12] = count;
        for (int i = 0; i < count; i++) {
            const NetFrame *f = &frames[(first + i) % RING];
            uint8_t *q = buf + 13 + i * FRAME_BYTES;
            q[0] = f->active;
            for (int k = 0; k < NET_PLAYERS; k++) put_input(q + 1 + k * INPUT_BYTES, &f->in[k]);
        }
        sendto(s_udp, buf, 13 + count * FRAME_BYTES, 0, (struct sockaddr *)&pe->udp, sizeof pe->udp);
    }
}

// ---- joining ----

int net_scan(void) {
    if (!net_init()) { snprintf(net.error, sizeof net.error, "The network isn't available"); return 0; }
    net_leave();
    s_beacon = udp_open(base_port(2), 0);
    if (s_beacon < 0) { snprintf(net.error, sizeof net.error, "Couldn't look for games (%s; init %x %x)", g_why, g_init_net, g_init_ctl); return 0; }
    g_nfound = 0;
    net.state = NET_SCANNING;
    net.error[0] = 0;
    return 1;
}

static void scan_poll(void) {
    uint8_t b[256];
    struct sockaddr_in from;
    for (int i = 0; i < g_nfound; i++) g_found[i].age++;
    for (;;) {
        socklen_t fl = sizeof from;
        int n = recvfrom(s_beacon, b, sizeof b, 0, (struct sockaddr *)&from, &fl);
        if (n < 0) break;
        if (n < 6 + NET_NAME || memcmp(b, "CCB", 3) || b[3] != NET_VERSION) continue;
        int i = 0;
        while (i < g_nfound && g_found[i].addr != from.sin_addr.s_addr) i++;
        if (i == g_nfound) {
            if (g_nfound == 8) continue;
            g_nfound++;
        }
        g_found[i].addr = from.sin_addr.s_addr;
        g_found[i].players = b[4];
        memcpy(g_found[i].name, b + 6, NET_NAME);
        g_found[i].name[NET_NAME - 1] = 0;
        g_found[i].age = 0;
    }
    int k = 0;   // a host unheard for ~3 s (at 60 polls a second) is gone
    for (int i = 0; i < g_nfound; i++)
        if (g_found[i].age < 180) g_found[k++] = g_found[i];
    g_nfound = k;
}

int net_hosts(NetHost *out, int max) {
    int n = g_nfound < max ? g_nfound : max;
    memcpy(out, g_found, n * sizeof *out);
    return n;
}

// connecting to a host, on its own thread: over the internet, or to an address nobody's at,
// connect() can take a long while, and the lobby must keep drawing
static pthread_mutex_t g_conn_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned g_conn_gen;          // each join (or leave): a thread on an older one keeps quiet
static int g_conn_s = -1, g_conn_done;
static char g_conn_why[48];

typedef struct { unsigned gen; struct sockaddr_in a; } ConnJob;

static void *connect_thread(void *arg) {
    ConnJob job = *(ConnJob *)arg;
    free(arg);
    char why[48] = "";
    int s = socket(AF_INET, SOCK_STREAM, 0), r = -1;
    if (s < 0) snprintf(why, sizeof why, "tcp %d/%d", s, errno);
    else if ((r = connect(s, (struct sockaddr *)&job.a, sizeof job.a)) < 0) {
        snprintf(why, sizeof why, "connect %d/%d", r, errno);
        close(s);
        s = -1;
    }
    pthread_mutex_lock(&g_conn_lock);
    if (job.gen == g_conn_gen) {
        g_conn_s = s;
        g_conn_done = 1;
        snprintf(g_conn_why, sizeof g_conn_why, "%s", why);
    } else if (s >= 0) close(s);
    pthread_mutex_unlock(&g_conn_lock);
    return NULL;
}

static void connect_cancel(void) {
    pthread_mutex_lock(&g_conn_lock);
    g_conn_gen++;
    if (g_conn_done && g_conn_s >= 0) close(g_conn_s);
    g_conn_s = -1;
    g_conn_done = 0;
    pthread_mutex_unlock(&g_conn_lock);
}

int net_join(uint32_t addr, const uint8_t *profile, int len) {
    if (!net_init()) { snprintf(net.error, sizeof net.error, "The network isn't available"); return 0; }
    net_leave();
    ConnJob *job = malloc(sizeof *job);
    set_addr(&job->a, addr, base_port(0));
    g_host_udp = job->a;
    g_host_udp.sin_port = htons(base_port(1));
    pthread_mutex_lock(&g_conn_lock);
    job->gen = ++g_conn_gen;
    g_conn_done = 0;
    g_conn_s = -1;
    pthread_mutex_unlock(&g_conn_lock);
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64 * 1024);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    int bad = pthread_create(&th, &at, connect_thread, job);
    pthread_attr_destroy(&at);
    if (bad) {
        free(job);
        snprintf(net.error, sizeof net.error, "Couldn't reach that game (thread %d)", bad);
        return 0;
    }
    set_profile(profile, len);
    memset(net.names, 0, sizeof net.names);
    net.port = 0;
    net.state = NET_CONNECTING;
    net.error[0] = 0;
    g_host_heard = now_ms();
    return 1;
}

static void lost(const char *why);

static void client_connecting(void) {
    pthread_mutex_lock(&g_conn_lock);
    int done = g_conn_done, s = g_conn_s;
    char why[48];
    memcpy(why, g_conn_why, sizeof why);
    if (done) { g_conn_done = 0; g_conn_s = -1; }
    pthread_mutex_unlock(&g_conn_lock);
    if (!done) {
        if (now_ms() - g_host_heard > 20000) { connect_cancel(); lost("Couldn't reach that game (no answer)"); }
        return;
    }
    if (s < 0) {
        char m[96];
        snprintf(m, sizeof m, "Couldn't reach that game (%s)", why);
        lost(m);
        return;
    }
    tcp_tune(s);
    g_host.s = s;
    s_udp = udp_open(getenv("NET_PORT") ? 0 : base_port(1), 0);
    if (s_udp < 0) s_udp = udp_open(0, 0);
    uint8_t head[4 + NET_NAME] = {0};
    put32(head, NET_VERSION);
    snprintf((char *)head + 4, NET_NAME, "%s", net_local_name());
    conn_send(&g_host, MSG_HELLO, head, sizeof head, g_my_profile, g_my_nprofile);
    conn_flush(&g_host);
    net.state = NET_JOINING;
    g_host_heard = now_ms();
}

static void lost(const char *why) {
    snprintf(net.error, sizeof net.error, "%s", why);
    close_all();
    net.state = NET_LOST;
}

static void client_lobby(void) {
    int type, len;
    uint8_t *m;
    while (conn_recv(&g_host, &type, &m, &len)) {
        if (type == MSG_WELCOME && len >= 1) {
            net.port = m[0];
            net.state = NET_LOBBY;
        } else if (type == MSG_REFUSE) {
            lost(len > 0 ? (const char *)m : "The host said no");
            return;
        } else if (type == MSG_ROSTER && len >= (int)sizeof net.names) {
            memcpy(net.names, m, sizeof net.names);
            for (int p = 0; p < NET_PLAYERS; p++) net.names[p][NET_NAME - 1] = 0;
        } else if (type == MSG_PING && len >= 4) {
            conn_send(&g_host, MSG_PONG, m, 4, NULL, 0);
        } else if (type == MSG_START && len >= 7 + (int)sizeof net.names) {
            const uint8_t *q = m, *end = m + len;
            net.seed = get32(q); q += 4;
            net.first = *q++;
            net.slowest = net.delay = *q++;
            if (net.first < 1 || net.slowest < net.first || net.slowest > 64) { lost("The game's start got lost"); return; }
            uint8_t active = *q++;
            memcpy(net.names, q, sizeof net.names); q += sizeof net.names;
            for (int p = 0; p < NET_PLAYERS; p++) {
                if (q + 4 > end) { lost("The game's start got lost"); return; }
                int n = get32(q); q += 4;
                if (n < 0 || q + n > end) { lost("The game's start got lost"); return; }
                g_profiles[p] = malloc(n + 1);
                if (n) memcpy(g_profiles[p], q, n);
                g_nprofiles[p] = n;
                q += n;
            }
            reset_game();
            for (int t = 0; t < net.first; t++) frames[t].active = active;
            g_host_heard = now_ms();
            net.state = NET_PLAYING;
            return;
        } else if (type == MSG_BYE) {
            lost("The host left");
            return;
        }
    }
    if (g_host.closed) lost("The host left");
}

static void client_game(void) {
    uint32_t t = now_ms();
    uint8_t buf[1500];
    struct sockaddr_in from;
    for (;;) {
        socklen_t fl = sizeof from;
        int n = recvfrom(s_udp, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        if (n < 0) break;
        // (from the host's frames port: its address can differ from the one joined, as when the
        // router sends a game back in to its own network)
        if (n < 13 || memcmp(buf, "CCF", 3) || from.sin_port != g_host_udp.sin_port) continue;
        g_host_heard = t;
        uint32_t have = get32(buf + 4), first = get32(buf + 8);
        if (have > mine_acked && have <= mine_have) mine_acked = have;
        int count = buf[12];
        for (int i = 0; i < count && 13 + (i + 1) * FRAME_BYTES <= n; i++) {
            uint32_t k = first + i;
            if (k != frames_have) continue;
            const uint8_t *q = buf + 13 + i * FRAME_BYTES;
            NetFrame *f = &frames[k % RING];
            f->active = q[0];
            for (int p = 0; p < NET_PLAYERS; p++) get_input(q + 1 + p * INPUT_BYTES, &f->in[p]);
            frame_check(f, k);
            frames_have++;
        }
    }
    int type, len;
    uint8_t *m;
    while (conn_recv(&g_host, &type, &m, &len))
        if (type == MSG_BYE) g_host.closed = 1;
    if (g_host.closed) { lost("The host left the game"); return; }
    if (t - g_host_heard > TIMEOUT_MS) { lost("Lost the connection to the host"); return; }
    // my inputs the host hasn't got (and, in the same packet, how many frames I have)
    uint32_t first = mine_acked < (uint32_t)net.delay ? (uint32_t)net.delay : mine_acked;
    int count = mine_have > first ? mine_have - first : 0;
    if (count > MAX_SEND) count = MAX_SEND;
    memcpy(buf, "CCI", 3);
    buf[3] = net.port;
    put32(buf + 4, frames_have);
    put32(buf + 8, first);
    buf[12] = count;
    for (int i = 0; i < count; i++) put_input(buf + 13 + i * INPUT_BYTES, &mine[(first + i) % RING]);
    sendto(s_udp, buf, 13 + count * INPUT_BYTES, 0, (struct sockaddr *)&g_host_udp, sizeof g_host_udp);
}

void net_poll(void) {
    switch (net.state) {
    case NET_HOSTING: host_lobby(); break;
    case NET_SCANNING: scan_poll(); break;
    case NET_CONNECTING: client_connecting(); break;
    case NET_JOINING: case NET_LOBBY: client_lobby(); break;
    case NET_PLAYING:
        if (net.port == 1) host_game();
        else client_game();
        break;
    }
    for (int p = 1; p < NET_PLAYERS; p++)
        if (peers[p].c.s >= 0) conn_flush(&peers[p].c);
    if (g_host.s >= 0) conn_flush(&g_host);
}

void net_leave(void) {
    for (int p = 1; p < NET_PLAYERS; p++)
        if (peers[p].c.s >= 0) { conn_send(&peers[p].c, MSG_BYE, NULL, 0, NULL, 0); conn_flush(&peers[p].c); }
    if (g_host.s >= 0) { conn_send(&g_host, MSG_BYE, NULL, 0, NULL, 0); conn_flush(&g_host); }
    if (net.state == NET_CONNECTING) connect_cancel();
    if (net.port == 1) upnp_close();
    close_all();
    net.state = NET_OFF;
    net.port = 0;
}

const uint8_t *net_profile(int port, int *len) {
    *len = port >= 1 && port <= NET_PLAYERS ? g_nprofiles[port - 1] : 0;
    return *len ? g_profiles[port - 1] : NULL;
}

const NetFrame *net_frame(uint32_t t) {
    return net.state == NET_PLAYING && t < frames_have && t + RING > frames_have ? &frames[t % RING] : NULL;
}

void net_submit(uint32_t t, const NetInput *in) {
    if (t != mine_have || t >= frames_have + RING - net.delay - 1) return;
    mine[t % RING] = *in;
    mine_have++;
}

// ---- the game code: the host's internet address, in 7 letters and numbers ----

static const char CODE_CHARS[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";   // no I, L, O, U

static unsigned code_check(uint32_t ip) { return (ip * 2654435761u) >> 29; }   // 3 bits

void net_code(uint32_t addr, char *out) {
    uint32_t ip = ntohl(addr);
    uint64_t v = (uint64_t)ip << 3 | code_check(ip);
    for (int i = NET_CODE_LEN - 1; i >= 0; i--) { out[i] = CODE_CHARS[v & 31]; v >>= 5; }
    out[NET_CODE_LEN] = 0;
}

uint32_t net_code_addr(const char *code) {
    uint64_t v = 0;
    int n = 0;
    for (const char *p = code; *p; p++) {
        char c = *p >= 'a' && *p <= 'z' ? *p - 32 : *p;
        if (c == '-' || c == ' ') continue;
        if (c == 'O') c = '0';
        if (c == 'I' || c == 'L') c = '1';
        const char *k = strchr(CODE_CHARS, c);
        if (!k || !c) return 0;
        v = v << 5 | (k - CODE_CHARS);
        n++;
    }
    if (n != NET_CODE_LEN || v >> 35) return 0;
    uint32_t ip = (uint32_t)(v >> 3);
    return ip && (v & 7) == code_check(ip) ? htonl(ip) : 0;
}

int net_internet(uint32_t *addr, const char **why) {
    int st = upnp_state();
    if (addr) *addr = st == UPNP_OPEN ? upnp_address() : 0;
    if (why) *why = st == UPNP_FAILED ? upnp_why() : "";
    return st == UPNP_OPEN ? 1 : st == UPNP_WORKING ? 0 : -1;
}
