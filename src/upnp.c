// Asking the home router to open the game's ports (UPnP IGD). See upnp.h.
//
// 1. SSDP: "any internet gateways?" to 239.255.255.250:1900 (and straight to the default
//    route, for routers that don't answer multicast); the answer's LOCATION is its description.
// 2. The description (XML) names the WANIPConnection (or WANPPPConnection) service's control URL.
// 3. SOAP to it: AddPortMapping for the lobby (TCP) and the frames (UDP), GetExternalIPAddress.
#include "upnp.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <psp2/net/netctl.h>
#endif

#define HTTP_MAX 32768
#define LEASE 0                    // for good (until upnp_close takes them back): a long game must not lose them
#define DESCRIPTION "Castle Crashers Vita"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_state;
static unsigned g_gen;             // each upnp_open/close: a thread still on an older one keeps quiet
static uint32_t g_addr;
static char g_why[96];
// what was opened (so it can be closed again)
static uint32_t g_ctl_ip;
static int g_ctl_port, g_ports[2], g_mapped;
static char g_ctl_path[256], g_service[80];

static void sleep_ms(int ms) {
#ifdef __vita__
    sceKernelDelayThread(ms * 1000);
#else
    usleep(ms * 1000);
#endif
}

static void set_addr(struct sockaddr_in *a, uint32_t ip, int port) {
    memset(a, 0, sizeof *a);
#ifdef __vita__
    a->sin_len = sizeof *a;
#endif
    a->sin_family = AF_INET;
    a->sin_port = htons(port);
    a->sin_addr.s_addr = ip;
}

static void nonblocking(int s) {
#ifdef __vita__
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NONBLOCK, &on, sizeof on);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

static uint32_t default_route(void) {   // network order, 0 if unknown
#ifdef __vita__
    SceNetCtlInfo info;
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_DEFAULT_ROUTE, &info) >= 0) {
        uint32_t a = inet_addr(info.default_route);
        if (a != INADDR_NONE) return a;
    }
#endif
    if (getenv("UPNP_ROUTER")) return inet_addr(getenv("UPNP_ROUTER"));
    return 0;
}

// "http://1.2.3.4:5678/path" -> ip, port, path. 0 if not that shape.
static int parse_url(const char *url, uint32_t *ip, int *port, char *path, int pathmax) {
    if (strncasecmp(url, "http://", 7)) return 0;
    url += 7;
    char host[64];
    int n = 0;
    while (*url && *url != ':' && *url != '/' && n < 63) host[n++] = *url++;
    host[n] = 0;
    *port = 80;
    if (*url == ':') *port = atoi(++url);
    while (*url && *url != '/') url++;
    snprintf(path, pathmax, "%s", *url ? url : "/");
    *ip = inet_addr(host);
    return *ip != INADDR_NONE;
}

// one HTTP request (Connection: close); the reply's body goes in out. The status, or -1.
// local: this Vita's address as the router sees it (for NewInternalClient).
static int http(uint32_t ip, int port, const char *method, const char *path, const char *action,
                const char *body, char *out, int outmax, uint32_t *local) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct timeval tv = {5, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    struct sockaddr_in a;
    set_addr(&a, ip, port);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) { close(s); return -1; }
    if (local) {
        struct sockaddr_in me;
        socklen_t ml = sizeof me;
        *local = getsockname(s, (struct sockaddr *)&me, &ml) == 0 ? me.sin_addr.s_addr : 0;
    }
    char head[768];
    struct in_addr ia = {ip};
    int hn = snprintf(head, sizeof head,
                      "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\nUser-Agent: CastleCrashersVita/1.0 UPnP/1.1\r\n",
                      method, path, inet_ntoa(ia), port);
    if (action)
        hn += snprintf(head + hn, sizeof head - hn,
                       "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"%s#%s\"\r\n", g_service, action);
    hn += snprintf(head + hn, sizeof head - hn, "Content-Length: %d\r\n\r\n", body ? (int)strlen(body) : 0);
    int ok = send(s, head, hn, 0) == hn;
    if (ok && body) ok = send(s, body, strlen(body), 0) == (int)strlen(body);
    int n = 0;
    while (ok && n < outmax - 1) {
        int r = recv(s, out + n, outmax - 1 - n, 0);
        if (r <= 0) break;
        n += r;
    }
    close(s);
    out[n] = 0;
    if (!ok || n < 12 || strncmp(out, "HTTP/1.", 7)) return -1;
    int status = atoi(out + 9);
    char *b = strstr(out, "\r\n\r\n");
    if (b) memmove(out, b + 4, strlen(b + 4) + 1);
    return status;
}

