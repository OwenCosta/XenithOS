/* platform.h on PCs and Raspberry Pis: Linux through SDL2.
 *
 * Console mode: xenithos-session starts XenithOS on the bare console (no desktop). XenithOS
 * then starts its own X server and runs as its only program (see "console mode" below and
 * kiosk.c), so apps from Flathub and Steam open over it. Without an X server it draws
 * straight to the screen through KMS/DRM, as before. Started from a desktop instead, it opens
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
 * seconds, or Ctrl+Alt+Backspace, closes an app that has stopped responding. Other programs
 * (apps from Flathub, Steam and its games) get the screen to themselves: see "other programs".
 *
 * Testing: XENITHOS_SHOT=file.png and XENITHOS_SHOT_FRAME=n save frame n and quit, which
 * works with SDL_VIDEODRIVER=offscreen on a machine with no screen.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <SDL.h>
#include <SDL_syswm.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include "platform.h"
#include "platform_linux.h"
#include "theme.h"

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

/* A desktop app (see plat_desktop in platform.h): one of XenithOS Console's apps opened by
 * itself from a desktop's menu. The mouse adds up here until plat_mouse() reads it, and the
 * keyboard's presses and releases wait in key_queue for plat_key_event(). */
static int desktop, focused = 1, grabbed, want_grab;
static struct plat_mouse mouse_now = {-1, -1, 0, 0, 0, 0, 0};
#define KEY_QUEUE 64
static int key_queue[KEY_QUEUE], key_head, key_tail;

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

static int in_kiosk;                          /* console mode on XenithOS's own X server */

/* The window's name: the app's own, from the <program>.txt next to it (name=VOLT), so the
 * desktop's task list and window switcher say which app it is. */
static void window_title(char *out, int n)
{
    char path[640], line[128];
    FILE *f;
    snprintf(out, n, "XenithOS");
    if (!desktop || !self_path[0]) return;
    snprintf(path, sizeof path, "%s.txt", self_path);
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "name=", 5)) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[5]) snprintf(out, n, "%s", line + 5);
            break;
        }
    fclose(f);
}

static void video_open(void)
{
    Uint32 flags = windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP;
    int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED, w = SCREEN_W, h = SCREEN_H;
    SDL_DisplayMode dm;
    SDL_Rect room;
    char title[64];
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "xenithos: no display: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    /* No window manager is running yet to make a window full screen: cover the screen. */
    if (in_kiosk && SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        flags = SDL_WINDOW_BORDERLESS;
        x = y = 0;
        w = dm.w;
        h = dm.h;
    }
    /* A desktop app's window fits the screen with room round it, at most the picture's own size. */
    if (desktop && windowed && SDL_GetDisplayUsableBounds(0, &room) == 0 && room.w > 0 && room.h > 0) {
        w = room.w * 85 / 100;
        h = w * 9 / 16;
        if (h > room.h * 85 / 100) {
            h = room.h * 85 / 100;
            w = h * 16 / 9;
        }
        if (w > SCREEN_W) {
            w = SCREEN_W;
            h = SCREEN_H;
        }
    }
    window_title(title, sizeof title);
    win = SDL_CreateWindow(title, x, y, w, h, flags);
    if (!win) {
        fprintf(stderr, "xenithos: can't open a window: %s\n", SDL_GetError());
        exit(1);
    }
    if (desktop) SDL_SetWindowMinimumSize(win, SCREEN_W / 2, SCREEN_H / 2);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "xenithos: can't draw: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    SDL_ShowCursor(desktop ? SDL_ENABLE : SDL_DISABLE);
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

/* ------------------------------------------------------------- console mode
 *
 * Started by xenithos-session on the bare console, XenithOS starts its own X server with
 * startx (as desktop mode does) and runs as its only program; kiosk.c manages the windows apps
 * open over it. If that didn't get as far as XenithOS last time (a driver problem, say),
 * XenithOS draws straight to the screen instead, until it's updated. XENITHOS_KIOSK=0 turns
 * it off. The markers are in ~/.config/xenithos: kiosk-starting while X starts, kiosk-failed
 * (with the version) when it didn't. */
static void kiosk_path(char *out, int n, const char *name)
{
    const char *home = getenv("HOME");
    snprintf(out, n, "%s/.config/xenithos/%s", home && *home ? home : "/tmp", name);
}

static void kiosk_maybe_start(void)
{
    char startx[256], marker[600], failed[600], seen[32] = "";
    const char *k = getenv("XENITHOS_KIOSK");
    FILE *f;
    if (k && !strcmp(k, "1")) { in_kiosk = 1; return; }      /* this is XenithOS on its X server */
    if ((k && !strcmp(k, "0")) || !getenv("XENITHOS_SESSION") || getenv("DISPLAY") || getenv("WAYLAND_DISPLAY") || !self_path[0])
        return;
    if (!linux_find_program("startx", startx, sizeof startx)) return;    /* no X server (--no-desktop) */
    kiosk_path(marker, sizeof marker, "kiosk-starting");
    kiosk_path(failed, sizeof failed, "kiosk-failed");
    if ((f = fopen(failed, "r"))) {
        if (!fgets(seen, sizeof seen, f)) seen[0] = 0;
        fclose(f);
        if (!strncmp(seen, XENITH_VERSION "\n", strlen(XENITH_VERSION) + 1)) return;   /* not again until an update */
    }
    if (access(marker, F_OK) == 0) {
        rename(marker, failed);
        fprintf(stderr, "xenithos: console mode's X server didn't start last time: drawing to the screen directly\n");
        return;
    }
    if ((f = fopen(marker, "w"))) {
        fprintf(f, "%s\n", XENITH_VERSION);
        fclose(f);
    }
    setenv("XENITHOS_KIOSK", "1", 1);
    unsetenv("SDL_VIDEODRIVER");
    unsetenv("SDL_VIDEO_DRIVER");            /* sdl2-compat copies it under SDL3's name too */
    {
        /* -s 0 -dpms: a controller doesn't count as input to X, so never blank the screen. */
        char *argv[] = {startx, self_path, "--", "-keeptty", "-s", "0", "-dpms", NULL};
        execv(startx, argv);
    }
    remove(marker);                          /* startx couldn't even be run */
    unsetenv("XENITHOS_KIOSK");
}

/* Services started on demand over D-Bus, like xdg-desktop-portal and its backends (which apps
 * from Flathub ask for their settings, links and file pickers), get the environment of the
 * session's D-Bus, not ours. A desktop's session tells it the display; console mode didn't, so
 * those services couldn't start and every app waited for them to time out (25 seconds or so,
 * each time a game started). */
static void kiosk_tell_dbus(void)
{
    char prog[256], *argv[8];
    int n = 0, i, status;
    pid_t pid;
    setenv("XDG_SESSION_TYPE", "x11", 1);       /* on the console it says "tty" */
    if (!linux_find_program("dbus-update-activation-environment", prog, sizeof prog)) return;
    argv[n++] = prog;
    argv[n++] = "--systemd";
    argv[n++] = "DISPLAY";
    if (getenv("XAUTHORITY")) argv[n++] = "XAUTHORITY";
    argv[n++] = "XDG_SESSION_TYPE";
    argv[n] = NULL;
    pid = linux_spawn(argv, NULL, NULL, 0);
    for (i = 0; pid > 0 && i < 100 && waitpid(pid, &status, WNOHANG) == 0; i++) SDL_Delay(20);
    if (pid > 0 && i == 100) {                  /* 2 seconds: leave it */
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
}

/* XenithOS's window is up on its X server: manage the windows apps open over it. */
static void kiosk_begin(void)
{
    SDL_SysWMinfo info;
    char marker[600];
    if (!in_kiosk) return;
    kiosk_path(marker, sizeof marker, "kiosk-starting");
    remove(marker);                          /* X got as far as XenithOS */
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_X11 ||
        !kiosk_wm_start((unsigned long)info.info.x11.window))
        fprintf(stderr, "xenithos: can't manage windows on this X server\n");
    kiosk_tell_dbus();
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
    kiosk_maybe_start();                     /* on the bare console this becomes startx */
    /* In a desktop, a window; on the bare console, its own X server (or when asked), the
     * whole screen. */
    windowed = (getenv("DISPLAY") || getenv("WAYLAND_DISPLAY")) && !getenv("XENITHOS_FULLSCREEN") && !in_kiosk;
    if (getenv("XENITHOS_WINDOW")) windowed = 1;
    s = getenv("XENITHOS_DESKTOP");
    desktop = s && !strcmp(s, "1") && (getenv("DISPLAY") || getenv("WAYLAND_DISPLAY")) && !in_kiosk;
    shot_path = getenv("XENITHOS_SHOT");
    if ((s = getenv("XENITHOS_SHOT_FRAME"))) shot_frame = atoi(s);
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "xenithos: SDL: %s\n", SDL_GetError());
        exit(1);
    }
    video_open();
    kiosk_begin();
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

