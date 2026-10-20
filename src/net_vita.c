// Netplay packets on the Vita (see net_vita.h). Every console uses one socket on NET_PORT.
// The host sends a beacon ("CN", version, 0x7F) to the broadcast address every half second;
// a client that wasn't given an address takes the first beacon's sender as its host.
// Ad hoc uses the same code on a P2P socket (the port is the virtual port); it needs the
// ad hoc link to be up, and is untested on hardware so far.
#include "net_vita.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BEACON 0x7F
#define BEACON_EVERY_US 500000
#define P2P_PORT 3658   // the system's port under the virtual ones

typedef struct {
    int fd, is_host, kind, found;
    SceNetSockaddrIn addr[NP_PORTS];   // host: clients by peer id (1..3); client: [0] the host
    int used[NP_PORTS];
    SceUInt64 last_beacon;
} VitaNet;

static VitaNet nets[2];
static void *net_memory;

int net_vita_init(char *err, int errlen) {
    if (net_memory) return 1;
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    const int size = 1024 * 1024;
    net_memory = malloc(size);
    SceNetInitParam p = {net_memory, size, 0};
    int r = sceNetInit(&p);
    if (r < 0 && r != (int)SCE_NET_ERROR_EBUSY) {
        snprintf(err, errlen, "network start failed (0x%08X)", r);
        free(net_memory);
        net_memory = NULL;
        return 0;
    }
    sceNetCtlInit();
    return 1;
}

void net_vita_term(void) {
    if (!net_memory) return;
    sceNetCtlTerm();
    sceNetTerm();
    free(net_memory);
    net_memory = NULL;
}

static SceNetSockaddrIn sockaddr(int kind, unsigned ip, int port) {
    SceNetSockaddrIn a;
    memset(&a, 0, sizeof a);
    a.sin_len = sizeof a;
    a.sin_family = SCE_NET_AF_INET;
    a.sin_addr.s_addr = ip;
    if (kind == NET_ADHOC) {
        a.sin_port = sceNetHtons(P2P_PORT);
        a.sin_vport = sceNetHtons(port);
    } else {
        a.sin_port = sceNetHtons(port);
    }
    return a;
}

static int same(const SceNetSockaddrIn *a, const SceNetSockaddrIn *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port && a->sin_vport == b->sin_vport;
}

static int vn_send(void *ctx, int peer, const void *buf, int len) {
    VitaNet *n = ctx;
    if (peer < 0 || peer >= NP_PORTS || !n->used[peer]) return -1;
    return sceNetSendto(n->fd, buf, len, 0, (SceNetSockaddr *)&n->addr[peer], sizeof n->addr[peer]);
}

static void beacon(VitaNet *n) {
    SceUInt64 now = sceKernelGetProcessTimeWide();
    if (now - n->last_beacon < BEACON_EVERY_US) return;
    n->last_beacon = now;
    static const uint8_t b[4] = {'C', 'N', 1, BEACON};
    SceNetSockaddrIn to = sockaddr(n->kind, SCE_NET_INADDR_BROADCAST, NET_PORT);
    sceNetSendto(n->fd, b, sizeof b, 0, (SceNetSockaddr *)&to, sizeof to);
}

static int vn_recv(void *ctx, void *buf, int cap, int *peer) {
    VitaNet *n = ctx;
    if (n->is_host) beacon(n);
    for (;;) {
        SceNetSockaddrIn from;
        unsigned int fl = sizeof from;
        int len = sceNetRecvfrom(n->fd, buf, cap, SCE_NET_MSG_DONTWAIT, (SceNetSockaddr *)&from, &fl);
        if (len <= 0) return 0;
        const uint8_t *b = buf;
        if (len >= 4 && b[0] == 'C' && b[1] == 'N' && b[3] == BEACON) {
            if (!n->is_host && !n->found) {   // the first host heard is ours
                n->addr[0] = from;
                n->used[0] = n->found = 1;
            }
            continue;
        }
        int id = -1;
        for (int i = 0; i < NP_PORTS; i++)
            if (n->used[i] && same(&n->addr[i], &from)) id = i;
        if (id < 0 && n->is_host)
            for (int i = 1; i < NP_PORTS && id < 0; i++)
                if (!n->used[i]) { id = i; n->used[i] = 1; n->addr[i] = from; }
        if (id < 0) continue;
        *peer = id;
        return len;
    }
}

