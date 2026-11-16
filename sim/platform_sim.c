/* platform.h on a PC: the XenithOS simulator.
 *
 * Runs the real shell code with virtual time, scripted controller input, PNG
 * screenshots and a WAV recording of everything the sound mixer plays.
 *
 * Environment:
 *   XSIM_SCRIPT  input script (see sim/README.md)          required
 *   XSIM_OUT     folder for screenshots and audio.wav      default: out
 *   XSIM_W/H     screen size                               default: 1216 x 680 (720p minus overscan)
 *   XSIM_THREAD  1 = run the sound mixer on a real second thread, like the console does.
 *                The simulator then runs in real time instead of instantly.
 *
 * The working directory needs a folder literally named "uda:" standing in for the USB stick.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>

#include "audio.h"
#include "platform.h"
#include "tiled.h"

static int W = 1216, H = 680;
static uint32_t *last_frame;
static int frames_presented;
static uint64_t vtime;                       /* virtual microseconds */
static const char *out_dir = "out";
static struct tiled_fb tfb;          /* XSIM_TILED: fake console framebuffer */
static int tiled_on;

/* ------------------------------------------------------------- PNG output */

static void tiled_check(const char *when);

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void png_chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len)
{
    unsigned char hdr[8];
    uint32_t crc;
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    crc = crc32(0, (const unsigned char *)type, 4);
    if (len) crc = crc32(crc, data, len);
    put32(hdr, crc);
    fwrite(hdr, 1, 4, f);
}

static void save_png(const char *name)
{
    char path[512];
    unsigned char ihdr[13];
    size_t raw_len = (size_t)(W * 3 + 1) * H;
    unsigned char *raw = malloc(raw_len), *z;
    uLongf zlen = compressBound(raw_len);
    FILE *f;
    int x, y;

    if (!last_frame) { fprintf(stderr, "sim: shot %s before any frame\n", name); free(raw); return; }
    for (y = 0; y < H; y++) {
        unsigned char *row = raw + (size_t)y * (W * 3 + 1);
        row[0] = 0;
        for (x = 0; x < W; x++) {
            uint32_t p = last_frame[y * W + x];
            row[1 + x * 3] = p >> 16;
            row[2 + x * 3] = p >> 8;
            row[3 + x * 3] = p;
        }
    }
    z = malloc(zlen);
    compress2(z, &zlen, raw, raw_len, 6);

    snprintf(path, sizeof path, "%s/%s.png", out_dir, name);
    f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    put32(ihdr, W);
    put32(ihdr + 4, H);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)zlen);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    tiled_check(name);
    fprintf(stderr, "sim: wrote %s (frame %d, t=%.2fs)\n", path, frames_presented, vtime / 1e6);
}

/* ------------------------------------------------------------- WAV output */

static int16_t *wav;
static size_t wav_frames, wav_cap;
static uint64_t audio_start_us;
static int audio_on;

static void write_wav(void)
{
    char path[512];
    unsigned char h[44];
    uint32_t data_bytes = (uint32_t)(wav_frames * 4);
    FILE *f;
    size_t i;
    if (!audio_on) return;
    snprintf(path, sizeof path, "%s/audio.wav", out_dir);
    f = fopen(path, "wb");
    if (!f) return;
    memcpy(h, "RIFF", 4);
    h[4] = (36 + data_bytes); h[5] = (36 + data_bytes) >> 8; h[6] = (36 + data_bytes) >> 16; h[7] = (36 + data_bytes) >> 24;
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = h[18] = h[19] = 0;
    h[20] = 1; h[21] = 0;                       /* PCM */
    h[22] = 2; h[23] = 0;                       /* stereo */
    h[24] = PLAT_AUDIO_RATE & 0xff; h[25] = (PLAT_AUDIO_RATE >> 8) & 0xff; h[26] = h[27] = 0;
    {
        uint32_t br = PLAT_AUDIO_RATE * 4;
        h[28] = br; h[29] = br >> 8; h[30] = br >> 16; h[31] = br >> 24;
    }
    h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    h[40] = data_bytes; h[41] = data_bytes >> 8; h[42] = data_bytes >> 16; h[43] = data_bytes >> 24;
    fwrite(h, 1, 44, f);
    for (i = 0; i < wav_frames * 2; i++) {      /* little-endian samples */
        unsigned char s[2] = {(unsigned char)(wav[i] & 0xff), (unsigned char)((uint16_t)wav[i] >> 8)};
        fwrite(s, 1, 2, f);
    }
    fclose(f);
    fprintf(stderr, "sim: wrote %s (%.2fs)\n", path, wav_frames / (double)PLAT_AUDIO_RATE);
}