/* ------------------------------------------------------------ desktop apps */

int plat_desktop(void)
{
    return desktop;
}

/* A key as plat_key_event() names it (platform.h), or 0 for one it doesn't. */
static int key_code(SDL_Keycode k)
{
    if ((k >= SDLK_a && k <= SDLK_z) || (k >= SDLK_0 && k <= SDLK_9)) return (int)k;   /* ASCII in SDL */
    if (k >= SDLK_F1 && k <= SDLK_F12) return PLAT_KEY_F1 + (int)(k - SDLK_F1);
    switch (k) {
    case SDLK_SPACE: return ' ';
    case SDLK_MINUS: case SDLK_KP_MINUS: return '-';
    case SDLK_EQUALS: case SDLK_KP_PLUS: return '=';
    case SDLK_ESCAPE: return PLAT_KEY_ESC;
    case SDLK_RETURN: case SDLK_KP_ENTER: return PLAT_KEY_ENTER;
    case SDLK_TAB: return PLAT_KEY_TAB;
    case SDLK_BACKSPACE: return PLAT_KEY_BACKSPACE;
    case SDLK_DELETE: return PLAT_KEY_DELETE;
    case SDLK_UP: return PLAT_KEY_UP;
    case SDLK_DOWN: return PLAT_KEY_DOWN;
    case SDLK_LEFT: return PLAT_KEY_LEFT;
    case SDLK_RIGHT: return PLAT_KEY_RIGHT;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return PLAT_KEY_SHIFT;
    case SDLK_LCTRL: case SDLK_RCTRL: return PLAT_KEY_CTRL;
    case SDLK_LALT: case SDLK_RALT: return PLAT_KEY_ALT;
    case SDLK_PAUSE: return PLAT_KEY_PAUSE;
    case SDLK_HOME: return PLAT_KEY_HOME;
    case SDLK_END: return PLAT_KEY_END;
    case SDLK_PAGEUP: return PLAT_KEY_PAGEUP;
    case SDLK_PAGEDOWN: return PLAT_KEY_PAGEDOWN;
    default: return 0;
    }
}

/* The desktop's clipboard (copy and paste), as plain text. */
int plat_clipboard_set(const char *text)
{
    return desktop && win && SDL_SetClipboardText(text ? text : "") == 0;
}

int plat_clipboard_get(char *out, int n)
{
    char *t;
    if (n <= 0) return 0;
    out[0] = 0;
    if (!desktop || !win || !SDL_HasClipboardText() || !(t = SDL_GetClipboardText())) return 0;
    snprintf(out, (size_t)n, "%s", t);
    SDL_free(t);
    return out[0] != 0;
}

static void queue_key(int key, int down)
{
    int next = (key_head + 1) % KEY_QUEUE;
    if (!key || next == key_tail) return;            /* full: the app isn't reading them */
    key_queue[key_head] = down ? key : -key;
    key_head = next;
}

int plat_key_event(int *key, int *down)
{
    if (!desktop || key_tail == key_head) return 0;
    *down = key_queue[key_tail] > 0;
    *key = *down ? key_queue[key_tail] : -key_queue[key_tail];
    key_tail = (key_tail + 1) % KEY_QUEUE;
    return 1;
}

void plat_mouse(struct plat_mouse *m)
{
    *m = mouse_now;
    if (!desktop || m->x < 0 || m->y < 0 || m->x >= SCREEN_W || m->y >= SCREEN_H) m->x = m->y = -1;
    mouse_now.clicks = mouse_now.wheel = mouse_now.dx = mouse_now.dy = 0;
}

/* The pointer is held while the app wants it and its window is in front. */
static void apply_grab(void)
{
    int on = desktop && want_grab && focused;
    if (on == grabbed || !win) return;
    grabbed = on;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    mouse_now.dx = mouse_now.dy = 0;
}

void plat_mouse_grab(int on)
{
    want_grab = on;
    apply_grab();
}

