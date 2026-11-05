// The public games list. See lobby.h; the server's side is tools/lobby/.
//
// The server speaks plain text over HTTP (or HTTPS on the Vita):
//   POST <url>/host   v=3&name=Jah&players=1   "a game here" (again every HEARTBEAT_MS)
//   POST <url>/leave  v=3                      "gone"
//   GET  <url>/list?v=3                        one game a line: address <tab> players <tab> name
// v is NET_VERSION: only games this build can join are listed.
#include "lobby.h"
#include "net.h"
#include "cc_host.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/http.h>
#include <psp2/libssl.h>
#include <psp2/sysmodule.h>
#else
#include <netdb.h>
#include <time.h>
#endif

#define HEARTBEAT_MS 30000       // the server forgets a game it hasn't heard from in 90 s
#define REPLY_MAX 8192

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static int g_started;
static char g_url[256];
static int g_url_read;

// what the game wants (under g_lock)
static int g_want_listed, g_want_players, g_want_list;
static char g_want_name[32];
// what the thread has done
static int g_listed = LOBBY_OFF, g_list_state = LOBBY_OFF;
static LobbyGame g_games[LOBBY_MAX];
static int g_ngames;
static char g_why[96];
static int g_err;                // the last request's error code (the Vita's, 0: none)

static uint32_t now_ms(void) {
#ifdef __vita__
    return (uint32_t)(sceKernelGetProcessTimeWide() / 1000);
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
#endif
}

static void sleep_ms(int ms) {
#ifdef __vita__
    sceKernelDelayThread(ms * 1000);
#else
    usleep(ms * 1000);
#endif
}

static const char *server(void) {
    if (g_url_read) return g_url;
    g_url_read = 1;
    const char *e = getenv("LOBBY_URL");
    snprintf(g_url, sizeof g_url, "%s", e ? e : CC_LOBBY_URL);
#ifdef __vita__
    FILE *f = fopen("ux0:data/CastleCrashers/lobby.txt", "r");
    if (f) {
        char line[256];
        if (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n \t")] = 0;
            if (line[0]) snprintf(g_url, sizeof g_url, "%s", line);
        }
        fclose(f);
    }
#endif
    size_t n = strlen(g_url);   // no slash at the end: the paths add theirs
    while (n && g_url[n - 1] == '/') g_url[--n] = 0;
    return g_url;
}

int lobby_available(void) { return server()[0] != 0; }

static void set_why(const char *s) {
    pthread_mutex_lock(&g_lock);
    snprintf(g_why, sizeof g_why, "%s", s);
    pthread_mutex_unlock(&g_lock);
}

// ---- one HTTP request: the status (the reply's body in out), or -1 ----

#ifdef __vita__
static int g_http_ready;

static int http_init(void) {
    if (g_http_ready) return g_http_ready > 0;
    g_http_ready = -1;
    if ((g_err = sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS)) < 0) return 0;
    sceSslInit(300 * 1024);   // (an error is usually "already done")
    sceHttpInit(80 * 1024);
    g_err = 0;
    g_http_ready = 1;
    return 1;
}