/* ----------------------------------------------------------- input script */

struct cmd { char op[16]; char arg[64]; int n, a, b; };
static struct cmd *cmds;
static int ncmds, pc, cmd_left = -1, polls;
static struct pad held;

static int set_button(struct pad *p, const char *name, int v)
{
    struct { const char *n; int *f; } map[] = {
        {"up", &p->up}, {"down", &p->down}, {"left", &p->left}, {"right", &p->right},
        {"a", &p->a}, {"b", &p->b}, {"x", &p->x}, {"y", &p->y}, {"lb", &p->lb}, {"rb", &p->rb},
        {"start", &p->start}, {"back", &p->back}, {"guide", &p->guide}, {"ls", &p->ls}, {"rs", &p->rs},
    };
    unsigned i;
    if (!strcasecmp(name, "lt")) { p->lt = v ? 255 : 0; return 1; }
    if (!strcasecmp(name, "rt")) { p->rt = v ? 255 : 0; return 1; }
    for (i = 0; i < sizeof map / sizeof *map; i++)
        if (!strcasecmp(name, map[i].n)) { *map[i].f = v; return 1; }
    return 0;
}

static void load_script(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    if (!f) { perror(path); exit(2); }
    while (fgets(line, sizeof line, f)) {
        struct cmd c;
        char *s = line;
        while (isspace((unsigned char)*s)) s++;
        if (!*s || *s == '#') continue;
        memset(&c, 0, sizeof c);
        c.n = -1;
        if (sscanf(s, "%15s %63s %d %d %d", c.op, c.arg, &c.n, &c.a, &c.b) < 1) continue;
        cmds = realloc(cmds, sizeof *cmds * (ncmds + 1));
        cmds[ncmds++] = c;
    }
    fclose(f);
}

/* Real time spent per frame on this PC (drawing + presenting), for before/after comparisons. */
static double work_total_ms, work_max_ms, last_present_ms;
static int work_frames;

static void finish(int code)
{
    struct audio_stats as;
    plat_thread_stop();
    tiled_check("end");
    write_wav();
    fprintf(stderr, "sim: done after %d polls, %d frames, %.2fs virtual\n", polls, frames_presented, vtime / 1e6);
    if (work_frames)
        fprintf(stderr, "sim: frame work on this PC: average %.2f ms, worst %.1f ms\n", work_total_ms / work_frames, work_max_ms);
    audio_get_stats(&as);
    fprintf(stderr, "sim: sound mixer on %s, %d ms queued, %d dropouts\n",
            as.threaded ? "its own thread" : "the main thread", as.queue_ms, as.dropouts);
    exit(code);
}

/* Advance the script by one controller poll. */
static void step_script(void)
{
    for (;;) {
        struct cmd *c;
        if (cmd_left > 0) { cmd_left--; return; }
        if (cmd_left == 0) {                     /* end of a press: release everything it held */
            memset(&held, 0, sizeof held);
            cmd_left = -1;
            pc++;
            return;                              /* one poll with everything released */
        }
        if (pc >= ncmds) finish(0);
        c = &cmds[pc];
        if (!strcmp(c->op, "wait")) {
            int n = atoi(c->arg);
            memset(&held, 0, sizeof held);
            cmd_left = n > 0 ? n - 1 : 0;
            if (cmd_left == 0) { pc++; cmd_left = -1; }
            return;
        } else if (!strcmp(c->op, "press") || !strcmp(c->op, "hold")) {
            memset(&held, 0, sizeof held);
            if (!set_button(&held, c->arg, 1)) { fprintf(stderr, "sim: unknown button %s\n", c->arg); finish(2); }
            cmd_left = (c->n > 0 ? c->n : 2) - 1;
            return;
        } else if (!strcmp(c->op, "stick")) {        /* stick lx|ly|rx|ry value polls */
            memset(&held, 0, sizeof held);
            if (!strcmp(c->arg, "lx")) held.lx = c->n;
            else if (!strcmp(c->arg, "ly")) held.ly = c->n;
            else if (!strcmp(c->arg, "rx")) held.rx = c->n;
            else if (!strcmp(c->arg, "ry")) held.ry = c->n;
            cmd_left = (c->a > 0 ? c->a : 2) - 1;
            return;
        } else if (!strcmp(c->op, "shot")) {
            save_png(c->arg);
            pc++;
        } else if (!strcmp(c->op, "quit")) {
            finish(0);
        } else {
            fprintf(stderr, "sim: unknown command %s\n", c->op);
            finish(2);
        }
    }
}

