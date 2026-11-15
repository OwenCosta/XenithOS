/* platform.h on PCs and Raspberry Pis: Linux through SDL2.
 *
 * Console mode: xenithos-session starts XenithOS on the bare console (no desktop), where
 * SDL draws straight to the screen through KMS/DRM. Started from a desktop instead, it opens
 * a window. The picture is drawn at 1280x720 and scaled to fit the screen.
 *
 * Files: the "uda:/..." paths the Xbox uses are kept. XenithOS changes into its data folder
 * (~/.local/share/xenithos, or $XENITHOS_DATA) which holds a folder named "uda:", so the
 * same paths work unchanged. Apps are Linux programs in uda:/apps, run as child processes.
 *
 * Controllers: anything SDL knows (Xbox, PlayStation, Switch Pro and most others). Keyboard:
 * arrows, Enter = A, Escape = B, Space = A, X / Y keys, Q / E = LB / RB, Tab = the power menu,
 * Page Up / Down = LT / RT.
 *
 * Apps run as their own programs but inside XenithOS's window (see "apps inside XenithOS"
 * below), so switching never changes the screen or the sound device. Holding Guide for two
 * seconds, or Ctrl+Alt+Backspace, closes an app that has stopped responding.
 *
 * Testing: XENITHOS_SHOT=file.png and XENITHOS_SHOT_FRAME=n save frame n and quit, which
 * works with SDL_VIDEODRIVER=offscreen on a machine with no screen.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include "platform.h"

#define SCREEN_W 1280
#define SCREEN_H 720

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_GameController *pad_dev;
static SDL_AudioDeviceID audio_dev;
static int windowed, frames, shot_frame = -1;
static const char *shot_path;
static char data_dir[600];
static char self_path[600];                  /* this program, to restart into an update */

static void (*hook_before)(void), (*hook_failed)(void);
static void (*launch_progress)(int done, int total);

/* ---------------------------------------------------- apps inside XenithOS
 *
 * XenithOS starts an app as its own program with XENITHOS_GUEST_FD set to this shared memory.
 * The app then opens no screen or sound device of its own: it draws its frames into
 * pixels[] and queues its sound in audio[], and XenithOS shows the frames in its window,
 * plays the sound and passes the controller and keyboard on through pad and text[]. The
 * screen never changes hands, so there's no black gap and no video mode switch, and
 * XenithOS still has its sound device when the app quits. */
#define GUEST_MAGIC 0x58475354u              /* "XGST" */
#define GUEST_VERSION 2
#define GUEST_FRAMES 3                        /* the app draws into one while XenithOS shows another */
#define GUEST_AUDIO_FRAMES 48000              /* a second of stereo sound */
#define GUEST_TEXT 256

struct guest_shm {
    uint32_t magic, version, size;
    /* XenithOS -> app */
    uint32_t pad_seq;                         /* odd while pad is being written */
    struct pad pad;
    uint32_t text_head;
    char text[GUEST_TEXT];
    int32_t host_queued;                      /* sound frames XenithOS still has queued */
    int32_t quit;                             /* the window was closed */
    /* app -> XenithOS */
    uint32_t pad_read;                        /* counts the app's reads, so presses last until seen */
    uint32_t frame_seq;                       /* pixels[frame_seq % GUEST_FRAMES] is the newest */
    int32_t rumble_l, rumble_r;
    uint32_t rumble_seq;
    int32_t text_on;
    uint32_t text_tail;
    uint32_t audio_head, audio_tail, audio_flush;
    int16_t audio[GUEST_AUDIO_FRAMES * 2];
    uint32_t pixels[GUEST_FRAMES][SCREEN_W * SCREEN_H];
};

static struct guest_shm *guest;               /* set when this program is an app shown by XenithOS */
static pid_t guest_host;

#define LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

static int guest_attach(void)
{
    const char *s = getenv("XENITHOS_GUEST_FD");
    struct guest_shm *g;
    int fd;
    if (!s || !*s) return 0;
    fd = atoi(s);
    unsetenv("XENITHOS_GUEST_FD");
    g = mmap(NULL, sizeof *g, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (g == MAP_FAILED) return 0;
    if (g->magic != GUEST_MAGIC || g->version != GUEST_VERSION || g->size != sizeof *g) {
        munmap(g, sizeof *g);
        return 0;
    }
    guest = g;
    guest_host = getppid();
    return 1;
}

/* ------------------------------------------------------------------ video */

static void video_open(void)
{
    Uint32 flags = windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "xenithos: no display: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    win = SDL_CreateWindow("XenithOS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, flags);
    if (!win) {
        fprintf(stderr, "xenithos: can't open a window: %s\n", SDL_GetError());
        exit(1);
    }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "xenithos: can't draw: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderPresent(ren);
}