static int mouse_bit(Uint8 button)
{
    return button == SDL_BUTTON_LEFT ? 1 : button == SDL_BUTTON_RIGHT ? 2 : button == SDL_BUTTON_MIDDLE ? 4 : 0;
}

/* The mouse, the keyboard's own presses and the window, for a desktop app. */
static void desktop_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_MOUSEMOTION:
        mouse_now.x = e->motion.x;
        mouse_now.y = e->motion.y;
        mouse_now.dx += e->motion.xrel;
        mouse_now.dy += e->motion.yrel;
        break;
    case SDL_MOUSEBUTTONDOWN:
        mouse_now.x = e->button.x;
        mouse_now.y = e->button.y;
        mouse_now.buttons |= mouse_bit(e->button.button);
        mouse_now.clicks |= mouse_bit(e->button.button);
        break;
    case SDL_MOUSEBUTTONUP:
        mouse_now.buttons &= ~mouse_bit(e->button.button);
        break;
    case SDL_MOUSEWHEEL:
        mouse_now.wheel += e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_LEAVE) mouse_now.x = mouse_now.y = -1;
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST || e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            focused = e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
            mouse_now.buttons = 0;
            apply_grab();
        }
        break;
    case SDL_KEYDOWN:
        if (e->key.repeat) break;
        if (e->key.keysym.sym == SDLK_F11) {             /* full screen and back */
            Uint32 f = SDL_GetWindowFlags(win);
            SDL_SetWindowFullscreen(win, (f & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
        }
        queue_key(key_code(e->key.keysym.sym), 1);
        break;
    case SDL_KEYUP:
        if (e->key.keysym.sym != SDLK_F11) queue_key(key_code(e->key.keysym.sym), 0);
        break;
    }
}

/* Controllers coming and going, and typed text. Returns 1 for a request to close. */
static int handle_event(const SDL_Event *e, void (*put)(char c))
{
    if (desktop) desktop_event(e);
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
    kiosk_wm_poll();
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

const char *linux_data_dir(void)
{
    return data_dir;
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

/* The guide (platform.h): what draws it, what Switch game chose, and the screen's picture. */
static int (*guide_fn)(struct plat_guide *g);
static char next_launch[128];
static uint32_t guide_shot[SCREEN_W * SCREEN_H];

void plat_set_guide(int (*fn)(struct plat_guide *g))
{
    guide_fn = fn;
}

int plat_has_guide(void)
{
    return guide_fn && in_kiosk && kiosk_wm_running();
}

const char *plat_next_launch(void)
{
    static char taken[128];
    if (!next_launch[0]) return NULL;
    snprintf(taken, sizeof taken, "%s", next_launch);
    next_launch[0] = 0;
    return taken;
}

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

/* The guide's sounds. While a game or app has the sound, XenithOS's mixer is stopped (and, for
 * a program of its own, its sound device closed). The guide starts them again for its own sounds
 * (a sound server mixes them in with the game's), and back in the game, they stop again. */
static int audio_close_needed;

static void guide_sound_on(void)
{
    audio_close_needed = !audio_dev;
    plat_audio_init();
    if (hook_failed) hook_failed();
}

static void guide_sound_off(void)
{
    if (hook_before) hook_before();
    if (audio_close_needed) audio_close();
    audio_close_needed = 0;
}

/* The guide over an app inside XenithOS: its last frame behind the panel. */
static int inside_guide(struct guest_shm *g, const char *path, uint32_t shown)
{
    struct plat_guide gd;
    int choice;
    memset(&gd, 0, sizeof gd);
    memcpy(guide_shot, g->pixels[shown % GUEST_FRAMES], sizeof guide_shot);
    gd.backdrop = guide_shot;
    gd.path = path;                              /* the guide looks its name up */
    guide_sound_on();
    choice = guide_fn(&gd);
    if (choice == GUIDE_RESUME) guide_sound_off();   /* the app's own sound again */
    if (choice == GUIDE_SWITCH) snprintf(next_launch, sizeof next_launch, "%s", gd.switch_to);
    return choice;
}

/* Run an app inside XenithOS's window until it quits. Returns 0 if that couldn't be set up,
 * 1 once the app has run (its exit status in *status). */
static int run_inside(const char *path, int *status)
{
    char fdvar[48], *argv[2], **env;
    struct guest_shm *g;
    struct pad acc;
    uint32_t shown = 0, rumble_seen = 0, flush_seen = 0, read_seen = 0;
    int fd, took_sound = 0, text_on = 0, closing = 0, esc_used = 0, hold_off = 0;
    uint64_t start, guide_since = 0, asked_at = 0, closing_at = 0, tap_since = 0, esc_since = 0;
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
        int want = LOAD(g->text_on), open = 0, guide_down;

        if (want != text_on) {                  /* the app's on-screen keyboard opened or closed */
            text_on = typing = want;
            if (want) SDL_StartTextInput();
            else SDL_StopTextInput();
        }
        kiosk_wm_poll();
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
        now = plat_time_us();
        guide_down = p.guide;
        p.guide = 0;                              /* the guide's button, not the app's */
        /* The guide: the Xbox button tapped (held for two seconds it still closes an app that
         * has stopped responding), or Esc held for half a second. */
        if (guide_down && !tap_since) tap_since = now;
        else if (!guide_down && tap_since) { open = now - tap_since < 1000000; tap_since = 0; }
        if (!keys_now[SDL_SCANCODE_ESCAPE]) esc_since = 0, esc_used = 0;
        else if (!esc_since) esc_since = now;
        if (esc_since && now - esc_since >= 500000 && !esc_used) open = esc_used = 1;
        if (open && guide_fn && shown && !asked_at) {
            if (inside_guide(g, path, shown) == GUIDE_RESUME) {
                SDL_UpdateTexture(tex, NULL, g->pixels[LOAD(g->frame_seq) % GUEST_FRAMES], SCREEN_W * 4);
                show_texture();
                hold_off = 1;
            } else {
                kill(pid, SIGTERM);              /* home, another app, or closed: this one quits */
                asked_at = plat_time_us();
            }
            memset(&p, 0, sizeof p);
            p.connected = 1;
            guide_down = 0;
        }
        if (hold_off) {          /* what was pressed in the guide isn't the app's until let go */
            if (p.a || p.b || p.x || p.y || p.start || p.back || p.lb || p.rb || p.up || p.down || p.left || p.right) {
                memset(&p, 0, sizeof p);
                p.connected = 1;
            } else {
                hold_off = 0;
            }
        }
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
        guide_since = guide_down ? (guide_since ? guide_since : now) : 0;
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

/* ------------------------------------------------------------ other programs
 *
 * Programs that aren't XenithOS apps (apps from Flathub, Steam and its games, "exec=" ones)
 * can't draw into XenithOS's window, so they open their own:
 *   in console mode, on XenithOS's own X server, their windows open over XenithOS's (kiosk.c
 *   makes them fill the screen), and XenithOS keeps its "Starting" screen moving until then;
 *   on the bare console (no X server), XenithOS lets go of the display and the program runs
 *   under cage, a kiosk Wayland compositor;
 *   in a desktop, XenithOS's window steps aside while the program opens its own.
 * Meanwhile XenithOS keeps watching the controller: Guide held for three seconds, or
 * Ctrl+Alt+Backspace, closes the program (asked first, then made to). A Steam game started
 * from XenithOS closes Steam again when the game ends, if Steam says when that is.
 */
static char launch_why[240];

const char *plat_launch_error(void)
{
    return launch_why;
}

/* Keyboards, read directly for Ctrl+Alt+Backspace and for Esc held down (the guide): with the
 * screen handed over, SDL sees none. Reading doesn't take the keys from the program. */
#define MAX_KBD 16
static int kbd_fd[MAX_KBD], nkbd, kbd_ctrl, kbd_alt;
static uint64_t kbd_esc_since;                 /* when Esc went down, 0 while it's up */

static void kbd_open(void)
{
    DIR *d = opendir("/dev/input");
    struct dirent *de;
    nkbd = kbd_ctrl = kbd_alt = 0;
    kbd_esc_since = 0;
    if (!d) return;
    while ((de = readdir(d)) && nkbd < MAX_KBD) {
        unsigned long bits[KEY_MAX / (8 * sizeof(long)) + 1];
        char path[300];
        int fd;
        if (strncmp(de->d_name, "event", 5)) continue;
        snprintf(path, sizeof path, "/dev/input/%s", de->d_name);
        if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0) continue;
        memset(bits, 0, sizeof bits);
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) >= 0 &&
            ((bits[KEY_BACKSPACE / (8 * sizeof(long))] >> (KEY_BACKSPACE % (8 * sizeof(long)))) & 1))
            kbd_fd[nkbd++] = fd;
        else
            close(fd);
    }
    closedir(d);
}

