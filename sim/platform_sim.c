/* platform.h on a PC: the XenithOS simulator.
 *
 * Runs the real shell code with virtual time, scripted controller input, PNG
 * screenshots and a WAV recording of everything the sound mixer plays.
 *
 * Environment:
 *   XSIM_SCRIPT  input script (see sim/README.md)          required
 *   XSIM_OUT     folder for screenshots and audio.wav      default: out
 *   XSIM_W/H     screen size                               default: 1216 x 680 (720p minus overscan)
 *
 * The working directory needs a folder literally named "uda:" standing in for the USB stick.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <zlib.h>

#include "platform.h"

static int W = 1216, H = 680;
static uint32_t *last_frame;
static int frames_presented;
static uint64_t vtime;                       /* virtual microseconds */
static const char *out_dir = "out";

/* ------------------------------------------------------------- PNG output */

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

static void finish(int code)
{
    write_wav();
    fprintf(stderr, "sim: done after %d polls, %d frames, %.2fs virtual\n", polls, frames_presented, vtime / 1e6);
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
}

void plat_screen_size(int *w, int *h)
{
    *w = W;
    *h = H;
}

void plat_present(const uint32_t *px)
{
    memcpy(last_frame, px, (size_t)W * H * 4);
    frames_presented++;
}

uint64_t plat_time_us(void)
{
    return vtime;
}

void plat_sleep_us(int us)
{
    if (us > 0) vtime += (uint64_t)us;
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

void plat_launch(const char *path)
{
    fprintf(stderr, "sim: launch %s (not possible in the simulator)\n", path);
}

void plat_reboot(void) { fprintf(stderr, "sim: reboot\n"); finish(0); }
void plat_power_off(void) { fprintf(stderr, "sim: power off\n"); finish(0); }
