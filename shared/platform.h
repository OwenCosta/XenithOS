#ifndef XENITH_PLATFORM_H
#define XENITH_PLATFORM_H

/* Everything XenithOS needs from the machine.
 *
 * shared/platform_xenon.c implements this on a real Xbox 360 through libxenon.
 * shared/platform_sdl.c implements it on PCs and Raspberry Pis (Linux, through SDL2).
 * sim/platform_sim.c implements it on a PC for testing, so the same code can be run for
 * screenshots, sound recordings and crash testing before it goes on a real machine.
 */
#include <stdint.h>

struct pad {
    int connected;
    int up, down, left, right;
    int a, b, x, y, lb, rb, start, back, guide, ls, rs;
    int lx, ly, rx, ry;       /* sticks, -32768..32767, up and right are positive */
    int lt, rt;               /* triggers, 0..255 */
};

void plat_init(void);                        /* video, USB, full CPU speed */
void plat_screen_size(int *w, int *h);       /* usable area inside the TV's overscan */
void plat_present(const uint32_t *pixels);   /* w*h pixels, 0x00RRGGBB, row by row */

uint64_t plat_time_us(void);                 /* monotonic */
void plat_sleep_us(int us);

void plat_poll(struct pad *p);
void plat_rumble(int left, int right);       /* 0..255 each */

/* Typing on a real keyboard (PCs). While it's on, letter keys type instead of acting as
 * buttons. plat_text_take() hands over what was typed since the last call: '\b' for
 * Backspace, '\n' for Enter. Consoles have no keyboard: it always returns 0 there. */
void plat_text_input(int on);
int  plat_text_take(char *out, int n);

#define PLAT_AUDIO_RATE  48000
#define PLAT_AUDIO_BLOCK 512                 /* frames per submit */
#define PLAT_AUDIO_RING_BLOCKS 32            /* the sound chip plays from a ring of this many blocks */
void plat_audio_init(void);
int  plat_audio_queued(void);                /* frames queued and not played yet, as the chip reports it */
void plat_audio_submit(const int16_t *stereo, int frames);  /* frames: multiple of PLAT_AUDIO_BLOCK */
/* Stop the sound hardware outright. Called before another program takes over: on the Xbox
 * the next program is loaded over this one's memory, and a sound chip still running would
 * play that as noise. Safe to call before plat_audio_init. */
void plat_audio_stop(void);

/* A spare hardware thread. step() is called about once a millisecond until plat_thread_stop().
 * Returns 0 if there's no spare thread (the simulator, by default); the caller then does the
 * work itself on the main thread. */
int  plat_thread_start(void (*step)(void));
void plat_thread_stop(void);

/* before() runs right before another program is launched or the console restarts or turns off.
 * failed() runs if a launch didn't happen after all. */
void plat_set_launch_hooks(void (*before)(void), void (*failed)(void));

void plat_storage_init(void);                /* USB / hard drive file systems */
int  plat_bdev_next(int handle, const char **name);         /* -1 to start, -1 when done */

int  plat_temps(int celsius[4]);             /* CPU, GPU, memory, board; -1 = no sensor. 1 if any */
const char *plat_video_out(void);
const char *plat_name(void);                 /* "Xbox 360", "Raspberry Pi 5", "PC" ... */
const char *plat_cpu_name(void);

/* What kind of machine this is, for the few things that differ. */
enum { PLAT_XBOX, PLAT_PC };
int  plat_kind(void);

/* Start another program (an app, or XENITH_SHELL_PATH for XenithOS itself).
 * Xbox: the program replaces this one, so this only returns (0) if loading failed. It gets
 * what handoff_set() was given (handoff.h), plus the screen mode, so it can keep the
 * picture as it is instead of setting the TV up again.
 * PC: the app runs as its own process and this returns 1 once it quits, or 0 if it couldn't
 * start. Asking for XenithOS from inside an app just quits the app. */
int  plat_launch(const char *path);

/* PCs and Pis: build a downloaded XenithOS source (.tar.gz) and put it in place of the
 * installed version, as the user (the Store's "XenithOS update"). step() is called many
 * times a second with what's happening and the seconds so far, for the screen. Returns 1
 * when the new version is in place (plat_launch(XENITH_SHELL_PATH) restarts into it), or 0
 * with why[] set. Not used on the Xbox, where the update is xenon.elf itself. */
int  plat_install_update(const char *tarball, void (*step)(const char *what, int seconds), char *why, int why_len);