static int kbd_combo(void)
{
    struct input_event ev[32];
    int i, k, hit = 0;
    for (i = 0; i < nkbd; i++) {
        ssize_t got;
        while ((got = read(kbd_fd[i], ev, sizeof ev)) > 0)
            for (k = 0; k < (int)(got / (ssize_t)sizeof ev[0]); k++) {
                if (ev[k].type != EV_KEY) continue;
                if (ev[k].code == KEY_LEFTCTRL || ev[k].code == KEY_RIGHTCTRL) kbd_ctrl = ev[k].value != 0;
                else if (ev[k].code == KEY_LEFTALT || ev[k].code == KEY_RIGHTALT) kbd_alt = ev[k].value != 0;
                else if (ev[k].code == KEY_BACKSPACE && ev[k].value == 1 && kbd_ctrl && kbd_alt) hit = 1;
                else if (ev[k].code == KEY_ESC && ev[k].value != 2)       /* 2: auto-repeat */
                    kbd_esc_since = ev[k].value ? plat_time_us() : 0;
            }
    }
    return hit;
}

/* Forget what was pressed while XenithOS had the keyboard (in the guide). */
static void kbd_drain(void)
{
    kbd_combo();
    kbd_ctrl = kbd_alt = 0;
    kbd_esc_since = 0;
}

static void kbd_close(void)
{
    while (nkbd > 0) close(kbd_fd[--nkbd]);
}

/* Our environment, minus XenithOS's own settings: SDL_VIDEODRIVER=kmsdrm would send SDL games
 * to the bare console instead of the screen they're given. On systems where SDL2 is sdl2-compat
 * over SDL3, it's also copied under SDL3's names (SDL_VIDEO_DRIVER), which Sober and other
 * SDL3 programs read: those go too. */
static char **other_env(void)
{
    static const char *const drop[] = {"XENITHOS_", "SDL_VIDEODRIVER=", "SDL_VIDEO_DRIVER=", "SDL_AUDIODRIVER=",
                                       "SDL_AUDIO_DRIVER=", "SDL2_COMPAT"};
    size_t n = 0, i, k = 0, d;
    char **env;
    while (environ[n]) n++;
    if (!(env = malloc(sizeof *env * (n + 1)))) return NULL;
    for (i = 0; i < n; i++) {
        int keep = 1;
        for (d = 0; d < sizeof drop / sizeof drop[0]; d++)
            if (!strncmp(environ[i], drop[d], strlen(drop[d]))) keep = 0;
        if (keep) env[k++] = environ[i];
    }
    env[k] = NULL;
    return env;
}

/* Asks everything a program started to close (SIGTERM), deepest first, except the compositor,
 * Xwayland and Flatpak's sandbox (bwrap): if those went first, the rest would be killed
 * outright instead of getting to save and close. */
struct proc { pid_t pid, ppid; char comm[20]; };

static void ask_to_close(pid_t root)
{
    static struct proc all[4096];
    static pid_t tree[1024];
    int n = 0, nt = 0, i, j;
    DIR *d = opendir("/proc");
    struct dirent *de;
    if (d) {
        while ((de = readdir(d)) && n < 4096) {
            char path[64], buf[512], *open_p, *close_p;
            size_t got;
            FILE *f;
            if (!isdigit((unsigned char)de->d_name[0])) continue;
            snprintf(path, sizeof path, "/proc/%s/stat", de->d_name);
            if (!(f = fopen(path, "r"))) continue;
            got = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            buf[got] = 0;
            open_p = strchr(buf, '(');
            close_p = strrchr(buf, ')');
            if (!open_p || !close_p || close_p < open_p || !close_p[1]) continue;
            all[n].pid = atoi(buf);
            snprintf(all[n].comm, sizeof all[n].comm, "%.*s", (int)(close_p - open_p - 1), open_p + 1);
            if (sscanf(close_p + 2, "%*c %d", &all[n].ppid) == 1) n++;
        }
        closedir(d);
    }
    tree[nt++] = root;
    for (i = 0; i < nt; i++)
        for (j = 0; j < n && nt < 1024; j++)
            if (all[j].ppid == tree[i]) tree[nt++] = all[j].pid;
    for (i = nt - 1; i >= 0; i--) {
        const char *comm = "";
        for (j = 0; j < n; j++) if (all[j].pid == tree[i]) comm = all[j].comm;
        if (!strcmp(comm, "cage") || !strcmp(comm, "Xwayland") || !strcmp(comm, "bwrap") || !strcmp(comm, "xdg-dbus-proxy"))
            continue;
        kill(tree[i], SIGTERM);
    }
}