static int http(const char *method, const char *path, const char *body, char *out, int outmax) {
    out[0] = 0;
    if (!http_init()) return -1;
    char url[384];
    snprintf(url, sizeof url, "%s%s", server(), path);
    int tmpl = sceHttpCreateTemplate("CastleCrashersVita/1.0", SCE_HTTP_VERSION_1_1, 1);
    if (tmpl < 0) { g_err = tmpl; return -1; }
    sceHttpSetResolveTimeOut(tmpl, 5 * 1000 * 1000);
    sceHttpSetConnectTimeOut(tmpl, 8 * 1000 * 1000);
    sceHttpSetRecvTimeOut(tmpl, 8 * 1000 * 1000);
    int status = -1;
    int conn = sceHttpCreateConnectionWithURL(tmpl, url, 0);
    g_err = conn < 0 ? conn : 0;
    if (conn >= 0) {
        int post = !strcmp(method, "POST");
        int req = sceHttpCreateRequestWithURL(conn, post ? SCE_HTTP_METHOD_POST : SCE_HTTP_METHOD_GET, url,
                                              body ? strlen(body) : 0);
        if (req < 0) g_err = req;
        if (req >= 0) {
            if (post) sceHttpAddRequestHeader(req, "Content-Type", "application/x-www-form-urlencoded", SCE_HTTP_HEADER_OVERWRITE);
            int r = sceHttpSendRequest(req, body, body ? strlen(body) : 0);
            if (r < 0) g_err = r;
            if (r >= 0 && sceHttpGetStatusCode(req, &status) >= 0) {
                int n = 0;
                while (n < outmax - 1) {
                    int k = sceHttpReadData(req, out + n, outmax - 1 - n);
                    if (k <= 0) break;
                    n += k;
                }
                out[n] = 0;
            } else {
                status = -1;
            }
            sceHttpDeleteRequest(req);
        }
        sceHttpDeleteConnection(conn);
    }
    sceHttpDeleteTemplate(tmpl);
    return status;
}
#else
// hosttest: plain http:// with sockets
static int http(const char *method, const char *path, const char *body, char *out, int outmax) {
    out[0] = 0;
    const char *u = server();
    if (strncasecmp(u, "http://", 7)) return -1;
    u += 7;
    char host[128], rest[256];
    int n = 0;
    while (*u && *u != ':' && *u != '/' && n < 127) host[n++] = *u++;
    host[n] = 0;
    int port = 80;
    if (*u == ':') port = atoi(++u);
    while (*u && *u != '/') u++;
    snprintf(rest, sizeof rest, "%s%s", u, path);
    struct hostent *he = gethostbyname(host);
    if (!he || he->h_addrtype != AF_INET) return -1;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct timeval tv = {8, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    memcpy(&a.sin_addr, he->h_addr_list[0], 4);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) { close(s); return -1; }
    char head[768];
    int hn = snprintf(head, sizeof head,
                      "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: CastleCrashersVita/1.0\r\n"
                      "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n\r\n",
                      method, rest, host, body ? (int)strlen(body) : 0);
    int ok = send(s, head, hn, 0) == hn;
    if (ok && body) ok = send(s, body, strlen(body), 0) == (int)strlen(body);
    static char buf[REPLY_MAX + 1024];
    int got = 0;
    while (ok && got < (int)sizeof buf - 1) {
        int r = recv(s, buf + got, sizeof buf - 1 - got, 0);
        if (r <= 0) break;
        got += r;
    }
    close(s);
    buf[got] = 0;
    if (!ok || got < 12 || strncmp(buf, "HTTP/1.", 7)) return -1;
    int status = atoi(buf + 9);
    char *b = strstr(buf, "\r\n\r\n");
    snprintf(out, outmax, "%s", b ? b + 4 : "");
    return status;
}
#endif

static void urlencode(const char *s, char *out, int cap) {
    static const char hex[] = "0123456789ABCDEF";
    int n = 0;
    for (; *s && n < cap - 4; s++) {
        unsigned char c = *s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out[n++] = c;
        else { out[n++] = '%'; out[n++] = hex[c >> 4]; out[n++] = hex[c & 15]; }
    }
    out[n] = 0;
}

static int post(const char *what, const char *body) {
    static char reply[256];
    char path[32];
    snprintf(path, sizeof path, "/%s", what);
    int st = http("POST", path, body, reply, sizeof reply);
    if (st != 200) {
        char why[96];
        if (st < 0 && g_err) snprintf(why, sizeof why, "Couldn't reach the public games list (%08X)", (unsigned)g_err);
        else if (st < 0) snprintf(why, sizeof why, "Couldn't reach the public games list");
        else snprintf(why, sizeof why, "The public games list said no (%d)", st);
        set_why(why);
    }
    return st == 200;
}