/* ------------------------------------------------------------- platform.h */

void plat_init(void)
{
    const char *s = getenv("XSIM_SCRIPT");
    if (getenv("XSIM_OUT")) out_dir = getenv("XSIM_OUT");
    if (getenv("XSIM_W")) W = atoi(getenv("XSIM_W"));
    if (getenv("XSIM_H")) H = atoi(getenv("XSIM_H"));
    mkdir(out_dir, 0755);
    if (!s) { fprintf(stderr, "sim: set XSIM_SCRIPT\n"); exit(2); }
    load_script(s);
    last_frame = calloc((size_t)W * H, 4);
    if (getenv("XSIM_TILED") && !strcmp(getenv("XSIM_TILED"), "1")) {
        int fw = 1280, fh = 720;
        uint32_t *fake = calloc((size_t)fw * fh, 4);
        tiled_init(&tfb, fake, fw, fh, (fw - W) / 2, (fh - H) / 2, W, H);
        tiled_on = 1;
    }
}

void plat_screen_size(int *w, int *h)
{
    *w = W;
    *h = H;
}


static double real_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

/* Read the fake framebuffer back and count pixels that don't match the last frame. */
static void tiled_check(const char *when)
{
    int x, y, bad = 0;
    if (!tiled_on) return;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if (tiled_read(&tfb, x, y) != last_frame[y * W + x]) bad++;
    fprintf(stderr, "sim: tiled framebuffer check (%s): %d wrong pixels", when, bad);
    fputc(10, stderr);
}

void plat_present(const uint32_t *px)
{
    double now;
    memcpy(last_frame, px, (size_t)W * H * 4);
    if (tiled_on) tiled_present(&tfb, px, NULL);
    frames_presented++;
    now = real_ms();
    if (last_present_ms > 0) {
        double d = now - last_present_ms;
        work_total_ms += d;
        if (d > work_max_ms) work_max_ms = d;
        work_frames++;
    }
    last_present_ms = now;
}

uint64_t plat_time_us(void)
{
    return __atomic_load_n(&vtime, __ATOMIC_SEQ_CST);
}

static int real_time;     /* set when the mixer thread runs: then time has to pass for real */

void plat_sleep_us(int us)
{
    if (us <= 0) return;
    if (real_time) usleep(us);
    __atomic_add_fetch(&vtime, (uint64_t)us, __ATOMIC_SEQ_CST);
}

void plat_poll(struct pad *p)
{
    polls++;
    if (polls > 200000) { fprintf(stderr, "sim: too many polls, stopping\n"); finish(3); }
    step_script();
    *p = held;
    p->connected = 1;
}

void plat_rumble(int left, int right)
{
    (void)left;
    (void)right;
}

void plat_audio_init(void)
{
    audio_on = 1;
    audio_start_us = vtime;
}

int plat_audio_queued(void)
{
    uint64_t played = (vtime - audio_start_us) * PLAT_AUDIO_RATE / 1000000;
    return wav_frames > played ? (int)(wav_frames - played) : 0;
}

void plat_audio_submit(const int16_t *stereo, int frames)
{
    uint64_t played = (vtime - audio_start_us) * PLAT_AUDIO_RATE / 1000000;
    /* The real chip plays silence when it runs dry; keep the recording in sync with that. */
    while (wav_frames < played) {
        if (wav_frames + 1 > wav_cap) { wav_cap = wav_cap ? wav_cap * 2 : 1 << 16; wav = realloc(wav, wav_cap * 4); }
        wav[wav_frames * 2] = wav[wav_frames * 2 + 1] = 0;
        wav_frames++;
    }
    if (wav_frames + frames > wav_cap) {
        while (wav_frames + frames > wav_cap) wav_cap = wav_cap ? wav_cap * 2 : 1 << 16;
        wav = realloc(wav, wav_cap * 4);
    }
    memcpy(wav + wav_frames * 2, stereo, (size_t)frames * 4);
    wav_frames += frames;
}