static void video_close(void)
{
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    tex = NULL;
    ren = NULL;
    win = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static void quit_now(int code)
{
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    SDL_Quit();
    exit(code);
}

void plat_init(void)
{
    const char *s;
    ssize_t n;
    signal(SIGPIPE, SIG_IGN);
    if (guest_attach()) return;              /* an app inside XenithOS: no screen of its own */
    /* Where this program is, before an update can replace the file (after that,
     * /proc/self/exe reads "... (deleted)"). */
    n = readlink("/proc/self/exe", self_path, sizeof self_path - 1);
    self_path[n > 0 ? n : 0] = 0;
    /* In a desktop, a window; on the bare console (or when asked), the whole screen. */
    windowed = (getenv("DISPLAY") || getenv("WAYLAND_DISPLAY")) && !getenv("XENITHOS_FULLSCREEN");
    if (getenv("XENITHOS_WINDOW")) windowed = 1;
    shot_path = getenv("XENITHOS_SHOT");
    if ((s = getenv("XENITHOS_SHOT_FRAME"))) shot_frame = atoi(s);
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "xenithos: SDL: %s\n", SDL_GetError());
        exit(1);
    }
    video_open();
}

void plat_screen_size(int *w, int *h)
{
    *w = SCREEN_W;
    *h = SCREEN_H;
}

/* A plain PNG writer for XENITHOS_SHOT. */
static void put32(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len)
{
    unsigned char b[4];
    uint32_t crc = crc32(0, (const unsigned char *)type, 4);
    if (len) crc = crc32(crc, data, len);
    put32(b, len); fwrite(b, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    put32(b, crc); fwrite(b, 1, 4, f);
}

static void save_png(const char *path, const uint32_t *px)
{
    size_t raw_len = (size_t)SCREEN_H * (SCREEN_W * 3 + 1);
    unsigned char *raw = malloc(raw_len), hdr[13];
    uLongf zlen = compressBound(raw_len);
    unsigned char *z = malloc(zlen);
    FILE *f;
    int x, y;
    if (!raw || !z) { free(raw); free(z); return; }
    for (y = 0; y < SCREEN_H; y++) {
        unsigned char *r = raw + (size_t)y * (SCREEN_W * 3 + 1);
        *r++ = 0;
        for (x = 0; x < SCREEN_W; x++) {
            uint32_t p = px[(size_t)y * SCREEN_W + x];
            *r++ = p >> 16; *r++ = p >> 8; *r++ = p;
        }
    }
    compress2(z, &zlen, raw, raw_len, 6);
    f = fopen(path, "wb");
    if (f) {
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
        put32(hdr, SCREEN_W); put32(hdr + 4, SCREEN_H);
        hdr[8] = 8; hdr[9] = 2; hdr[10] = hdr[11] = hdr[12] = 0;
        chunk(f, "IHDR", hdr, 13);
        chunk(f, "IDAT", z, (uint32_t)zlen);
        chunk(f, "IEND", NULL, 0);
        fclose(f);
    }
    free(raw);
    free(z);
}

void plat_present(const uint32_t *px)
{
    if (guest) {
        uint32_t seq = __atomic_load_n(&guest->frame_seq, __ATOMIC_RELAXED) + 1;   /* only we write it */
        memcpy(guest->pixels[seq % GUEST_FRAMES], px, sizeof guest->pixels[0]);
        STORE(guest->frame_seq, seq);
        return;
    }
    if (!ren) return;
    SDL_UpdateTexture(tex, NULL, px, SCREEN_W * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    if (shot_path && frames == shot_frame) {
        save_png(shot_path, px);
        fprintf(stderr, "xenithos: saved %s\n", shot_path);
        quit_now(0);
    }
    frames++;
}

/* ------------------------------------------------------------------- time */

uint64_t plat_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

void plat_sleep_us(int us)
{
    struct timespec ts;
    if (us <= 0) return;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (long)(us % 1000000) * 1000;
    while (nanosleep(&ts, &ts) && errno == EINTR) { }
}

/* ------------------------------------------------------------------ input */

static int typing;
static char typed[64];
static int ntyped;

void plat_text_input(int on)
{
    typing = on;
    ntyped = 0;
    if (guest) {
        STORE(guest->text_tail, LOAD(guest->text_head));   /* nothing typed before counts */
        STORE(guest->text_on, on);
        return;
    }
    if (on) SDL_StartTextInput();
    else SDL_StopTextInput();
}

int plat_text_take(char *out, int n)
{
    int k;
    if (guest) {
        uint32_t head = LOAD(guest->text_head), tail = LOAD(guest->text_tail);
        for (k = 0; k < n && tail != head; k++, tail++) out[k] = guest->text[tail % GUEST_TEXT];
        STORE(guest->text_tail, tail);
        return k;
    }
    k = ntyped < n ? ntyped : n;
    memcpy(out, typed, (size_t)k);
    memmove(typed, typed + k, (size_t)(ntyped - k));
    ntyped -= k;
    return k;
}

static void type_char(char c)
{
    if (ntyped < (int)sizeof typed) typed[ntyped++] = c;
}

static void open_first_pad(void)
{
    int i;
    if (pad_dev) return;
    for (i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i) && (pad_dev = SDL_GameControllerOpen(i))) return;
}

static int axis(SDL_GameControllerAxis a)
{
    return pad_dev ? SDL_GameControllerGetAxis(pad_dev, a) : 0;
}

/* Presses since the last read_pad(), so a key or button tapped and let go between two frames
 * still counts once. */
static Uint8 tapped_key[SDL_NUM_SCANCODES], tapped_btn[SDL_CONTROLLER_BUTTON_MAX];
static const Uint8 *keys_now;

static int kd(SDL_Scancode s)
{
    return keys_now[s] || tapped_key[s];
}

static int btn(SDL_GameControllerButton b)
{
    return (pad_dev && SDL_GameControllerGetButton(pad_dev, b)) || tapped_btn[b];
}

/* Controllers coming and going, and typed text. Returns 1 for a request to close. */
static int handle_event(const SDL_Event *e, void (*put)(char c))
{
    switch (e->type) {
    case SDL_QUIT:
        return 1;
    case SDL_CONTROLLERDEVICEADDED:
        open_first_pad();
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (pad_dev && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_dev)) == e->cdevice.which) {
            SDL_GameControllerClose(pad_dev);
            pad_dev = NULL;
            open_first_pad();
        }
        break;
    case SDL_TEXTINPUT:
        if (typing) {
            const char *t = e->text.text;
            for (; *t; t++)
                if ((unsigned char)*t >= 32 && (unsigned char)*t < 127) put(*t);
        }
        break;
    case SDL_KEYDOWN:
        if (!e->key.repeat && e->key.keysym.scancode < SDL_NUM_SCANCODES) tapped_key[e->key.keysym.scancode] = 1;
        if (typing && e->key.keysym.sym == SDLK_BACKSPACE) put('\b');
        if (typing && (e->key.keysym.sym == SDLK_RETURN || e->key.keysym.sym == SDLK_KP_ENTER)) put('\n');
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        if (e->cbutton.button < SDL_CONTROLLER_BUTTON_MAX) tapped_btn[e->cbutton.button] = 1;
        break;
    }
    return 0;
}

