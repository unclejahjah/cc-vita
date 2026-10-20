// Netplay over UDP for the PC harness (BSD sockets).
#ifndef CC_NET_POSIX_H
#define CC_NET_POSIX_H

#include "netplay.h"

int net_udp_host(NpTransport *t, int port, char *err, int errlen);                    // 1 on success
int net_udp_join(NpTransport *t, const char *host, int port, char *err, int errlen);  // 1 on success
void net_udp_set_loss(NpTransport *t, double share);   // tests: throw away this share of packets

#endif