/* ------------------------------------------------------------ mixer thread */

static pthread_t thread;
static volatile int thread_run;
static void (*thread_step)(void);

static void *thread_main(void *arg)
{
    (void)arg;
    while (__atomic_load_n(&thread_run, __ATOMIC_SEQ_CST)) {
        thread_step();
        usleep(1000);
    }
    return NULL;
}

int plat_thread_start(void (*step)(void))
{
    const char *s = getenv("XSIM_THREAD");
    if (!s || strcmp(s, "1")) return 0;      /* default: everything on one thread, deterministic */
    if (thread_run) return 1;
    thread_step = step;
    real_time = 1;
    __atomic_store_n(&thread_run, 1, __ATOMIC_SEQ_CST);
    if (pthread_create(&thread, NULL, thread_main, NULL)) { thread_run = 0; return 0; }
    return 1;
}

void plat_thread_stop(void)
{
    if (!thread_run) return;
    __atomic_store_n(&thread_run, 0, __ATOMIC_SEQ_CST);
    pthread_join(thread, NULL);
}

static void (*hook_before)(void), (*hook_failed)(void);

void plat_set_launch_hooks(void (*before)(void), void (*failed)(void))
{
    hook_before = before;
    hook_failed = failed;
}

void plat_storage_init(void)
{
    mkdir("uda:", 0755);
}

int plat_bdev_next(int handle, const char **name)
{
    if (handle < 0) { *name = "uda"; return 0; }
    return -1;
}

int plat_temps(int c[4])
{
    c[0] = 52; c[1] = 57; c[2] = 49; c[3] = 41;   /* made-up values for screenshots */
    return 1;
}

const char *plat_video_out(void) { return "HDMI"; }
const char *plat_name(void) { return "Simulator"; }
const char *plat_cpu_name(void) { return "Simulated"; }

/* XSIM_KIND=pc makes the simulator act like the PC / Raspberry Pi build (desktop mode switch,
 * no XeLL), so those screens can be checked too. */
static int sim_is_pc(void)
{
    const char *k = getenv("XSIM_KIND");
    return k && !strcmp(k, "pc");
}
int plat_kind(void) { return sim_is_pc() ? PLAT_PC : PLAT_XBOX; }
int plat_can_exit_to_loader(void) { return !sim_is_pc(); }
int plat_has_desktop(void) { return sim_is_pc(); }
void plat_switch_to_desktop(void)
{
    if (hook_before) hook_before();
    fprintf(stderr, "sim: switch to desktop mode\n");
    finish(0);
}
void plat_audio_stop(void) { }

static void (*launch_progress)(int done, int total);

void plat_set_launch_progress(void (*fn)(int done, int total))
{
    launch_progress = fn;
}

/* A PC's Store update, acted out: a few seconds of "Building", then XSIM_UPDATE=fail says why
 * it didn't work, anything else succeeds. */
int plat_install_update(const char *tarball, void (*step)(const char *what, int seconds), char *why, int why_len)
{
    const char *u = getenv("XSIM_UPDATE");
    int i;
    fprintf(stderr, "sim: install update %s\n", tarball);
    for (i = 0; i < 240; i++)
        if (step) step(i < 30 ? "Unpacking" : "Building", i / 60);
    if (u && !strcmp(u, "fail")) {
        snprintf(why, why_len, "It didn't build. What went wrong is in update.log in XenithOS's data folder.");
        return 0;
    }
    return 1;
}

/* The guide, acted out (PCs). XSIM_GUIDE=1: launching an app opens the guide over a made-up game
 * picture, as if the Xbox button had been pressed in it; what's chosen there happens as on a
 * PC (Return home leaves a ".app" running, Switch game starts the one chosen). */
static int (*guide_fn)(struct plat_guide *g);
static char sim_running[128], sim_next[128];

void plat_set_guide(int (*fn)(struct plat_guide *g)) { guide_fn = fn; }
int plat_has_guide(void) { return sim_is_pc() && guide_fn != NULL; }
const char *plat_running(void) { return sim_running[0] ? sim_running : NULL; }