// the text of <tag>...</tag> (any namespace prefix) after from, before end (NULL: anywhere)
static int xml_text(const char *from, const char *end, const char *tag, char *out, int max) {
    char open[64];
    snprintf(open, sizeof open, "%s>", tag);
    for (const char *p = from; (p = strstr(p, open)); p++) {
        if (end && p >= end) return 0;
        if (p[-1] != '<' && p[-1] != ':') continue;
        const char *v = p + strlen(open), *e = strchr(v, '<');
        if (!e) return 0;
        int n = e - v < max - 1 ? e - v : max - 1;
        while (n > 0 && (*v == ' ' || *v == '\n' || *v == '\r' || *v == '\t')) { v++; n--; }
        memcpy(out, v, n);
        while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\n' || out[n - 1] == '\r')) n--;
        out[n] = 0;
        return 1;
    }
    return 0;
}

static int soap(const char *action, const char *args, char *out, int outmax) {
    char body[1536];
    snprintf(body, sizeof body,
             "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s</u:%s>"
             "</s:Body></s:Envelope>\r\n", action, g_service, args, action);
    return http(g_ctl_ip, g_ctl_port, "POST", g_ctl_path, action, body, out, outmax, NULL);
}

static int add_mapping(const char *proto, int port, uint32_t local, char *buf, int lease) {
    char args[640];
    struct in_addr ia = {local};
    snprintf(args, sizeof args,
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>%s</NewProtocol>"
             "<NewInternalPort>%d</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
             "<NewPortMappingDescription>" DESCRIPTION "</NewPortMappingDescription><NewLeaseDuration>%d</NewLeaseDuration>",
             port, proto, port, inet_ntoa(ia), lease);
    return soap("AddPortMapping", args, buf, HTTP_MAX);
}