/* PCs and Pis: apps from Flathub (through Flatpak) and games from Steam. The Xbox has none of
 * this, and says so.
 *   plat_arch()              the processor as Flathub names it: "x86_64", "aarch64" ("" on the Xbox)
 *   plat_flatpak_ready()     1 when Flatpak is installed here
 *   plat_flatpak_installed() 1 when that app (an id like "tv.kodi.Kodi") is installed
 *   plat_flatpak()           verb "install", "update", "uninstall", or "purge" (uninstall and
 *                            delete the app's data: settings, saves, Steam's games). step(percent or -1,
 *                            seconds) is called many times a second while it works, and can
 *                            return 0 to cancel. 1 when done, -1 when cancelled, 0 with why[] set.
 *   plat_steam_ready()       1 when the Steam app is installed
 *   plat_steam_open(url)     Steam full screen at a steam:// address (a game's page, say), until
 *                            it's closed. 0 if it couldn't start (plat_launch_error() says why)
 *   plat_sync_apps()         puts the games installed in Steam into uda:/apps as
 *                            "steam-<appid>.app", with Steam's picture of each
 * An installed app is "uda:/apps/<id>.app" (name=, then flatpak=<id> and args=, steam=<appid>
 * or exec=<program and arguments>), started with plat_launch() like any other app. It gets
 * the whole screen; holding Guide for three seconds closes it and comes back to XenithOS. */
const char *plat_arch(void);
int  plat_flatpak_ready(void);
int  plat_flatpak_installed(const char *ref);
int  plat_flatpak(const char *verb, const char *ref, int (*step)(int percent, int seconds), char *why, int why_len);
int  plat_steam_ready(void);
int  plat_steam_open(const char *url);
void plat_sync_apps(void);

/* PCs and Pis: XenithOS's own apps (VOLT, DOOM, HostMC...) are built along with XenithOS, in
 * /usr/lib/xenithos, and are on the home screen once they're installed from the Store. The
 * Xbox gets them as downloads instead, so it has none of these: 0.
 *   plat_local_app(id)          1 when that app's program is on this machine
 *   plat_local_app_set(id, on)  puts it on the home screen (uda:/apps/<id>.elf32 with its name
 *                               and picture) or takes it off; 1 when done. On XenithOS Desktop
 *                               its own tool does it, so Settings > Console apps agrees.
 *   plat_local_app_file(id, ext, out, n)  where this machine keeps that app's "txt" (name=,
 *                               version=) or "ximg" (its picture), installed or not; 0 if nowhere */
int  plat_local_app(const char *id);
int  plat_local_app_set(const char *id, int on);
int  plat_local_app_file(const char *id, const char *ext, char *out, int n);
/* Why the last plat_launch() or plat_steam_open() returned 0, when the platform knows. */
const char *plat_launch_error(void);
/* fn(done, total) is called as plat_launch reads the program (Xbox), for a progress bar.
 * NULL turns it off. */
void plat_set_launch_progress(void (*fn)(int done, int total));
void plat_reboot(void);
void plat_power_off(void);
int  plat_can_exit_to_loader(void);          /* Xbox: back to XeLL by returning from main */

/* The guide (PCs): pressing the Xbox button, or holding Esc for half a second, while an app or
 * game is on screen brings up XenithOS's side panel over it. The platform takes a picture of
 * the screen, puts XenithOS's window in front and calls fn, which runs the panel until a
 * choice is made and returns it:
 *   GUIDE_RESUME   back to the game
 *   GUIDE_HOME     XenithOS's home screen; with keeps_running the game is left running
 *                  (plat_running() names it, and plat_launch() of it goes back to it),
 *                  otherwise it's closed
 *   GUIDE_SWITCH   the game is closed and switch_to started instead (plat_next_launch())
 *   GUIDE_CLOSE    the game is closed
 * Before returning GUIDE_SWITCH or GUIDE_CLOSE, fn leaves a "Closing" screen up: the
 * platform keeps calling the launch progress hook while the game closes. pad_modes says
 * which controller modes the game can switch between (0: no choice), pad_mode which is on:
 *   PAD_GAME   the game reads the controller itself
 *   PAD_MOUSE  the controller moves the pointer (launchers and menus)
 *   PAD_KEYS   the controller plays it like a keyboard and mouse (Minecraft) */
enum { GUIDE_RESUME, GUIDE_HOME, GUIDE_SWITCH, GUIDE_CLOSE };
enum { PAD_GAME = 1, PAD_MOUSE = 2, PAD_KEYS = 4 };
struct plat_guide {
    const uint32_t *backdrop;            /* the screen when it was opened (w*h), or NULL */
    const char *name, *path;             /* the game or app */
    int keeps_running;
    int pad_modes, pad_mode;
    char switch_to[128];
};
void plat_set_guide(int (*fn)(struct plat_guide *g));
int  plat_has_guide(void);               /* 1 where other programs (Flathub, Steam) get the guide */
const char *plat_running(void);          /* the program Return home left running, or NULL */
int  plat_close_running(void (*step)(void));   /* asks it to close and waits (step: a frame) */
const char *plat_next_launch(void);      /* what Switch game chose, once; NULL when nothing */

/* PCs and Pis have two modes: console mode (XenithOS full screen) and desktop mode (a normal
 * Linux desktop). plat_has_desktop() says whether one is installed; plat_switch_to_desktop()
 * returns only if switching failed. */
int  plat_has_desktop(void);
void plat_switch_to_desktop(void);

#endif