static void read_pad(struct pad *p);

/* An app inside XenithOS: the controller and keyboard as XenithOS last saw them. */
static void guest_poll(struct pad *p)
{
    int tries;
    for (tries = 0; tries < 1000; tries++) {
        uint32_t s1 = LOAD(guest->pad_seq), s2;
        memcpy(p, &guest->pad, sizeof *p);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        s2 = LOAD(guest->pad_seq);
        if (!(s1 & 1) && s1 == s2) break;
    }
    if (tries == 1000) memset(p, 0, sizeof *p);
    STORE(guest->pad_read, LOAD(guest->pad_read) + 1);
    /* XenithOS's window was closed, or XenithOS itself is gone: this app goes too. */
    if (LOAD(guest->quit) || getppid() != guest_host) {
        if (hook_before) hook_before();
        quit_now(0);
    }
}

void plat_poll(struct pad *p)
{
    SDL_Event e;
    if (guest) {
        guest_poll(p);
        return;
    }
    while (SDL_PollEvent(&e))
        if (handle_event(&e, type_char)) {
            if (hook_before) hook_before();
            quit_now(0);
        }
    open_first_pad();
    read_pad(p);
}

/* The keyboard and the first controller, mapped to an Xbox pad. */
static void read_pad(struct pad *p)
{
    int ly, ry;

    keys_now = SDL_GetKeyboardState(NULL);
    memset(p, 0, sizeof *p);
    p->connected = 1;
    p->up = kd(SDL_SCANCODE_UP) || btn(SDL_CONTROLLER_BUTTON_DPAD_UP);
    p->down = kd(SDL_SCANCODE_DOWN) || btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    p->left = kd(SDL_SCANCODE_LEFT) || btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    p->right = kd(SDL_SCANCODE_RIGHT) || btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    /* While typing, Enter, Space and the letter keys belong to the text. */
    p->a = btn(SDL_CONTROLLER_BUTTON_A) || (!typing && (kd(SDL_SCANCODE_RETURN) || kd(SDL_SCANCODE_KP_ENTER) || kd(SDL_SCANCODE_SPACE)));
    p->b = btn(SDL_CONTROLLER_BUTTON_B) || kd(SDL_SCANCODE_ESCAPE);
    p->x = btn(SDL_CONTROLLER_BUTTON_X) || (!typing && kd(SDL_SCANCODE_X));
    p->y = btn(SDL_CONTROLLER_BUTTON_Y) || (!typing && kd(SDL_SCANCODE_Y));
    p->lb = btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || (!typing && kd(SDL_SCANCODE_Q));
    p->rb = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) || (!typing && kd(SDL_SCANCODE_E));
    p->start = btn(SDL_CONTROLLER_BUTTON_START) || (!typing && kd(SDL_SCANCODE_F1));
    p->back = btn(SDL_CONTROLLER_BUTTON_BACK) || (!typing && kd(SDL_SCANCODE_TAB));
    p->guide = btn(SDL_CONTROLLER_BUTTON_GUIDE);
    p->ls = btn(SDL_CONTROLLER_BUTTON_LEFTSTICK);
    p->rs = btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    /* SDL's sticks are positive down; XenithOS wants up positive. */
    ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
    ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
    p->lx = axis(SDL_CONTROLLER_AXIS_LEFTX);
    p->ly = ly == -32768 ? 32767 : -ly;
    p->rx = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    p->ry = ry == -32768 ? 32767 : -ry;
    if (!typing) {
        if (kd(SDL_SCANCODE_W)) p->ly = 32767;
        if (kd(SDL_SCANCODE_S)) p->ly = -32767;
        if (kd(SDL_SCANCODE_A)) p->lx = -32767;
        if (kd(SDL_SCANCODE_D)) p->lx = 32767;
    }
    p->lt = axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7;
    p->rt = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7;
    if (kd(SDL_SCANCODE_PAGEUP)) p->lt = 255;
    if (kd(SDL_SCANCODE_PAGEDOWN) || kd(SDL_SCANCODE_LCTRL)) p->rt = 255;
    memset(tapped_key, 0, sizeof tapped_key);
    memset(tapped_btn, 0, sizeof tapped_btn);
}

