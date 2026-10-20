// Netplay packets over UDP with BSD sockets: the PC harness (tools/hosttest.c, tools/nettest.c).
#include "net_posix.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int fd, is_host;
    struct sockaddr_in addr[NP_PORTS];   // host: clients by peer id (1..3); client: [0] the host
    int used[NP_PORTS];
    double loss;                         // tests: the share of packets thrown away
    unsigned rng;
} Udp;

static Udp udps[8];
static int nudps;

static int drop_this(Udp *u) {
    if (u->loss <= 0) return 0;
    u->rng = u->rng * 1103515245u + 12345u;
    return (u->rng >> 8) % 1000 < (unsigned)(u->loss * 1000);
}

static int udp_send(void *ctx, int peer, const void *buf, int len) {
    Udp *u = ctx;
    if (peer < 0 || peer >= NP_PORTS || !u->used[peer]) return -1;
    if (drop_this(u)) return len;
    return (int)sendto(u->fd, buf, len, 0, (struct sockaddr *)&u->addr[peer], sizeof u->addr[peer]);
}

static int udp_recv(void *ctx, void *buf, int cap, int *peer) {
    Udp *u = ctx;
    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n = (int)recvfrom(u->fd, buf, cap, 0, (struct sockaddr *)&from, &fl);
        if (n <= 0) return 0;
        if (drop_this(u)) continue;
        int id = -1;
        for (int i = 0; i < NP_PORTS; i++)
            if (u->used[i] && u->addr[i].sin_addr.s_addr == from.sin_addr.s_addr && u->addr[i].sin_port == from.sin_port)
                id = i;
        if (id < 0 && u->is_host)   // a new address: the next free client id
            for (int i = 1; i < NP_PORTS && id < 0; i++)
                if (!u->used[i]) { id = i; u->used[i] = 1; u->addr[i] = from; }
        if (id < 0) continue;
        *peer = id;
        return n;
    }
}

static void udp_drop(void *ctx, int peer) {
    Udp *u = ctx;
    if (peer > 0 && peer < NP_PORTS) u->used[peer] = 0;
}

static Udp *udp_open(int port, char *err, int errlen) {
    if (nudps >= 8) { snprintf(err, errlen, "too many sockets"); return NULL; }
    Udp *u = &udps[nudps];
    memset(u, 0, sizeof *u);
    u->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (u->fd < 0) { snprintf(err, errlen, "socket: %s", strerror(errno)); return NULL; }
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(u->fd, (struct sockaddr *)&a, sizeof a) < 0) {
        snprintf(err, errlen, "bind port %d: %s", port, strerror(errno));
        close(u->fd);
        return NULL;
    }
    fcntl(u->fd, F_SETFL, fcntl(u->fd, F_GETFL) | O_NONBLOCK);
    u->rng = 1 + nudps;
    nudps++;
    return u;
}

static NpTransport transport(Udp *u) { NpTransport t = {u, udp_send, udp_recv, udp_drop}; return t; }

int net_udp_host(NpTransport *t, int port, char *err, int errlen) {
    Udp *u = udp_open(port, err, errlen);
    if (!u) return 0;
    u->is_host = 1;
    *t = transport(u);
    return 1;
}

int net_udp_join(NpTransport *t, const char *host, int port, char *err, int errlen) {
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host, ps, &hints, &res) || !res) { snprintf(err, errlen, "can't find %s", host); return 0; }
    Udp *u = udp_open(0, err, errlen);
    if (!u) { freeaddrinfo(res); return 0; }
    memcpy(&u->addr[0], res->ai_addr, sizeof u->addr[0]);
    u->used[0] = 1;
    freeaddrinfo(res);
    *t = transport(u);
    return 1;
}

void net_udp_set_loss(NpTransport *t, double share) { ((Udp *)t->ctx)->loss = share; }