int plat_close_running(void (*step)(void))
{
    int i;
    for (i = 0; step && i < 30; i++) step();
    fprintf(stderr, "sim: closed %s\n", sim_running);
    sim_running[0] = 0;
    return 1;
}

const char *plat_next_launch(void)
{
    static char taken[128];
    if (!sim_next[0]) return NULL;
    snprintf(taken, sizeof taken, "%s", sim_next);
    sim_next[0] = 0;
    return taken;
}

/* A game picture: sky, hills and blocks, so the darkened backdrop has something in it. */
static void sim_game_picture(uint32_t *px)
{
    int x, y;
    for (y = 0; y < 720; y++)
        for (x = 0; x < 1280; x++) {
            int ground = 470 + (int)(40 * sin(x / 140.0)), c;
            if (y < ground) c = (int)(90 + 100 * y / 720.0);
            px[y * 1280 + x] = y < ground ? (uint32_t)((c / 3) << 16 | (c * 2 / 3) << 8 | (c + 60 > 255 ? 255 : c + 60))
                             : ((x / 48 + y / 48) % 2 ? 0x4c8a3cu : 0x5a9a46u);
        }
}

int plat_launch(const char *path)
{
    struct stat s;
    int total = stat(path, &s) == 0 && s.st_size > 0 ? (int)s.st_size : 1 << 20, i;
    if (launch_progress && !sim_is_pc())        /* as if reading it off the stick, like the Xbox */
        for (i = 1; i <= 4; i++) launch_progress(total * i / 4, total);
    save_png("launch");                         /* what's left on screen for the next program */
    if (hook_before) hook_before();
    fprintf(stderr, "sim: launch %s (not possible in the simulator)\n", path);
    if (sim_is_pc() && guide_fn && getenv("XSIM_GUIDE")) {
        static uint32_t picture[1280 * 720];
        struct plat_guide g;
        int other = strlen(path) > 4 && !strcmp(path + strlen(path) - 4, ".app"), c;
        sim_game_picture(picture);
        memset(&g, 0, sizeof g);
        g.backdrop = picture;
        g.path = path;
        g.keeps_running = other;
        g.pad_modes = other ? PAD_GAME | PAD_MOUSE | PAD_KEYS : 0;
        g.pad_mode = PAD_GAME;
        c = guide_fn(&g);
        fprintf(stderr, "sim: guide chose %d, controller %d, switch to %s\n", c, g.pad_mode, g.switch_to);
        if (c == GUIDE_HOME && other) snprintf(sim_running, sizeof sim_running, "%s", path);
        else if (!strcmp(sim_running, path)) sim_running[0] = 0;
        if (c == GUIDE_SWITCH) snprintf(sim_next, sizeof sim_next, "%s", g.switch_to);
    }
    if (hook_failed) hook_failed();
    return sim_is_pc();        /* as on a PC: the "app" ran and came back */
}

/* Apps from Flathub and Steam, acted out. XSIM_FLATPAK=none: Flatpak isn't installed.
 * XSIM_FLATPAK_FAIL=1: installing fails. XSIM_ARCH sets the processor (default x86_64).
 * XSIM_INSTALLED="tv.kodi.Kodi,com.valvesoftware.Steam" starts with those installed. */
static char sim_installed[1024];
static int sim_installed_init;

static void sim_installed_load(void)
{
    const char *s = getenv("XSIM_INSTALLED");
    if (sim_installed_init) return;
    sim_installed_init = 1;
    snprintf(sim_installed, sizeof sim_installed, ",%s,", s ? s : "");
}

const char *plat_arch(void)
{
    const char *a = getenv("XSIM_ARCH");
    return a && *a ? a : "x86_64";
}

int plat_flatpak_ready(void)
{
    const char *f = getenv("XSIM_FLATPAK");
    return sim_is_pc() && !(f && !strcmp(f, "none"));
}

int plat_flatpak_installed(const char *ref)
{
    char key[160];
    sim_installed_load();
    snprintf(key, sizeof key, ",%s,", ref);
    return strstr(sim_installed, key) != NULL;
}