/* What an app inside XenithOS gets: buttons pressed at any point since its last read stay
 * pressed until it has read them; sticks and triggers are as they are now. */
static void merge_pad(struct pad *acc, const struct pad *p, int fresh)
{
    struct pad keep = *acc;
    *acc = *p;
    if (fresh) return;
    acc->up |= keep.up; acc->down |= keep.down; acc->left |= keep.left; acc->right |= keep.right;
    acc->a |= keep.a; acc->b |= keep.b; acc->x |= keep.x; acc->y |= keep.y;
    acc->lb |= keep.lb; acc->rb |= keep.rb; acc->start |= keep.start; acc->back |= keep.back;
    acc->guide |= keep.guide; acc->ls |= keep.ls; acc->rs |= keep.rs;
}

void plat_rumble(int left, int right)
{
    if (left < 0) left = 0;
    if (left > 255) left = 255;
    if (right < 0) right = 0;
    if (right > 255) right = 255;
    if (guest) {
        if (LOAD(guest->rumble_l) == left && LOAD(guest->rumble_r) == right) return;
        STORE(guest->rumble_l, left);
        STORE(guest->rumble_r, right);
        STORE(guest->rumble_seq, LOAD(guest->rumble_seq) + 1);
        return;
    }
    if (!pad_dev) return;
    SDL_GameControllerRumble(pad_dev, (Uint16)(left * 257), (Uint16)(right * 257), left || right ? 10000 : 0);
}

/* ------------------------------------------------------------------ audio */

void plat_audio_init(void)
{
    SDL_AudioSpec want, have;
    if (guest || audio_dev) return;          /* an app inside XenithOS plays through XenithOS */
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    memset(&want, 0, sizeof want);
    want.freq = PLAT_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = PLAT_AUDIO_BLOCK;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);
}

int plat_audio_queued(void)
{
    if (guest) return (int)(LOAD(guest->audio_head) - LOAD(guest->audio_tail)) + LOAD(guest->host_queued);
    return audio_dev ? (int)(SDL_GetQueuedAudioSize(audio_dev) / 4) : 0;
}

void plat_audio_submit(const int16_t *stereo, int frames)
{
    if (guest) {
        uint32_t head = __atomic_load_n(&guest->audio_head, __ATOMIC_RELAXED), tail = LOAD(guest->audio_tail);
        uint32_t space = GUEST_AUDIO_FRAMES - (head - tail);
        int i;
        if (frames > (int)space) frames = (int)space;      /* full: XenithOS has stopped taking it */
        for (i = 0; i < frames; i++) {
            uint32_t at = (head + (uint32_t)i) % GUEST_AUDIO_FRAMES;
            guest->audio[at * 2] = stereo[i * 2];
            guest->audio[at * 2 + 1] = stereo[i * 2 + 1];
        }
        if (frames > 0) STORE(guest->audio_head, head + (uint32_t)frames);
        return;
    }
    if (audio_dev && frames > 0) SDL_QueueAudio(audio_dev, stereo, (Uint32)frames * 4);
}

void plat_audio_stop(void)
{
    if (guest) {
        STORE(guest->audio_flush, LOAD(guest->audio_flush) + 1);   /* XenithOS drops what's queued */
        return;
    }
    if (audio_dev) SDL_ClearQueuedAudio(audio_dev);
}