/* ------------------------------------------ the controller as a mouse or keyboard (console mode)
 *
 * For programs that don't take a controller, XenithOS turns it into what they do take, through
 * the XTEST extension (kiosk.c). It reads the controller as SDL sees it (so the buttons are
 * where the labels say on any controller), and the keyboard and a real mouse keep working.
 *   PAD_MOUSE  launchers and menus (Prism Launcher, Sober's first start): the left stick moves
 *              the pointer, A or RT clicks, B or LT right clicks, X middle clicks, the right
 *              stick scrolls, the d-pad sends the arrow keys, Y Enter, Start Esc, Back Tab.
 *   PAD_KEYS   a game played with a keyboard and mouse, laid out like Minecraft on an Xbox
 *              (Bedrock's default controls). Playing: the left stick is W A S D (pressed: Ctrl,
 *              sprint), the right stick looks around (pressed: middle click, pick block), A
 *              Space (jump), B Shift (sneak), X and Y E (crafting, inventory), RT left click
 *              (break), LT right click (place), LB and RB the hotbar, d-pad up F5 (camera), down
 *              Q (drop), left F (swap hands), right T (chat), Start Esc, Back Tab. In a menu
 *              (the game shows its pointer): the left stick moves the pointer, A left clicks, X
 *              right clicks, Y Shift-clicks, B and Start Esc, the right stick scrolls.
 * The guide switches between them and the game reading the controller itself. */
enum { EK_W, EK_A, EK_S, EK_D, EK_SPACE, EK_SHIFT, EK_CTRL, EK_Q, EK_E, EK_F, EK_T, EK_F5, EK_ESC, EK_TAB,
       EK_RETURN, EK_UP, EK_DOWN, EK_LEFT, EK_RIGHT, EK_COUNT };
static const unsigned long ek_sym[EK_COUNT] = {
    0x77, 0x61, 0x73, 0x64, 0x20, 0xffe1, 0xffe3, 0x71, 0x65, 0x66, 0x74, 0xffc2, 0xff1b, 0xff09,   /* X keysyms */
    0xff0d, 0xff52, 0xff54, 0xff51, 0xff53};
static int emu_mode;                           /* PAD_MOUSE or PAD_KEYS while it's on, else 0 */
static int emu_menu;                           /* PAD_KEYS: the game is showing a menu (pointer shown) */
static int emu_key[EK_COUNT], emu_button[4];   /* what's held down now */
static int emu_was[SDL_CONTROLLER_BUTTON_MAX];  /* buttons last time, for presses that step */
static float emu_fx, emu_fy, emu_sx, emu_sy;
static uint64_t emu_last, emu_looked;

static int emu_ready(void)
{
    return in_kiosk && kiosk_wm_can_point();
}

static int emu_on(void)
{
    return emu_mode != 0;
}

/* Sends what changed: keys and buttons go down and up as wanted. */
static void emu_send(const int *key, const int *button)
{
    int i;
    for (i = 0; i < EK_COUNT; i++)
        if (key[i] != emu_key[i]) kiosk_wm_key(ek_sym[i], emu_key[i] = key[i]);
    for (i = 1; i <= 3; i++)
        if (button[i] != emu_button[i]) kiosk_wm_pointer_button(i, emu_button[i] = button[i]);
}

static void emu_click(int button)
{
    kiosk_wm_pointer_button(button, 1);
    kiosk_wm_pointer_button(button, 0);
}

/* Lets go of everything it's holding down. */
static void emu_release(void)
{
    int none[EK_COUNT], up[4];
    memset(none, 0, sizeof none);
    memset(up, 0, sizeof up);
    emu_send(none, up);
}

static void emu_start(int mode)
{
    if (!emu_ready() || (mode != PAD_MOUSE && mode != PAD_KEYS)) return;
    emu_mode = mode;
    emu_menu = 0;
    memset(emu_key, 0, sizeof emu_key);
    memset(emu_button, 0, sizeof emu_button);
    memset(emu_was, 0, sizeof emu_was);
    emu_fx = emu_fy = emu_sx = emu_sy = 0;
    emu_last = emu_looked = 0;
}

static void emu_stop(void)
{
    if (!emu_mode) return;
    emu_release();
    emu_mode = 0;
}

/* Tests have no controller: XENITHOS_TEST_STICK="x,y" (-100..100) holds the left stick there, and
 * XENITHOS_TEST_BUTTON=a (or b, x, y) holds that button down. */
static int test_stick(float *x, float *y)
{
    const char *s = getenv("XENITHOS_TEST_STICK");
    int tx = 0, ty = 0;
    if (!s || sscanf(s, "%d,%d", &tx, &ty) != 2) return 0;
    *x = tx / 100.0f;
    *y = ty / 100.0f;
    return 1;
}

static int test_button(SDL_GameControllerButton b)
{
    const char *s = getenv("XENITHOS_TEST_BUTTON");
    return s && ((b == SDL_CONTROLLER_BUTTON_A && *s == 'a') || (b == SDL_CONTROLLER_BUTTON_B && *s == 'b') ||
                 (b == SDL_CONTROLLER_BUTTON_X && *s == 'x') || (b == SDL_CONTROLLER_BUTTON_Y && *s == 'y'));
}

static int pad_down(SDL_GameControllerButton b)
{
    return (pad_dev && SDL_GameControllerGetButton(pad_dev, b)) || test_button(b);
}

static float stick(SDL_GameControllerAxis a)
{
    float v = axis(a) / 32767.0f;
    return v < -1 ? -1 : v;
}

/* A stick as a speed: nothing in the middle, faster the further it's pushed. */
static void stick_speed(float x, float y, float slow, float fast, float dt, float *ax, float *ay)
{
    float mag = sqrtf(x * x + y * y), k;
    if (mag <= 0.15f) return;
    k = (mag - 0.15f) / 0.85f;
    if (k > 1) k = 1;
    *ax += x / mag * (slow + fast * k * k) * dt;
    *ay += y / mag * (slow + fast * k * k) * dt;
}

