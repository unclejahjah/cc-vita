// Internet play without port forwarding: the host's Vita asks the home router (UPnP IGD) to
// send the game's ports to it, and learns the router's internet address for the game code.
// Runs on its own thread: finding the router and talking to it takes a few seconds.
#ifndef UPNP_H
#define UPNP_H

#include <stdint.h>

enum { UPNP_OFF, UPNP_WORKING, UPNP_OPEN, UPNP_FAILED };

void upnp_open(int tcp_port, int udp_port);   // start asking (again: drops the last attempt)
void upnp_close(void);                        // take the ports back off the router (in the background)
int upnp_state(void);
uint32_t upnp_address(void);                  // UPNP_OPEN: the internet address, network order
const char *upnp_why(void);                   // UPNP_FAILED: what went wrong

#endif
