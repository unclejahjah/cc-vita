#include "audio.h"

#include <mpg123.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define OUT_RATE 44100
#define OUT_FRAMES 1024            // per output block (~23 ms)
#define MAX_SFX 512
#define MAX_VOICES 32
#define CACHE_BYTES (24 * 1024 * 1024)   // decoded sound effects kept in memory (of the 160 MB heap)
#define MUSIC_VOLUME 0.7f

enum { SFX_NONE, SFX_QUEUED, SFX_READY, SFX_FAILED };

typedef struct {
    char name[64];                 // linkage without extension
    volatile int state;
    int16_t *pcm;                  // stereo
    int frames, rate;
    uint32_t last_use;
} Sfx;

typedef struct {
    int active;
    int sfx;
    double pos;                    // in source frames
    float vl, vr;
    int loops;                     // further plays after this one
    const void *owner;
} Voice;

static char g_dir[128];
static Sfx g_sfx[MAX_SFX];
static int g_nsfx;
static size_t g_cache_bytes;
static uint32_t g_clock;
static Voice g_voices[MAX_VOICES];
static SceKernelLwMutexWork g_lock;
static int g_port = -1;
static volatile int g_quit;
static volatile float g_music_gain = 1, g_sfx_gain = 1;   // Help & Options' volume settings
static SceUID g_mix_thread = -1, g_load_thread = -1, g_load_sema = -1;

// music, owned by the mixer thread (requests come through these)
static char g_music_req[64];
static volatile int g_music_req_loop, g_music_req_seq;
static mpg123_handle *g_music;
static int g_music_loop, g_music_rate;
static char g_music_path[192];

static void lock(void) { sceKernelLockLwMutex(&g_lock, 1, NULL); }
static void unlock(void) { sceKernelUnlockLwMutex(&g_lock, 1); }

static mpg123_handle *open_mp3(const char *path, long *rate) {
    int err;
    mpg123_handle *h = mpg123_new(NULL, &err);
    if (!h) return NULL;
    mpg123_param(h, MPG123_ADD_FLAGS, MPG123_FORCE_STEREO | MPG123_QUIET, 0);
    if (mpg123_open(h, path) != MPG123_OK) { mpg123_delete(h); return NULL; }
    int ch, enc;
    if (mpg123_getformat(h, rate, &ch, &enc) != MPG123_OK) { mpg123_close(h); mpg123_delete(h); return NULL; }
    mpg123_format_none(h);
    mpg123_format(h, *rate, MPG123_STEREO, MPG123_ENC_SIGNED_16);
    return h;
}

// ---- sound effects: decoded whole on the loader thread ------------------------------

static void decode_sfx(Sfx *s) {
    char path[256];
    snprintf(path, sizeof path, "%ssfx/%s.mp3", g_dir, s->name);
    long rate;
    mpg123_handle *h = open_mp3(path, &rate);
    if (!h) { s->state = SFX_FAILED; return; }
    size_t cap = 65536, n = 0;   // frames
    int16_t *pcm = malloc(cap * 4);
    if (!pcm) { mpg123_close(h); mpg123_delete(h); s->state = SFX_FAILED; return; }
    for (;;) {
        if (n + 4096 > cap) {
            cap *= 2;
            int16_t *p = realloc(pcm, cap * 4);
            if (!p) break;
            pcm = p;
        }
        size_t got = 0;
        int r = mpg123_read(h, (unsigned char *)(pcm + n * 2), 4096 * 4, &got);
        n += got / 4;
        if (r != MPG123_OK && r != MPG123_NEW_FORMAT) break;
    }
    mpg123_close(h);
    mpg123_delete(h);
    if (!n) { free(pcm); s->state = SFX_FAILED; return; }
    lock();
    s->pcm = pcm;
    s->frames = (int)n;
    s->rate = (int)rate;
    g_cache_bytes += n * 4;
    s->state = SFX_READY;
    // over budget: drop the least recently used effects nobody is playing
    while (g_cache_bytes > CACHE_BYTES) {
        int victim = -1;
        for (int i = 0; i < g_nsfx; i++) {
            if (g_sfx[i].state != SFX_READY || &g_sfx[i] == s) continue;
            int playing = 0;
            for (int v = 0; v < MAX_VOICES; v++) playing |= g_voices[v].active && g_voices[v].sfx == i;
            if (!playing && (victim < 0 || g_sfx[i].last_use < g_sfx[victim].last_use)) victim = i;
        }
        if (victim < 0) break;
        g_cache_bytes -= (size_t)g_sfx[victim].frames * 4;
        free(g_sfx[victim].pcm);
        g_sfx[victim].pcm = NULL;
        g_sfx[victim].state = SFX_NONE;
    }
    unlock();
}