static void emu_step(uint64_t now)
{
    int key[EK_COUNT], button[4], b, pressed[SDL_CONTROLLER_BUTTON_MAX], dx, dy;
    float dt, lx, ly, rx, ry, lt, rt, tx, ty;
    int test = test_stick(&tx, &ty) || getenv("XENITHOS_TEST_BUTTON");
    if (!emu_mode || (!pad_dev && !test)) return;
    dt = emu_last ? (float)(now - emu_last) / 1e6f : 0;
    if (dt > 0.05f) dt = 0.05f;
    emu_last = now;
    for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
        int down = pad_down((SDL_GameControllerButton)b);
        pressed[b] = down && !emu_was[b];
        emu_was[b] = down;
    }
    if (!test_stick(&lx, &ly)) {
        lx = stick(SDL_CONTROLLER_AXIS_LEFTX);
        ly = stick(SDL_CONTROLLER_AXIS_LEFTY);
    }
    rx = stick(SDL_CONTROLLER_AXIS_RIGHTX);
    ry = stick(SDL_CONTROLLER_AXIS_RIGHTY);
    lt = axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
    rt = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
    /* Keyboard and mouse: playing, or in one of the game's menus? Minecraft hides the pointer
     * while you play and shows it in menus (the inventory, crafting, the pause menu). */
    if (emu_mode == PAD_KEYS && now - emu_looked > 100000) {
        int menu = kiosk_wm_pointer_hidden() == 0;
        emu_looked = now;
        if (menu != emu_menu) {
            emu_release();                       /* nothing held over from the other way */
            emu_menu = menu;
            emu_fx = emu_fy = emu_sx = emu_sy = 0;
        }
    }
    memset(key, 0, sizeof key);
    memset(button, 0, sizeof button);
#define DOWN(x) pad_down(SDL_CONTROLLER_BUTTON_##x)
    key[EK_ESC] = DOWN(START);
    key[EK_TAB] = DOWN(BACK);
    if (emu_mode == PAD_MOUSE) {
        stick_speed(lx, ly, 90, 2300, dt, &emu_fx, &emu_fy);        /* pixels a second */
        button[1] = DOWN(A) || rt > 0.5f;
        button[2] = DOWN(X);
        button[3] = DOWN(B) || lt > 0.5f;
        key[EK_RETURN] = DOWN(Y);
        key[EK_UP] = DOWN(DPAD_UP);
        key[EK_DOWN] = DOWN(DPAD_DOWN);
        key[EK_LEFT] = DOWN(DPAD_LEFT);
        key[EK_RIGHT] = DOWN(DPAD_RIGHT);
        /* The right stick scrolls, up to 14 steps a second. */
        if (fabsf(ry) > 0.25f) emu_sy -= ry * 14 * dt; else emu_sy = 0;
        if (fabsf(rx) > 0.25f) emu_sx += rx * 14 * dt; else emu_sx = 0;
    } else if (emu_menu) {
        /* A menu, the way the console does them: the left stick moves the pointer, A picks up
         * and puts down, X one at a time, Y moves a stack across (Shift-click), B closes. */
        stick_speed(lx, ly, 90, 1600, dt, &emu_fx, &emu_fy);
        if (fabsf(ry) > 0.25f) emu_sy -= ry * 14 * dt; else emu_sy = 0;
        key[EK_ESC] = DOWN(START) || DOWN(B);
        key[EK_SHIFT] = DOWN(Y);
        button[1] = DOWN(A) || DOWN(Y) || rt > 0.5f;
        button[3] = DOWN(X) || lt > 0.5f;
        if (pressed[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]) emu_sy += 1;
        if (pressed[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER]) emu_sy -= 1;
    } else {
        /* Playing, with Minecraft's own Xbox layout (Bedrock's defaults) */
        stick_speed(rx, ry, 40, 1400, dt, &emu_fx, &emu_fy);        /* looking around */
        key[EK_W] = ly < -0.35f;
        key[EK_S] = ly > 0.35f;
        key[EK_A] = lx < -0.35f;
        key[EK_D] = lx > 0.35f;
        key[EK_CTRL] = DOWN(LEFTSTICK);              /* sprint */
        key[EK_SPACE] = DOWN(A);                     /* jump */
        key[EK_SHIFT] = DOWN(B);                     /* sneak */
        key[EK_E] = DOWN(X) || DOWN(Y);              /* crafting, inventory */
        key[EK_F5] = DOWN(DPAD_UP);                  /* camera */
        key[EK_Q] = DOWN(DPAD_DOWN);                 /* drop */
        key[EK_F] = DOWN(DPAD_LEFT);                 /* (emote on a console) swap hands */
        key[EK_T] = DOWN(DPAD_RIGHT);                /* chat */
        button[1] = rt > 0.5f;                       /* attack, break */
        button[2] = DOWN(RIGHTSTICK);                /* pick the block you're looking at */
        button[3] = lt > 0.5f;                       /* use, place */
        if (pressed[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]) emu_sy += 1;    /* the hotbar */
        if (pressed[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER]) emu_sy -= 1;
    }
#undef DOWN
    dx = (int)emu_fx;
    dy = (int)emu_fy;
    emu_fx -= (float)dx;
    emu_fy -= (float)dy;
    kiosk_wm_pointer_move(dx, dy);
    while (emu_sy >= 1) { emu_click(4); emu_sy -= 1; }
    while (emu_sy <= -1) { emu_click(5); emu_sy += 1; }
    while (emu_sx >= 1) { emu_click(7); emu_sx -= 1; }
    while (emu_sx <= -1) { emu_click(6); emu_sx += 1; }
    emu_send(key, button);
}

/* --------------------------------------------------------- the program XenithOS started
 *
 * One at a time: on screen (run_other, resume_other), or left running behind the home screen
 * by the guide's Return home, until it's chosen again or closed (plat_close_running). */
static struct {
    pid_t pid;
    struct linux_app app;                     /* for its name and Steam game; argv isn't kept */
    char path[128];                           /* the .app it came from ("": Steam from the Store) */
    char log[700];
    uint64_t start, close_at, checked;
    int status, forced, seen_running, idle;
    int manual;                               /* the controller mode was chosen in the guide */
} other;
static int pad_mode = PAD_GAME;

static void other_close(uint64_t now)
{
    if (other.close_at || other.pid <= 0) return;
    ask_to_close(other.pid);
    other.close_at = now;
}

