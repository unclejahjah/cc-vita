// Netplay packets on the Vita: UDP over Wi-Fi (a router: the same home network, or the
// internet with the host's port forwarded), or ad hoc (Vita to Vita, P2P sockets).
#ifndef CC_NET_VITA_H
#define CC_NET_VITA_H

#include "netplay.h"

#define NET_PORT 47310   // UDP port (Wi-Fi) or virtual port (ad hoc)

enum { NET_WIFI, NET_ADHOC };

int net_vita_init(char *err, int errlen);   // loads the net modules; 1 on success
void net_vita_term(void);
// A host answers anyone and announces itself on the local network a few times a second.
int net_vita_host(NpTransport *t, int kind, char *err, int errlen);
// address: the host's IP for internet play, or NULL to take the first host heard announcing
// itself on the local network.
int net_vita_join(NpTransport *t, int kind, const char *address, char *err, int errlen);
int net_vita_found_host(const NpTransport *t);   // a client knows where the host is
void net_vita_close(NpTransport *t);
// This console's address as text ("" if none), for the lobby screen.
void net_vita_address(int kind, char *out, int outlen);

#endif