static int load_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    while (!g_quit) {
        sceKernelWaitSema(g_load_sema, 1, NULL);
        for (int i = 0; i < g_nsfx && !g_quit; i++)
            if (g_sfx[i].state == SFX_QUEUED) decode_sfx(&g_sfx[i]);
    }
    return 0;
}

// ---- mixing ------------------------------------------------------------------------

static void music_open(void) {
    if (g_music) { mpg123_close(g_music); mpg123_delete(g_music); g_music = NULL; }
    long rate;
    g_music = open_mp3(g_music_path, &rate);
    g_music_rate = (int)rate;
}

// Adds up to n frames of music at the output rate into mix.
static void mix_music(int32_t *mix, int n) {
    static int16_t buf[OUT_FRAMES * 2 * 2 + 8];
    static int have, used;           // frames in buf, frames consumed
    static double frac;
    if (!g_music) { have = used = 0; frac = 0; return; }
    double step = (double)g_music_rate / OUT_RATE;
    for (int i = 0; i < n; i++) {
        while (used + 1 >= have) {   // keep two frames to interpolate between
            int keep = have - used;
            if (keep > 0) memmove(buf, buf + used * 2, keep * 4);
            have = keep > 0 ? keep : 0;
            used = 0;
            size_t got = 0;
            int r = mpg123_read(g_music, (unsigned char *)(buf + have * 2), (OUT_FRAMES * 2 - have) * 4, &got);
            have += got / 4;
            if (r == MPG123_NEW_FORMAT) continue;
            if (r == MPG123_DONE || (r != MPG123_OK && !got)) {
                if (g_music_loop && mpg123_seek(g_music, 0, SEEK_SET) >= 0) continue;
                mpg123_close(g_music);
                mpg123_delete(g_music);
                g_music = NULL;
                return;
            }
        }
        float t = (float)frac;
        for (int c = 0; c < 2; c++) {
            float a = buf[used * 2 + c], b = buf[(used + 1) * 2 + c];
            mix[i * 2 + c] += (int32_t)((a + (b - a) * t) * (MUSIC_VOLUME * g_music_gain));
        }
        frac += step;
        while (frac >= 1) { frac -= 1; used++; }
    }
}

static int mix_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    static int16_t out[2][OUT_FRAMES * 2];
    static int32_t mix[OUT_FRAMES * 2];
    int which = 0, music_seq = 0;
    while (!g_quit) {
        // the latest music request: a track, or "" to stop
        if (g_music_req_seq != music_seq) {
            lock();
            music_seq = g_music_req_seq;
            int stop = !g_music_req[0];
            snprintf(g_music_path, sizeof g_music_path, "%smusic/%s.mp3", g_dir, g_music_req);
            g_music_loop = g_music_req_loop;
            unlock();
            if (stop) {
                if (g_music) { mpg123_close(g_music); mpg123_delete(g_music); g_music = NULL; }
            } else {
                music_open();
            }
        }
        memset(mix, 0, sizeof mix);
        mix_music(mix, OUT_FRAMES);
        float sfx = g_sfx_gain;
        lock();
        for (int v = 0; v < MAX_VOICES; v++) {
            Voice *vo = &g_voices[v];
            if (!vo->active) continue;
            Sfx *s = &g_sfx[vo->sfx];
            if (s->state != SFX_READY) {
                if (s->state == SFX_FAILED || s->state == SFX_NONE) vo->active = 0;
                continue;   // still decoding: starts when it's ready
            }
            double step = (double)s->rate / OUT_RATE;
            for (int i = 0; i < OUT_FRAMES; i++) {
                int p = (int)vo->pos;
                if (p >= s->frames) {
                    if (vo->loops > 0) { vo->loops--; vo->pos -= s->frames; p = (int)vo->pos; }
                    else { vo->active = 0; break; }
                }
                mix[i * 2] += (int32_t)(s->pcm[p * 2] * vo->vl * sfx);
                mix[i * 2 + 1] += (int32_t)(s->pcm[p * 2 + 1] * vo->vr * sfx);
                vo->pos += step;
            }
        }
        unlock();
        for (int i = 0; i < OUT_FRAMES * 2; i++) {
            int32_t x = mix[i];
            out[which][i] = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
        }
        sceAudioOutOutput(g_port, out[which]);   // blocks until the port takes it
        which ^= 1;
    }
    return 0;
}