static void get_list(void) {
    static char reply[REPLY_MAX];
    char path[32];
    snprintf(path, sizeof path, "/list?v=%d", NET_VERSION);
    int st = http("GET", path, NULL, reply, sizeof reply);
    LobbyGame games[LOBBY_MAX];
    int n = 0;
    for (char *line = reply; st == 200 && *line && n < LOBBY_MAX;) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (t2) {
            *t1 = *t2 = 0;
            uint32_t a = inet_addr(line);
            if (a != INADDR_NONE && a) {
                games[n].addr = a;
                games[n].players = atoi(t1 + 1);
                snprintf(games[n].name, sizeof games[n].name, "%s", t2 + 1);
                n++;
            }
        }
        if (!end) break;
        line = end + 1;
    }
    pthread_mutex_lock(&g_lock);
    if (st == 200) {
        memcpy(g_games, games, n * sizeof *games);
        g_ngames = n;
        g_list_state = LOBBY_OK;
    } else {
        g_list_state = LOBBY_FAILED;
        if (st >= 0) snprintf(g_why, sizeof g_why, "The public games list isn't working (%d)", st);
        else if (g_err) snprintf(g_why, sizeof g_why, "Couldn't reach the public games list (%08X)", (unsigned)g_err);
        else snprintf(g_why, sizeof g_why, "Couldn't reach the public games list");
    }
    pthread_mutex_unlock(&g_lock);
}

static void *lobby_thread(void *arg) {
    (void)arg;
    int listed = 0, players = 0;
    uint32_t last = 0;
    for (;;) {
        pthread_mutex_lock(&g_lock);
        int want = g_want_listed, want_players = g_want_players, list = g_want_list;
        char name[32];
        snprintf(name, sizeof name, "%s", g_want_name);
        g_want_list = 0;
        pthread_mutex_unlock(&g_lock);
        if (want && (!listed || want_players != players || now_ms() - last >= HEARTBEAT_MS)) {
            if (!listed && last && now_ms() - last < 5000) { sleep_ms(100); continue; }   // failed: again in 5 s
            char en[100], body[160];
            urlencode(name, en, sizeof en);
            snprintf(body, sizeof body, "v=%d&name=%s&players=%d", NET_VERSION, en, want_players);
            int ok = post("host", body);
            last = now_ms();
            listed = ok;
            players = want_players;
            pthread_mutex_lock(&g_lock);
            if (g_want_listed) g_listed = ok ? LOBBY_OK : LOBBY_FAILED;
            pthread_mutex_unlock(&g_lock);
            continue;
        }
        if (!want && listed) {
            char body[16];
            snprintf(body, sizeof body, "v=%d", NET_VERSION);
            post("leave", body);
            listed = 0;
            last = 0;
            continue;
        }
        if (!want) last = 0;
        if (list) { get_list(); continue; }
        sleep_ms(100);
    }
    return NULL;
}

static void start(void) {
    if (g_started || !lobby_available()) return;
    g_started = 1;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64 * 1024);
    pthread_create(&g_thread, &at, lobby_thread, NULL);
    pthread_attr_destroy(&at);
}

void lobby_publish(const char *name, int players) {
    start();
    if (!g_started) return;
    pthread_mutex_lock(&g_lock);
    if (!g_want_listed) g_listed = LOBBY_WORKING;
    g_want_listed = 1;
    g_want_players = players;
    snprintf(g_want_name, sizeof g_want_name, "%s", name);
    pthread_mutex_unlock(&g_lock);
}

void lobby_unpublish(void) {
    pthread_mutex_lock(&g_lock);
    g_want_listed = 0;
    g_listed = LOBBY_OFF;
    pthread_mutex_unlock(&g_lock);
}

int lobby_listed(void) {
    pthread_mutex_lock(&g_lock);
    int r = g_listed;
    pthread_mutex_unlock(&g_lock);
    return r;
}

void lobby_refresh(void) {
    start();
    if (!g_started) return;
    pthread_mutex_lock(&g_lock);
    g_want_list = 1;
    if (g_list_state == LOBBY_OFF || g_list_state == LOBBY_FAILED) g_list_state = LOBBY_WORKING;
    pthread_mutex_unlock(&g_lock);
}

int lobby_list_state(void) {
    pthread_mutex_lock(&g_lock);
    int r = g_list_state;
    pthread_mutex_unlock(&g_lock);
    return r;
}

int lobby_games(LobbyGame *out, int max) {
    pthread_mutex_lock(&g_lock);
    int n = g_ngames < max ? g_ngames : max;
    memcpy(out, g_games, n * sizeof *out);
    pthread_mutex_unlock(&g_lock);
    return n;
}

const char *lobby_why(void) {
    static char why[96];
    pthread_mutex_lock(&g_lock);
    snprintf(why, sizeof why, "%s", g_why);
    pthread_mutex_unlock(&g_lock);
    return why;
}
