#ifndef XENITH_PLATFORM_H
#define XENITH_PLATFORM_H

/* Everything XenithOS needs from the machine.
 *
 * shared/platform_xenon.c implements this on a real Xbox 360 through libxenon.
 * sim/platform_sim.c implements it on a PC, so the same code can be run for
 * screenshots, sound recordings and crash testing before it goes on the console.
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

#define PLAT_AUDIO_RATE  48000
#define PLAT_AUDIO_BLOCK 512                 /* frames per submit */
#define PLAT_AUDIO_RING_BLOCKS 32            /* the sound chip plays from a ring of this many blocks */
void plat_audio_init(void);
int  plat_audio_queued(void);                /* frames queued and not played yet, as the chip reports it */
void plat_audio_submit(const int16_t *stereo, int frames);  /* frames: multiple of PLAT_AUDIO_BLOCK */

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

int  plat_temps(int celsius[4]);             /* CPU, GPU, memory, board. 1 if available */
const char *plat_video_out(void);
const char *plat_name(void);

void plat_launch(const char *path);          /* returns only if loading failed */
void plat_reboot(void);
void plat_power_off(void);

#endif