// ---- API ---------------------------------------------------------------------------

int audio_init(const char *data_dir) {
    snprintf(g_dir, sizeof g_dir, "%s", data_dir);
    if (mpg123_init() != MPG123_OK) return 0;
    g_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, OUT_FRAMES, OUT_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (g_port < 0) return 0;
    sceKernelCreateLwMutex(&g_lock, "cc_audio", 0, 0, NULL);
    g_load_sema = sceKernelCreateSema("cc_sfx_load", 0, 0, 1 << 20, NULL);
    g_mix_thread = sceKernelCreateThread("cc_mix", mix_thread, 0x10000100 - 10, 0x10000, 0, 0, NULL);
    g_load_thread = sceKernelCreateThread("cc_sfx", load_thread, 0x10000100 + 10, 0x10000, 0, 0, NULL);
    if (g_mix_thread < 0 || g_load_thread < 0) return 0;
    sceKernelStartThread(g_mix_thread, 0, NULL);
    sceKernelStartThread(g_load_thread, 0, NULL);
    return 1;
}

void audio_set_volume(float music, float sfx) {
    // squared, so each step of the ten sounds about as big as the last
    g_music_gain = music < 0 ? 0 : music > 1 ? 1 : music * music;
    g_sfx_gain = sfx < 0 ? 0 : sfx > 1 ? 1 : sfx * sfx;
}

void audio_shutdown(void) {
    if (g_port < 0) return;
    g_quit = 1;
    sceKernelSignalSema(g_load_sema, 1);
    sceKernelWaitThreadEnd(g_mix_thread, NULL, NULL);
    sceKernelWaitThreadEnd(g_load_thread, NULL, NULL);
    sceAudioOutReleasePort(g_port);
    g_port = -1;
}

void audio_sfx_play(const char *linkage, float volume, float pan, int loops, const void *owner) {
    if (g_port < 0) return;
    char name[64];
    snprintf(name, sizeof name, "%s", linkage);
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;
    lock();
    int i;
    for (i = 0; i < g_nsfx && strcasecmp(g_sfx[i].name, name); i++) {}
    if (i == g_nsfx) {
        if (g_nsfx == MAX_SFX) { unlock(); return; }
        memset(&g_sfx[i], 0, sizeof g_sfx[i]);
        snprintf(g_sfx[i].name, sizeof g_sfx[i].name, "%s", name);
        g_nsfx++;
    }
    Sfx *s = &g_sfx[i];
    s->last_use = ++g_clock;
    if (s->state == SFX_FAILED) { unlock(); return; }
    if (s->state == SFX_NONE) {
        s->state = SFX_QUEUED;
        sceKernelSignalSema(g_load_sema, 1);
    }
    // a free voice, or the oldest
    int v, best = 0;
    for (v = 0; v < MAX_VOICES && g_voices[v].active; v++) {}
    if (v == MAX_VOICES) v = best;
    if (volume < 0) volume = 0;
    if (pan < -1) pan = -1;
    if (pan > 1) pan = 1;
    g_voices[v] = (Voice){1, i, 0, volume * (pan > 0 ? 1 - pan : 1), volume * (pan < 0 ? 1 + pan : 1), loops > 1 ? loops - 1 : 0,
                          owner};
    unlock();
}

// sound effects waiting for, or in, their first decode
int audio_sfx_pending(void) {
    int n = 0;
    for (int i = 0; i < g_nsfx; i++) n += g_sfx[i].state == SFX_QUEUED;
    return n;
}

void audio_sfx_stop(const void *owner) {
    if (g_port < 0) return;
    lock();
    for (int v = 0; v < MAX_VOICES; v++)
        if (!owner || g_voices[v].owner == owner) g_voices[v].active = 0;
    unlock();
}

void audio_music_play(const char *name, int loop) {
    if (g_port < 0) return;
    lock();
    snprintf(g_music_req, sizeof g_music_req, "%s", name);
    g_music_req_loop = loop;
    g_music_req_seq++;
    unlock();
}

void audio_music_stop(void) {
    if (g_port < 0) return;
    lock();
    g_music_req[0] = 0;
    g_music_req_seq++;
    unlock();
}