static void del_mapping(const char *proto, int port, char *buf) {
    char args[256];
    snprintf(args, sizeof args,
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>%s</NewProtocol>", port, proto);
    soap("DeletePortMapping", args, buf, HTTP_MAX);
}

// a mapping for port; taken (718, e.g. ours from a game that crashed): drop it and ask again
static int map_port(const char *proto, int port, uint32_t local, char *buf, char *why, int whymax) {
    int st = add_mapping(proto, port, local, buf, LEASE);
    char code[16] = "";
    if (st != 200) xml_text(buf, NULL, "errorCode", code, sizeof code);
    if (st != 200 && !strcmp(code, "718")) {
        del_mapping(proto, port, buf);
        st = add_mapping(proto, port, local, buf, LEASE);
    }
    if (st != 200) {
        if (!code[0]) xml_text(buf, NULL, "errorCode", code, sizeof code);
        snprintf(why, whymax, "the router said no to %s %d (%d %s)", proto, port, st, code);
        return 0;
    }
    return 1;
}

typedef struct { unsigned gen; int tcp, udp; } Job;

static int still(unsigned gen) {
    pthread_mutex_lock(&g_lock);
    int r = gen == g_gen;
    pthread_mutex_unlock(&g_lock);
    return r;
}

static void finish(unsigned gen, int state, uint32_t addr, const char *why) {
    pthread_mutex_lock(&g_lock);
    if (gen == g_gen) {
        g_state = state;
        g_addr = addr;
        snprintf(g_why, sizeof g_why, "%s", why ? why : "");
    }
    pthread_mutex_unlock(&g_lock);
}

// 1: the router's description URL is in loc
static int discover(char *loc, int locmax) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return 0;
    struct sockaddr_in me;
    set_addr(&me, htonl(INADDR_ANY), 0);
    bind(s, (struct sockaddr *)&me, sizeof me);
    unsigned char ttl = 2;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    nonblocking(s);
    static const char *const kinds[] = {
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
    };
    uint32_t route = default_route();
    int found = 0;
    for (int round = 0; round < 3 && !found; round++) {
        for (int k = 0; k < 4; k++) {
            char m[256];
            int n = snprintf(m, sizeof m,
                             "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n", kinds[k]);
            struct sockaddr_in a;
            set_addr(&a, inet_addr("239.255.255.250"), 1900);
            sendto(s, m, n, 0, (struct sockaddr *)&a, sizeof a);
            if (route) {
                set_addr(&a, route, 1900);
                sendto(s, m, n, 0, (struct sockaddr *)&a, sizeof a);
            }
        }
        for (int w = 0; w < 25 && !found; w++) {   // 2.5 s of answers
            char b[1500];
            int n;
            while (!found && (n = recvfrom(s, b, sizeof b - 1, 0, NULL, NULL)) > 0) {
                b[n] = 0;
                // only gateways (other UPnP things answer some of these too)
                if (!strstr(b, "InternetGatewayDevice") && !strstr(b, "WANIPConnection") && !strstr(b, "WANPPPConnection")) continue;
                for (char *line = b; line && *line; line = strstr(line, "\n") ? strstr(line, "\n") + 1 : NULL) {
                    if (strncasecmp(line, "location:", 9)) continue;
                    char *v = line + 9;
                    while (*v == ' ') v++;
                    int l = 0;
                    while (v[l] && v[l] != '\r' && v[l] != '\n' && l < locmax - 1) l++;
                    memcpy(loc, v, l);
                    loc[l] = 0;
                    found = 1;
                    break;
                }
            }
            if (!found) sleep_ms(100);
        }
    }
    close(s);
    return found;
}

static int is_private(uint32_t a) {   // network order: an address the internet can't reach
    uint32_t h = ntohl(a);
    return (h >> 24) == 10 || (h >> 20) == (172 << 4 | 1) || (h >> 16) == (192 << 8 | 168) ||
           (h >> 22) == (100 << 2 | 1) || (h >> 16) == (169 << 8 | 254) || h == 0;
}

