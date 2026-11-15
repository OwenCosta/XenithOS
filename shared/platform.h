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
/* fn(done, total) is called as plat_launch reads the program (Xbox), for a progress bar.
 * NULL turns it off. */
void plat_set_launch_progress(void (*fn)(int done, int total));
void plat_reboot(void);
void plat_power_off(void);
int  plat_can_exit_to_loader(void);          /* Xbox: back to XeLL by returning from main */

/* PCs and Pis have two modes: console mode (XenithOS full screen) and desktop mode (a normal
 * Linux desktop). plat_has_desktop() says whether one is installed; plat_switch_to_desktop()
 * returns only if switching failed. */
int  plat_has_desktop(void);
void plat_switch_to_desktop(void);

#endif
