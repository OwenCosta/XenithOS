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
 * Testing: XENITHOS_SHOT=file.png and XENITHOS_SHOT_FRAME=n save frame n and quit, which
 * works with SDL_VIDEODRIVER=offscreen on a machine with no screen.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <SDL.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void (*hook_before)(void), (*hook_failed)(void);

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
    signal(SIGPIPE, SIG_IGN);
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
    if (on) SDL_StartTextInput();
    else SDL_StopTextInput();
}

int plat_text_take(char *out, int n)
{
    int k = ntyped < n ? ntyped : n;
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

static int btn(SDL_GameControllerButton b)
{
    return pad_dev ? SDL_GameControllerGetButton(pad_dev, b) : 0;
}

void plat_poll(struct pad *p)
{
    SDL_Event e;
    const Uint8 *k;
    int ly, ry;

    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            if (hook_before) hook_before();
            quit_now(0);
            break;
        case SDL_CONTROLLERDEVICEADDED:
            open_first_pad();
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (pad_dev && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_dev)) == e.cdevice.which) {
                SDL_GameControllerClose(pad_dev);
                pad_dev = NULL;
                open_first_pad();
            }
            break;
        case SDL_TEXTINPUT:
            if (typing) {
                const char *t = e.text.text;
                for (; *t; t++)
                    if ((unsigned char)*t >= 32 && (unsigned char)*t < 127) type_char(*t);
            }
            break;
        case SDL_KEYDOWN:
            if (typing && e.key.keysym.sym == SDLK_BACKSPACE) type_char('\b');
            if (typing && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)) type_char('\n');
            break;
        }
    }
    open_first_pad();

    k = SDL_GetKeyboardState(NULL);
    memset(p, 0, sizeof *p);
    p->connected = 1;
    p->up = k[SDL_SCANCODE_UP] || btn(SDL_CONTROLLER_BUTTON_DPAD_UP);
    p->down = k[SDL_SCANCODE_DOWN] || btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    p->left = k[SDL_SCANCODE_LEFT] || btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    p->right = k[SDL_SCANCODE_RIGHT] || btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    /* While typing, Enter, Space and the letter keys belong to the text. */
    p->a = btn(SDL_CONTROLLER_BUTTON_A) || (!typing && (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER] || k[SDL_SCANCODE_SPACE]));
    p->b = btn(SDL_CONTROLLER_BUTTON_B) || k[SDL_SCANCODE_ESCAPE];
    p->x = btn(SDL_CONTROLLER_BUTTON_X) || (!typing && k[SDL_SCANCODE_X]);
    p->y = btn(SDL_CONTROLLER_BUTTON_Y) || (!typing && k[SDL_SCANCODE_Y]);
    p->lb = btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || (!typing && k[SDL_SCANCODE_Q]);
    p->rb = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) || (!typing && k[SDL_SCANCODE_E]);
    p->start = btn(SDL_CONTROLLER_BUTTON_START) || (!typing && k[SDL_SCANCODE_F1]);
    p->back = btn(SDL_CONTROLLER_BUTTON_BACK) || (!typing && k[SDL_SCANCODE_TAB]);
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
        if (k[SDL_SCANCODE_W]) p->ly = 32767;
        if (k[SDL_SCANCODE_S]) p->ly = -32767;
        if (k[SDL_SCANCODE_A]) p->lx = -32767;
        if (k[SDL_SCANCODE_D]) p->lx = 32767;
    }
    p->lt = axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7;
    p->rt = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7;
    if (k[SDL_SCANCODE_PAGEUP]) p->lt = 255;
    if (k[SDL_SCANCODE_PAGEDOWN] || k[SDL_SCANCODE_LCTRL]) p->rt = 255;
}

void plat_rumble(int left, int right)
{
    if (!pad_dev) return;
    if (left < 0) left = 0;
    if (left > 255) left = 255;
    if (right < 0) right = 0;
    if (right > 255) right = 255;
    SDL_GameControllerRumble(pad_dev, (Uint16)(left * 257), (Uint16)(right * 257), left || right ? 10000 : 0);
}

/* ------------------------------------------------------------------ audio */

void plat_audio_init(void)
{
    SDL_AudioSpec want, have;
    if (audio_dev) return;
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
    return audio_dev ? (int)(SDL_GetQueuedAudioSize(audio_dev) / 4) : 0;
}

void plat_audio_submit(const int16_t *stereo, int frames)
{
    if (audio_dev && frames > 0) SDL_QueueAudio(audio_dev, stereo, (Uint32)frames * 4);
}

void plat_audio_stop(void)
{
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

/* Apps start as their own processes here, straight from disk: nothing to show progress for. */
void plat_set_launch_progress(void (*fn)(int done, int total))
{
    (void)fn;
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
    const char *drv = SDL_GetCurrentVideoDriver();
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

/* Apps are programs in uda:/apps. XenithOS hands the screen and sound over while one runs
 * (on the bare console only one program can drive the screen) and takes them back after. */
int plat_launch(const char *path)
{
    pid_t pid;
    int status = 0, ok;
    const char *slash = strrchr(path, '/');
    if (slash && !strcmp(slash + 1, "xenon.elf")) {       /* "back to XenithOS" from an app */
        if (hook_before) hook_before();
        quit_now(0);
    }
    if (access(path, X_OK) != 0) chmod(path, 0755);
    if (access(path, X_OK) != 0) return 0;

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
    open_first_pad();
    if (hook_failed) hook_failed();               /* starts the sound again */
    return ok;
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