static void vn_drop(void *ctx, int peer) {
    VitaNet *n = ctx;
    if (peer > 0 && peer < NP_PORTS) n->used[peer] = 0;
}

static VitaNet *vn_open(int kind, int is_host, char *err, int errlen) {
    VitaNet *n = &nets[is_host];
    if (kind == NET_ADHOC) {
        int state = 0;
        if (sceNetCtlAdhocGetState(&state) < 0 || state == 0) {
            snprintf(err, errlen, "No ad hoc connection (it's experimental: try Wi-Fi)");
            return NULL;
        }
    }
    memset(n, 0, sizeof *n);
    n->kind = kind;
    n->is_host = is_host;
    n->fd = sceNetSocket("cc_net", SCE_NET_AF_INET, kind == NET_ADHOC ? SCE_NET_SOCK_DGRAM_P2P : SCE_NET_SOCK_DGRAM, 0);
    if (n->fd < 0) {
        snprintf(err, errlen, "socket failed (0x%08X)", n->fd);
        return NULL;
    }
    int on = 1;
    sceNetSetsockopt(n->fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &on, sizeof on);
    sceNetSetsockopt(n->fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &on, sizeof on);
    SceNetSockaddrIn a = sockaddr(kind, SCE_NET_INADDR_ANY, NET_PORT);
    int r = sceNetBind(n->fd, (SceNetSockaddr *)&a, sizeof a);
    if (r < 0) {
        snprintf(err, errlen, "port %d is busy (0x%08X)", NET_PORT, r);
        sceNetSocketClose(n->fd);
        return NULL;
    }
    return n;
}

static NpTransport transport(VitaNet *n) { NpTransport t = {n, vn_send, vn_recv, vn_drop}; return t; }

int net_vita_host(NpTransport *t, int kind, char *err, int errlen) {
    VitaNet *n = vn_open(kind, 1, err, errlen);
    if (!n) return 0;
    *t = transport(n);
    return 1;
}

int net_vita_join(NpTransport *t, int kind, const char *address, char *err, int errlen) {
    unsigned ip = 0;
    if (address && *address && sceNetInetPton(SCE_NET_AF_INET, address, &ip) != 1) {
        snprintf(err, errlen, "\"%s\" isn't an IP address", address);
        return 0;
    }
    VitaNet *n = vn_open(kind, 0, err, errlen);
    if (!n) return 0;
    if (ip) {
        n->addr[0] = sockaddr(kind, ip, NET_PORT);
        n->used[0] = n->found = 1;
    }
    *t = transport(n);
    return 1;
}

int net_vita_found_host(const NpTransport *t) { return t->ctx && ((const VitaNet *)t->ctx)->found; }

void net_vita_close(NpTransport *t) {
    VitaNet *n = t->ctx;
    if (n && n->fd >= 0) sceNetSocketClose(n->fd);
    if (n) n->fd = -1;
    t->ctx = NULL;
}

void net_vita_address(int kind, char *out, int outlen) {
    out[0] = 0;
    SceNetInAddr a = {0};
    if (kind == NET_ADHOC) {
        if (sceNetCtlAdhocGetInAddr(&a) < 0) return;
        sceNetInetNtop(SCE_NET_AF_INET, &a, out, outlen);
    } else {
        SceNetCtlInfo info;
        if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) < 0) return;
        snprintf(out, outlen, "%s", info.ip_address);
    }
}