int plat_flatpak(const char *verb, const char *ref, int (*step)(int percent, int seconds), char *why, int why_len)
{
    char key[160], *at;
    int i;
    sim_installed_load();
    if (!plat_flatpak_ready()) {
        snprintf(why, why_len, "Apps need Flatpak, which isn't on this machine yet. Run the XenithOS install command once more and come back to the Store.");
        return 0;
    }
    fprintf(stderr, "sim: flatpak %s %s\n", verb, ref);
    for (i = 0; i < 120; i++)
        if (step && !step(i < 10 ? -1 : (i - 10) * 100 / 110, i / 60)) return -1;
    if (getenv("XSIM_FLATPAK_FAIL")) {
        snprintf(why, why_len, "Unable to load summary from remote flathub: Could not resolve hostname");
        return 0;
    }
    snprintf(key, sizeof key, ",%s,", ref);
    if (!strcmp(verb, "uninstall") || !strcmp(verb, "purge")) {
        if ((at = strstr(sim_installed, key))) memmove(at, at + strlen(key) - 1, strlen(at + strlen(key) - 1) + 1);
    } else if (!strstr(sim_installed, key) && strlen(sim_installed) + strlen(ref) + 2 < sizeof sim_installed) {
        strcat(sim_installed, ref);
        strcat(sim_installed, ",");
    }
    return 1;
}

int plat_steam_ready(void)
{
    return plat_flatpak_installed("com.valvesoftware.Steam");
}

int plat_steam_open(const char *url)
{
    save_png("steam");
    fprintf(stderr, "sim: steam %s\n", url);
    return plat_steam_ready();
}

void plat_sync_apps(void) { }

/* XenithOS's own apps on a PC, acted out. XSIM_LIB is a folder standing in for /usr/lib/xenithos
 * (sim/run.sh makes one for XSIM_KIND=pc): each app's program, name and picture. */
static const char *sim_lib(void)
{
    const char *l = getenv("XSIM_LIB");
    return sim_is_pc() && l && *l ? l : NULL;
}

int plat_local_app(const char *id)
{
    char p[512];
    if (!sim_lib() || !id || !*id || strchr(id, '/')) return 0;
    snprintf(p, sizeof p, "%s/%s", sim_lib(), id);
    return access(p, F_OK) == 0;
}

int plat_local_app_file(const char *id, const char *ext, char *out, int n)
{
    if (!plat_local_app(id)) return 0;
    snprintf(out, n, "%s/%s.%s", sim_lib(), id, ext);
    if (access(out, R_OK) == 0) return 1;
    snprintf(out, n, "uda:/apps/%s.%s", id, ext);
    return access(out, R_OK) == 0;
}

static void sim_copy(const char *from, const char *to)
{
    char buf[8192];
    size_t got;
    FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
    while (in && out && (got = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, got, out);
    if (in) fclose(in);
    if (out) fclose(out);
}

int plat_local_app_set(const char *id, int on)
{
    static const char *const exts[] = {"elf32", "txt", "ximg"};
    char from[512], to[96];
    int i;
    if (!plat_local_app(id)) return 0;
    fprintf(stderr, "sim: %s %s\n", on ? "on the home screen:" : "off the home screen:", id);
    for (i = 0; i < 3; i++) {
        snprintf(to, sizeof to, "uda:/apps/%s.%s", id, exts[i]);
        if (!on) {
            remove(to);
            continue;
        }
        snprintf(from, sizeof from, "%s/%s%s%s", sim_lib(), id, i ? "." : "", i ? exts[i] : "");
        if (access(to, F_OK) != 0) sim_copy(from, to);
    }
    return 1;
}

const char *plat_launch_error(void) { return ""; }

/* Typing on a real keyboard: XSIM_TYPE="text" is typed the first time typing is switched on. */
static int typing_on, typed_once;
void plat_text_input(int on) { typing_on = on; }
int plat_text_take(char *out, int n)
{
    const char *t = getenv("XSIM_TYPE");
    int i;
    if (!typing_on || typed_once || !t) return 0;
    typed_once = 1;
    for (i = 0; t[i] && i < n; i++) out[i] = t[i] == '|' ? '\n' : t[i];
    return i;
}

void plat_reboot(void) { if (hook_before) hook_before(); fprintf(stderr, "sim: reboot\n"); finish(0); }
void plat_power_off(void) { if (hook_before) hook_before(); fprintf(stderr, "sim: power off\n"); finish(0); }