static void *open_thread(void *arg) {
    Job job = *(Job *)arg;
    free(arg);
    char *buf = malloc(HTTP_MAX), loc[256], why[96] = "";
    uint32_t ip, local = 0, ext = 0;
    int port, ok = 0;
    char path[256];
    if (!discover(loc, sizeof loc)) snprintf(why, sizeof why, "no router answered (UPnP off?)");
    else if (!parse_url(loc, &ip, &port, path, sizeof path) ||
             http(ip, port, "GET", path, NULL, NULL, buf, HTTP_MAX, &local) != 200)
        snprintf(why, sizeof why, "couldn't read the router's description");
    else {
        // the service to talk to, and where (relative to URLBase if given, else to the description)
        static const char *const services[] = {
            "urn:schemas-upnp-org:service:WANIPConnection:2",
            "urn:schemas-upnp-org:service:WANIPConnection:1",
            "urn:schemas-upnp-org:service:WANPPPConnection:1",
        };
        char ctl[256] = "", base[256] = "";
        xml_text(buf, NULL, "URLBase", base, sizeof base);
        for (int k = 0; k < 3 && !ctl[0]; k++) {
            char want[96];
            snprintf(want, sizeof want, ">%s<", services[k]);
            const char *at = strstr(buf, want);
            if (!at) continue;
            const char *end = strstr(at, "</service>");
            if (end && xml_text(at, end, "controlURL", ctl, sizeof ctl)) snprintf(g_service, sizeof g_service, "%s", services[k]);
        }
        if (!ctl[0]) snprintf(why, sizeof why, "the router can't open ports (no WAN service)");
        else {
            uint32_t bip = ip;
            int bport = port;
            char bpath[256];
            if (base[0]) parse_url(base, &bip, &bport, bpath, sizeof bpath);
            if (!strncasecmp(ctl, "http://", 7)) parse_url(ctl, &g_ctl_ip, &g_ctl_port, g_ctl_path, sizeof g_ctl_path);
            else {
                g_ctl_ip = bip;
                g_ctl_port = bport;
                snprintf(g_ctl_path, sizeof g_ctl_path, "%s%s", ctl[0] == '/' ? "" : "/", ctl);
            }
            if (!local) snprintf(why, sizeof why, "couldn't tell this Vita's address");
            else if (map_port("TCP", job.tcp, local, buf, why, sizeof why) &&
                     map_port("UDP", job.udp, local, buf, why, sizeof why)) {
                pthread_mutex_lock(&g_lock);
                g_ports[0] = job.tcp;
                g_ports[1] = job.udp;
                g_mapped = 1;
                pthread_mutex_unlock(&g_lock);
                char a[32];
                struct in_addr ia;
                if (soap("GetExternalIPAddress", "", buf, HTTP_MAX) == 200 &&
                    xml_text(buf, NULL, "NewExternalIPAddress", a, sizeof a) && inet_aton(a, &ia)) {
                    ext = ia.s_addr;
                    if (is_private(ext)) snprintf(why, sizeof why, "your internet provider shares your address (%s)", a);
                    else ok = 1;
                } else snprintf(why, sizeof why, "the router didn't give its internet address");
            }
        }
    }
    free(buf);
    if (still(job.gen)) finish(job.gen, ok ? UPNP_OPEN : UPNP_FAILED, ext, why);
    return NULL;
}

static void *close_thread(void *arg) {
    (void)arg;
    char *buf = malloc(HTTP_MAX);
    pthread_mutex_lock(&g_lock);
    int tcp = g_ports[0], udp = g_ports[1];
    pthread_mutex_unlock(&g_lock);
    del_mapping("TCP", tcp, buf);
    del_mapping("UDP", udp, buf);
    free(buf);
    return NULL;
}

static void spawn(void *(*fn)(void *), void *arg) {
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 128 * 1024);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &at, fn, arg)) {
        free(arg);
        pthread_mutex_lock(&g_lock);
        g_state = UPNP_FAILED;
        snprintf(g_why, sizeof g_why, "couldn't start");
        pthread_mutex_unlock(&g_lock);
    }
    pthread_attr_destroy(&at);
}

void upnp_open(int tcp_port, int udp_port) {
    Job *job = malloc(sizeof *job);
    pthread_mutex_lock(&g_lock);
    job->gen = ++g_gen;
    job->tcp = tcp_port;
    job->udp = udp_port;
    g_state = UPNP_WORKING;
    g_addr = 0;
    g_why[0] = 0;
    pthread_mutex_unlock(&g_lock);
    spawn(open_thread, job);
}

void upnp_close(void) {
    pthread_mutex_lock(&g_lock);
    g_gen++;
    int mapped = g_mapped;
    g_mapped = 0;
    g_state = UPNP_OFF;
    pthread_mutex_unlock(&g_lock);
    if (mapped) spawn(close_thread, NULL);
}

int upnp_state(void) {
    pthread_mutex_lock(&g_lock);
    int s = g_state;
    pthread_mutex_unlock(&g_lock);
    return s;
}

uint32_t upnp_address(void) {
    pthread_mutex_lock(&g_lock);
    uint32_t a = g_addr;
    pthread_mutex_unlock(&g_lock);
    return a;
}

const char *upnp_why(void) {
    static char why[96];
    pthread_mutex_lock(&g_lock);
    memcpy(why, g_why, sizeof why);
    pthread_mutex_unlock(&g_lock);
    return why;
}