static void audio_close(void)
{
    if (!audio_dev) return;
    SDL_CloseAudioDevice(audio_dev);
    audio_dev = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

/* ------------------------------------------------------------ mixer thread */

static pthread_t thread;
static void (*thread_step)(void);
static volatile int thread_run;

static void *thread_main(void *arg)
{
    (void)arg;
    while (__atomic_load_n(&thread_run, __ATOMIC_SEQ_CST)) {
        thread_step();
        plat_sleep_us(1000);
    }
    return NULL;
}

int plat_thread_start(void (*step)(void))
{
    if (thread_run) return 1;
    thread_step = step;
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

void plat_set_launch_hooks(void (*before)(void), void (*failed)(void))
{
    hook_before = before;
    hook_failed = failed;
}

/* Apps start as their own programs here. Until an app's first frame arrives, plat_launch
 * calls this with (0, 0) so the "Starting" screen keeps moving. */
void plat_set_launch_progress(void (*fn)(int done, int total))
{
    launch_progress = fn;
}

/* ---------------------------------------------------------------- storage */

static void mkdirs(const char *path)
{
    char tmp[600], *p;
    snprintf(tmp, sizeof tmp, "%s", path);
    for (p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

static void copy_if_missing(const char *from, const char *to)
{
    FILE *in, *out;
    char buf[4096];
    size_t n;
    if (access(to, F_OK) == 0 || !(in = fopen(from, "rb"))) return;
    if ((out = fopen(to, "wb"))) {
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
        fclose(out);
    }
    fclose(in);
}

void plat_storage_init(void)
{
    const char *d = getenv("XENITHOS_DATA"), *home = getenv("HOME");
    if (d && *d) snprintf(data_dir, sizeof data_dir, "%s", d);
    else snprintf(data_dir, sizeof data_dir, "%s/.local/share/xenithos", home ? home : "/tmp");
    mkdirs(data_dir);
    if (chdir(data_dir) != 0) {
        fprintf(stderr, "xenithos: can't use %s\n", data_dir);
        return;
    }
    mkdirs("uda:/xenith");
    mkdirs("uda:/apps");
    /* The installer leaves a config with the XenithOS Cloud address for new users. */
    copy_if_missing("/usr/share/xenithos/config.txt", "uda:/xenith/config.txt");
    if (access("uda:/xenith/config.txt", F_OK) != 0 && getenv("XENITHOS_SERVER")) {
        FILE *f = fopen("uda:/xenith/config.txt", "w");
        if (f) { fprintf(f, "server=%s\n", getenv("XENITHOS_SERVER")); fclose(f); }
    }
}

int plat_bdev_next(int handle, const char **name)
{
    if (handle < 0) { *name = "uda"; return 0; }
    return -1;
}

/* ----------------------------------------------------------------- system */

int plat_temps(int c[4])
{
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    long milli = -1;
    c[0] = c[1] = c[2] = c[3] = -1;
    if (!f) return 0;
    if (fscanf(f, "%ld", &milli) != 1) milli = -1;
    fclose(f);
    if (milli < 0) return 0;
    c[0] = (int)(milli / 1000);
    return 1;
}

const char *plat_video_out(void)
{
    static char out[64];
    const char *drv;
    if (guest) return "XenithOS window";
    drv = SDL_GetCurrentVideoDriver();
    const char *name = SDL_GetDisplayName(0);
    snprintf(out, sizeof out, "%s%s%s", name && *name ? name : "Display", drv ? ", " : "", drv ? drv : "");
    return out;
}

static void read_line_file(const char *path, char *out, int n)
{
    FILE *f = fopen(path, "r");
    int len;
    out[0] = 0;
    if (!f) return;
    if (!fgets(out, n, f)) out[0] = 0;
    fclose(f);
    len = (int)strlen(out);
    while (len && (out[len - 1] == '\n' || out[len - 1] == ' ' || out[len - 1] == '\r')) out[--len] = 0;
}

const char *plat_name(void)
{
    static char name[96];
    char model[96];
    if (name[0]) return name;
    read_line_file("/proc/device-tree/model", model, sizeof model);     /* Raspberry Pis */
    if (model[0]) {
        char *rev = strstr(model, " Rev ");
        if (rev) *rev = 0;
        snprintf(name, sizeof name, "%s", model);
        return name;
    }
    read_line_file("/sys/class/dmi/id/product_name", model, sizeof model);
    if (model[0] && strcmp(model, "To be filled by O.E.M.") && strcmp(model, "System Product Name"))
        snprintf(name, sizeof name, "PC (%s)", model);
    else
        snprintf(name, sizeof name, "PC");
    return name;
}

const char *plat_cpu_name(void)
{
    static char cpu[96];
    char line[256], model[128] = "";
    FILE *f;
    struct utsname u;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu[0]) return cpu;
    if ((f = fopen("/proc/cpuinfo", "r"))) {
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "model name", 10) && !model[0]) {
                char *v = strchr(line, ':'), *nl;
                if (v) {
                    for (v++; *v == ' '; v++) { }
                    snprintf(model, sizeof model, "%s", v);
                    if ((nl = strchr(model, '\n'))) *nl = 0;
                }
            }
        fclose(f);
    }
    if (!model[0] && uname(&u) == 0) snprintf(model, sizeof model, "%s", u.machine);
    snprintf(cpu, sizeof cpu, "%s, %ld cores", model[0] ? model : "Unknown", cores > 0 ? cores : 1);
    return cpu;
}

int plat_kind(void)
{
    return PLAT_PC;
}

/* ------------------------------------------------------------ running apps */

extern char **environ;
static struct guest_shm *host_shm;           /* the app running inside XenithOS, if any */

static void show_texture(void)
{
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

static void put_text(char c)
{
    uint32_t head = host_shm->text_head;     /* only XenithOS writes it */
    if (head - LOAD(host_shm->text_tail) >= GUEST_TEXT) return;
    host_shm->text[head % GUEST_TEXT] = c;
    STORE(host_shm->text_head, head + 1);
}

/* XenithOS's own sound stops (once the app is on screen, so the launch sound gets to play)
 * and the app's takes over the device. */
static int take_sound(void)
{
    if (hook_before) hook_before();
    if (audio_dev) SDL_ClearQueuedAudio(audio_dev);
    return 1;
}

static void pump_sound(struct guest_shm *g, uint32_t *flush_seen)
{
    uint32_t head = LOAD(g->audio_head), tail = g->audio_tail, fl = LOAD(g->audio_flush);
    if (fl != *flush_seen) {
        *flush_seen = fl;
        if (audio_dev) SDL_ClearQueuedAudio(audio_dev);
        tail = head;
    }
    while (head != tail) {
        uint32_t at = tail % GUEST_AUDIO_FRAMES, n = head - tail;
        if (n > GUEST_AUDIO_FRAMES - at) n = GUEST_AUDIO_FRAMES - at;
        if (audio_dev) SDL_QueueAudio(audio_dev, &g->audio[at * 2], n * 4);
        tail += n;
    }
    STORE(g->audio_tail, tail);
    STORE(g->host_queued, audio_dev ? (int32_t)(SDL_GetQueuedAudioSize(audio_dev) / 4) : 0);
}

/* The environment for an app: ours, minus the screenshot test settings, plus where the
 * shared memory is. Built before fork(), since only exec-safe calls may follow it while the
 * sound thread runs. */
static char **guest_env(int fd, char *fdvar, size_t fdvar_len)
{
    size_t n = 0, i, k = 0;
    char **env;
    while (environ[n]) n++;
    if (!(env = malloc(sizeof *env * (n + 2)))) return NULL;
    for (i = 0; i < n; i++)
        if (strncmp(environ[i], "XENITHOS_SHOT", 13) && strncmp(environ[i], "XENITHOS_GUEST_FD=", 18))
            env[k++] = environ[i];
    snprintf(fdvar, fdvar_len, "XENITHOS_GUEST_FD=%d", fd);
    env[k++] = fdvar;
    env[k] = NULL;
    return env;
}

/* Run an app inside XenithOS's window until it quits. Returns 0 if that couldn't be set up,
 * 1 once the app has run (its exit status in *status). */
static int run_inside(const char *path, int *status)
{
    char fdvar[48], *argv[2], **env;
    struct guest_shm *g;
    struct pad acc;
    uint32_t shown = 0, rumble_seen = 0, flush_seen = 0, read_seen = 0;
    int fd, took_sound = 0, text_on = 0, closing = 0;
    uint64_t start, guide_since = 0, asked_at = 0, closing_at = 0;
    pid_t pid;

    fd = memfd_create("xenithos-app", 0);
    if (fd < 0) return 0;
    g = ftruncate(fd, sizeof *g) == 0 ? mmap(NULL, sizeof *g, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
    if (g == MAP_FAILED) { close(fd); return 0; }
    g->magic = GUEST_MAGIC;
    g->version = GUEST_VERSION;
    g->size = sizeof *g;
    env = guest_env(fd, fdvar, sizeof fdvar);
    argv[0] = (char *)path;
    argv[1] = NULL;
    pid = env ? fork() : -1;
    if (pid == 0) {
        execve(path, argv, env);
        _exit(127);
    }
    free(env);
    if (pid < 0) { munmap(g, sizeof *g); close(fd); return 0; }
    host_shm = g;
    start = plat_time_us();

    for (;;) {
        SDL_Event e;
        struct pad p;
        uint32_t seq;
        uint64_t now;
        int want = LOAD(g->text_on);

        if (want != text_on) {                  /* the app's on-screen keyboard opened or closed */
            text_on = typing = want;
            if (want) SDL_StartTextInput();
            else SDL_StopTextInput();
        }
        while (SDL_PollEvent(&e)) {
            if (handle_event(&e, put_text)) {
                closing = 1;
                if (!closing_at) closing_at = plat_time_us();
                STORE(g->quit, 1);
            } else if (e.type == SDL_WINDOWEVENT && shown) {
                show_texture();
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_BACKSPACE &&
                       (e.key.keysym.mod & KMOD_CTRL) && (e.key.keysym.mod & KMOD_ALT) && !asked_at) {
                kill(pid, SIGTERM);
                asked_at = plat_time_us();
            }
        }
        open_first_pad();
        read_pad(&p);
        seq = LOAD(g->pad_read);
        merge_pad(&acc, &p, seq != read_seen || !g->pad_seq);
        read_seen = seq;
        STORE(g->pad_seq, g->pad_seq + 1);      /* odd: being written */
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        memcpy(&g->pad, &acc, sizeof acc);
        STORE(g->pad_seq, g->pad_seq + 1);

        seq = LOAD(g->rumble_seq);
        if (seq != rumble_seen) {
            rumble_seen = seq;
            plat_rumble(LOAD(g->rumble_l), LOAD(g->rumble_r));
        }

        seq = LOAD(g->frame_seq);
        if (seq != shown) {
            if (!took_sound) took_sound = take_sound();
            SDL_UpdateTexture(tex, NULL, g->pixels[seq % GUEST_FRAMES], SCREEN_W * 4);
            show_texture();
            shown = seq;
        } else if (!shown && launch_progress) {
            launch_progress(0, 0);               /* nothing from the app yet: keep "Starting" moving */
        }

        now = plat_time_us();
        if (!took_sound && now - start > 1500000) took_sound = take_sound();
        if (took_sound) pump_sound(g, &flush_seen);

        /* An app that stopped responding: Guide held for two seconds, Ctrl+Alt+Backspace, or
         * the window closed and the app not quitting by itself. */
        guide_since = p.guide ? (guide_since ? guide_since : now) : 0;
        if (!asked_at && ((guide_since && now - guide_since > 2000000) || (closing_at && now - closing_at > 2000000))) {
            kill(pid, SIGTERM);
            asked_at = now;
        }
        if (asked_at && now - asked_at > 1500000) kill(pid, SIGKILL);

        if (waitpid(pid, status, WNOHANG) == pid) break;
        SDL_Delay(2);
    }

    if (text_on) SDL_StopTextInput();
    typing = 0;
    plat_rumble(0, 0);
    if (took_sound && audio_dev) SDL_ClearQueuedAudio(audio_dev);
    host_shm = NULL;
    munmap(g, sizeof *g);
    close(fd);
    if (closing) {                               /* the window was closed: XenithOS goes too */
        if (!took_sound && hook_before) hook_before();
        quit_now(0);
    }
    if (took_sound && hook_failed) hook_failed();   /* XenithOS's sound starts again */
    return 1;
}

/* Apps are programs in uda:/apps, shown inside XenithOS's window (run_inside). Only if that
 * can't be set up does XenithOS hand the screen and sound over and take them back after. */
/* XenithOS starts again as the program now at self_path: the new version after an update. */
static void restart_self(void)
{
    char *argv[2];
    argv[0] = self_path;
    argv[1] = NULL;
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    audio_dev = 0;
    SDL_Quit();
    execv(self_path, argv);
    /* If that failed, quitting lets xenithos-session start it again. */
}

int plat_launch(const char *path)
{
    pid_t pid;
    int status = 0, ok;
    const char *slash = strrchr(path, '/');
    if (slash && !strcmp(slash + 1, "xenon.elf")) {       /* an app: back to XenithOS; XenithOS: restart */
        if (hook_before) hook_before();
        if (!guest && self_path[0]) restart_self();
        quit_now(0);
    }
    if (guest) return 0;                                  /* apps don't start other apps */
    if (access(path, X_OK) != 0) chmod(path, 0755);
    if (access(path, X_OK) != 0) return 0;

    if (run_inside(path, &status))
        return !(WIFEXITED(status) && WEXITSTATUS(status) == 127);

    if (hook_before) hook_before();
    plat_rumble(0, 0);
    if (pad_dev) { SDL_GameControllerClose(pad_dev); pad_dev = NULL; }
    audio_close();
    video_close();

    pid = fork();
    if (pid == 0) {
        execl(path, path, (char *)NULL);
        _exit(127);
    }
    ok = pid > 0;
    if (ok) while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    ok = ok && !(WIFEXITED(status) && WEXITSTATUS(status) == 127);

    video_open();
    plat_audio_init();                            /* audio_close() gave the device up */
    open_first_pad();
    if (hook_failed) hook_failed();               /* starts the sound again */
    return ok;
}

/* ------------------------------------------------------------ Store updates */

/* Run a program (absolute path), its output appended to log if there is one, calling step()
 * while it works. Returns its exit code, or -1 if it couldn't start. Only exec-safe calls
 * after fork(), since the sound threads are running. */
static int spawn_wait(char *const argv[], const char *log, void (*step)(const char *, int), const char *what,
                      uint64_t start)
{
    int st;
    pid_t pid = fork(), r;
    if (pid == 0) {
        if (log) {
            int fd = open(log, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
        }
        execv(argv[0], argv);
        _exit(127);
    }
    if (pid < 0) return -1;
    for (;;) {
        r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
        if (r < 0 && errno != EINTR) return -1;
        if (step) step(what, (int)((plat_time_us() - start) / 1000000));
        else plat_sleep_us(20000);
    }
}

/* The Store's "XenithOS update" on a PC: unpack the new source in ~/.cache/xenithos-update and
 * run the update.sh that comes with it, which builds everything and puts the programs in
 * place of the old ones (install.sh made /usr/lib/xenithos the user's for this). The old
 * version stays unless the new one built. */
int plat_install_update(const char *tarball, void (*step)(const char *what, int seconds), char *why, int why_len)
{
    char work[640], src[660], script[700], log[700], tar_abs[700];
    const char *home = getenv("HOME"), *cache = getenv("XDG_CACHE_HOME");
    uint64_t start = plat_time_us();
    int code;

    if (guest) {
        snprintf(why, why_len, "Only XenithOS itself can update.");
        return 0;
    }
    if (cache && *cache) snprintf(work, sizeof work, "%s/xenithos-update", cache);
    else snprintf(work, sizeof work, "%s/.cache/xenithos-update", home && *home ? home : "/tmp");
    snprintf(src, sizeof src, "%s/src", work);
    snprintf(script, sizeof script, "%s/linux/update.sh", src);
    snprintf(log, sizeof log, "%s/uda:/xenith/update.log", data_dir);
    if (tarball[0] == '/') snprintf(tar_abs, sizeof tar_abs, "%s", tarball);
    else snprintf(tar_abs, sizeof tar_abs, "%s/%s", data_dir, tarball);
    remove(log);
    {
        char *rm[] = {"/bin/rm", "-rf", work, NULL};
        spawn_wait(rm, NULL, step, "Getting ready", start);
    }
    mkdirs(src);
    {
        char *tar[] = {"/bin/tar", "-xzf", tar_abs, "-C", src, "--strip-components=1", NULL};
        if (spawn_wait(tar, log, step, "Unpacking", start) != 0) {
            snprintf(why, why_len, "The download didn't unpack. Try again.");
            return 0;
        }
    }
    if (access(script, R_OK) != 0) {
        snprintf(why, why_len, "That version can't install itself here.");
        return 0;
    }
    {
        char *build[] = {"/bin/bash", script, data_dir, NULL};
        code = spawn_wait(build, log, step, "Building", start);
    }
    if (code == 0) {
        char *rm[] = {"/bin/rm", "-rf", work, NULL};
        spawn_wait(rm, NULL, NULL, NULL, start);
        return 1;
    }
    if (code == 3)
        snprintf(why, why_len, "XenithOS can't replace itself yet. Run the install command once more, and updates come from the Store after that.");
    else
        snprintf(why, why_len, "It didn't build. What went wrong is in update.log in XenithOS's data folder.");
    return 0;
}

static void run(const char *const argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    if (pid > 0) while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) { }
}

void plat_reboot(void)
{
    static const char *const argv[] = {"systemctl", "reboot", NULL};
    if (hook_before) hook_before();
    run(argv);
    quit_now(0);
}

void plat_power_off(void)
{
    static const char *const argv[] = {"systemctl", "poweroff", NULL};
    if (hook_before) hook_before();
    run(argv);
    quit_now(0);
}

int plat_can_exit_to_loader(void)
{
    return 0;
}

/* Desktop mode is only offered when XenithOS runs as the console session (xenithos-session
 * sets XENITHOS_SESSION) and a desktop is installed. Switching writes the mode file and
 * quits; xenithos-session then starts the desktop. */
static const char *desktop_command(void)
{
    const char *d = getenv("XENITHOS_DESKTOP");
    if (d && *d) return d;
    if (access("/usr/bin/startxfce4", X_OK) == 0) return "/usr/bin/startxfce4";
    return NULL;
}

int plat_has_desktop(void)
{
    return getenv("XENITHOS_SESSION") && desktop_command();
}

void plat_switch_to_desktop(void)
{
    char path[600];
    const char *home = getenv("HOME");
    FILE *f;
    if (!plat_has_desktop() || !home) return;
    snprintf(path, sizeof path, "%s/.config/xenithos", home);
    mkdirs(path);
    snprintf(path, sizeof path, "%s/.config/xenithos/mode", home);
    if (!(f = fopen(path, "w"))) return;
    fprintf(f, "desktop\n");
    fclose(f);
    if (hook_before) hook_before();
    quit_now(0);
}