/* A Steam game from XenithOS: once Steam says it has ended, Steam closes too. */
static void other_check_steam(uint64_t now)
{
    int running;
    if (!other.app.steam_appid || other.close_at || now - other.checked < 1000000) return;
    other.checked = now;
    running = linux_steam_game_running(other.app.steam_appid);
    if (running == 1) {
        other.seen_running = 1;
        other.idle = 0;
    } else if (running == 0 && other.seen_running && ++other.idle >= 3) {
        other_close(now);
    }
}

/* Asked to close and still there: everything it started is told to go, then made to. */
static void other_hurry(uint64_t now)
{
    if (!other.close_at || other.pid <= 0) return;
    if (now - other.close_at > 6000000 && other.forced == 0) { kill(-other.pid, SIGTERM); other.forced = 1; }
    if (now - other.close_at > 10000000 && other.forced == 1) { kill(-other.pid, SIGKILL); other.forced = 2; }
}

/* The guide over the program: XenithOS's window in front, the screen's picture behind it. */
static int other_guide(void)
{
    struct plat_guide g;
    int choice;
    memset(&g, 0, sizeof g);
    g.backdrop = kiosk_wm_capture(guide_shot, SCREEN_W, SCREEN_H) ? guide_shot : NULL;
    g.name = other.app.name;
    g.path = other.path[0] ? other.path : NULL;
    g.keeps_running = other.path[0] != 0;
    /* A launcher with a game in it (Minecraft) can't read a controller itself: the pointer, or
     * the keyboard and mouse. Others can be left to read it. */
    g.pad_modes = !emu_ready() ? 0 : other.app.game_title[0] ? PAD_MOUSE | PAD_KEYS : PAD_GAME | PAD_MOUSE | PAD_KEYS;
    g.pad_mode = pad_mode;
    emu_stop();                                /* nothing held down while the panel is up */
    kiosk_wm_hold(1);
    guide_sound_on();
    choice = guide_fn(&g);
    if (choice == GUIDE_RESUME) guide_sound_off();   /* otherwise XenithOS keeps its sound */
    if (g.pad_mode != pad_mode) other.manual = 1;
    pad_mode = g.pad_mode;
    kbd_drain();
    memset(tapped_btn, 0, sizeof tapped_btn);
    memset(tapped_key, 0, sizeof tapped_key);
    if (choice == GUIDE_SWITCH) snprintf(next_launch, sizeof next_launch, "%s", g.switch_to);
    if (choice == GUIDE_RESUME) {
        kiosk_wm_hold(0);
        emu_start(pad_mode);
    }
    return choice;
}

/* While the program is on screen: the guide, the way out, the controller as the pointer.
 * 1 when the guide's Return home left it running, 0 once it has closed. */
static int other_watch(int console)
{
    uint64_t now, guide_since = 0, test_close, test_guide, titled = 0, focused = 0, drawn = 0;
    int esc_used = 0;
    pid_t r;
    kbd_open();
    /* Tests can't use a controller in a virtual display: XENITHOS_TEST_CLOSE=<seconds> acts like
     * holding the Xbox button, XENITHOS_TEST_GUIDE=<seconds> like tapping it. */
    test_close = getenv("XENITHOS_TEST_CLOSE") ? (uint64_t)atoi(getenv("XENITHOS_TEST_CLOSE")) * 1000000 : 0;
    test_guide = getenv("XENITHOS_TEST_GUIDE") ? (uint64_t)atoi(getenv("XENITHOS_TEST_GUIDE")) * 1000000 : 0;
    if (!console) emu_start(pad_mode);
    while (other.pid > 0) {
        SDL_Event e;
        int open = 0, tap = 0, down, combo;
        kiosk_wm_poll();
        while (SDL_PollEvent(&e)) {
            handle_event(&e, type_char);            /* controllers coming and going */
            if (in_kiosk && e.type == SDL_WINDOWEVENT) show_texture();   /* uncovered: our last frame again */
        }
        open_first_pad();
        now = plat_time_us();
        /* The Xbox button: tapped, the guide; held for three seconds, the way out (the only one
         * where there's no guide). */
        down = btn(SDL_CONTROLLER_BUTTON_GUIDE);
        if (down && !guide_since) guide_since = now;
        else if (!down && guide_since) { tap = now - guide_since < 1000000; guide_since = 0; }
        memset(tapped_btn, 0, sizeof tapped_btn);
        memset(tapped_key, 0, sizeof tapped_key);
        combo = kbd_combo();
        if ((guide_since && now - guide_since > 3000000) || combo || (test_close && now - other.start > test_close))
            other_close(now);
        if (!console && plat_has_guide() && !other.close_at) {
            if (tap) open = 1;
            if (kbd_esc_since && now - kbd_esc_since >= 500000 && !esc_used) open = esc_used = 1;
            if (test_guide && now - other.start > test_guide) { open = 1; test_guide = 0; }
        }
        if (!kbd_esc_since) esc_used = 0;
        if (open) {
            int c = other_guide();
            if (c == GUIDE_HOME && other.path[0]) return 1;
            if (c != GUIDE_RESUME) other_close(plat_time_us());
            guide_since = 0;
            continue;
        }
        /* A launcher with a game in it (Prism Launcher and Minecraft): the pointer for the
         * launcher, the keyboard and mouse once the game's window is in front. */
        if (other.app.game_title[0] && !other.manual && in_kiosk && now - titled > 500000) {
            char title[96];
            int want;
            titled = now;
            kiosk_wm_top_title(title, sizeof title);
            want = strncmp(title, other.app.game_title, strlen(other.app.game_title)) ? PAD_MOUSE : PAD_KEYS;
            if (want != pad_mode) {
                emu_stop();
                pad_mode = want;
                emu_start(pad_mode);
                fprintf(stderr, "xenithos: the controller %s\n", want == PAD_KEYS ? "plays it as a keyboard and mouse" : "moves the pointer");
            }
        }
        if (in_kiosk && now - focused > 300000) {    /* the game keeps the keyboard */
            focused = now;
            kiosk_wm_keep_focus();
        }
        emu_step(now);
        other_check_steam(now);
        other_hurry(now);
        r = waitpid(other.pid, &other.status, WNOHANG);
        if (r == other.pid || (r < 0 && errno != EINTR)) break;
        /* Console mode: until the app's window is up (or between its windows, or while it
         * closes from the guide), XenithOS is what's on screen, so its loading screen moves:
         * 20 frames a second, so the game starting up gets the machine. */
        if (in_kiosk && (!kiosk_wm_windows() || other.close_at) && launch_progress && now - drawn >= 50000) {
            drawn = now;
            launch_progress(0, 0);
        } else {
            SDL_Delay(emu_on() ? 8 : 20);
        }
    }
    return 0;
}

/* XenithOS again, after the program closed or with it left running behind. 1 once it has run,
 * 0 (with launch_why) if it couldn't start or stopped straight away. */
static int other_finish(int console, int left_running)
{
    char reason[160];
    int said, status = other.status;
    emu_stop();
    kbd_close();
    if (!left_running && other.pid > 0) kill(-other.pid, SIGTERM);   /* anything it left behind */
    if (console) {
        video_open();
    } else {
        if (in_kiosk) {
            if (left_running) kiosk_wm_hold(1);
            else kiosk_wm_show_self();
        } else {
            SDL_RestoreWindow(win);
        }
        SDL_RaiseWindow(win);
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
    }
    plat_audio_init();
    open_first_pad();
    if (hook_failed) hook_failed();                /* XenithOS's sounds start again */
    if (left_running) return 1;
    if (other.pid <= 0) {
        snprintf(launch_why, sizeof launch_why, "%s didn't start.", other.app.name);
        other.pid = 0;
        return 0;
    }
    other.pid = 0;
    /* Gone again within a few seconds without being asked: say what it printed. */
    said = linux_log_reason(other.log, reason, sizeof reason);
    if (!other.close_at && plat_time_us() - other.start < 5000000 &&
        (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || said == 2)) {
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127 && !said)
            snprintf(launch_why, sizeof launch_why, "%s couldn't be started.", other.app.name);
        else if (strstr(reason, "not installed"))
            snprintf(launch_why, sizeof launch_why, "%s isn't installed any more. Get it again from the Store.", other.app.name);
        else
            snprintf(launch_why, sizeof launch_why, "%s stopped straight away%s%s", other.app.name, reason[0] ? ": " : ".", reason);
        return 0;
    }
    return 1;
}

/* Runs a program with the screen to itself until it closes (or the guide leaves it running). */
static int run_other(struct linux_app *app, const char *path)
{
    char cage[256], *argv[32], **env;
    /* XENITHOS_BARE=1: act as if on the bare console (tests, in a virtual display) */
    int console = !in_kiosk && ((!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) || getenv("XENITHOS_BARE"));
    int n = 0, i;

    if (other.pid > 0) plat_close_running(NULL);   /* one at a time (the home screen asks first) */
    if (console) {
        if (!linux_find_program("cage", cage, sizeof cage)) {
            snprintf(launch_why, sizeof launch_why, "XenithOS needs cage to show %s on this screen. Run the XenithOS install command once more, then try again.", app->name);
            return 0;
        }
        argv[n++] = cage;
        argv[n++] = "-d";                          /* no title bars */
        argv[n++] = "-s";                          /* Ctrl+Alt+F2 and so on still switch consoles */
        argv[n++] = "--";
    }
    for (i = 0; app->argv[i] && n < 31; i++) argv[n++] = app->argv[i];
    argv[n] = NULL;
    memset(&other, 0, sizeof other);
    other.app = *app;
    snprintf(other.path, sizeof other.path, "%s", path ? path : "");
    snprintf(other.log, sizeof other.log, "%s/uda:/xenith/app.log", data_dir);
    remove(other.log);
    if (!(env = other_env())) return 0;
    pad_mode = app->pointer ? PAD_MOUSE : PAD_GAME;

    if (hook_before) hook_before();                /* XenithOS's sounds stop */
    plat_rumble(0, 0);
    audio_close();                                 /* the program gets the sound device */
    if (console) {
        video_close();                             /* and the screen */
    } else {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");    /* Guide still counts */
        if (!in_kiosk) SDL_MinimizeWindow(win);    /* in console mode the app's window covers ours */
        kiosk_wm_hold(0);
    }
    other.pid = linux_spawn(argv, env, other.log, 1);
    free(env);
    other.start = plat_time_us();
    return other_finish(console, other.pid > 0 && other_watch(console));
}

/* Back to the program the guide's Return home left running. */
static int resume_other(void)
{
    if (hook_before) hook_before();
    plat_rumble(0, 0);
    audio_close();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    kiosk_wm_hold(0);                              /* its windows in front again */
    return other_finish(0, other_watch(0));
}

const char *plat_running(void)
{
    uint64_t now;
    int status;
    pid_t r;
    if (other.pid <= 0) return NULL;
    r = waitpid(other.pid, &status, WNOHANG);
    if (r == other.pid || (r < 0 && errno != EINTR)) {     /* it ended behind the home screen */
        kill(-other.pid, SIGTERM);
        other.pid = 0;
        kiosk_wm_show_self();
        return NULL;
    }
    now = plat_time_us();
    other_check_steam(now);
    other_hurry(now);
    return other.path;
}

int plat_close_running(void (*step)(void))
{
    if (!plat_running()) return 1;
    other_close(plat_time_us());
    while (other.pid > 0) {
        int status;
        pid_t r = waitpid(other.pid, &status, WNOHANG);
        if (r == other.pid || (r < 0 && errno != EINTR)) break;
        other_hurry(plat_time_us());
        kiosk_wm_poll();
        if (step) step();
        else SDL_Delay(20);
    }
    if (other.pid > 0) kill(-other.pid, SIGTERM);  /* anything it left behind */
    other.pid = 0;
    kiosk_wm_show_self();
    return 1;
}

int plat_steam_open(const char *url)
{
    struct linux_app a;
    launch_why[0] = 0;
    if (guest) return 0;
    if (!linux_steam_command(url, &a)) {
        snprintf(launch_why, sizeof launch_why, "Steam isn't installed yet. Get it from the Store first.");
        return 0;
    }
    return run_other(&a, NULL);
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
        if (!guest && other.pid > 0) plat_close_running(NULL);   /* nothing left running behind */
        if (hook_before) hook_before();
        /* Only XenithOS itself (the one with the guide) starts again. An app started on its own,
         * from a desktop's menu say, just closes, instead of starting again. */
        if (!guest && self_path[0] && guide_fn) restart_self();
        quit_now(0);
    }
    if (guest) return 0;                                  /* apps don't start other apps */
    launch_why[0] = 0;
    if (strlen(path) > 4 && !strcmp(path + strlen(path) - 4, ".app")) {   /* Flathub, Steam, your own */
        struct linux_app a;
        if (other.pid > 0 && !strcmp(other.path, path) && plat_running()) return resume_other();
        if (!linux_app_read(path, &a, launch_why, sizeof launch_why)) return 0;
        return run_other(&a, path);
    }
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
