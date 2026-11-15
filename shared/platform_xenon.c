/* platform.h on a real Xbox 360, through libxenon. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xenos/xenos.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <time/time.h>
#include <ppc/timebase.h>
#include <ppc/cache.h>
#include <diskio/ata.h>
#include <libfat/fat.h>
#include <elf/elf.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_smc/xenon_smc.h>
#include <xenon_sound/sound.h>

#include "platform.h"
#include "tiled.h"

int bdev_enum(int handle, const char **name);

/* ------------------------------------------------------------------ video */

/* Display registers, the same block libxenon's console driver reads. */
struct ati_info {
    uint32_t unknown1[4];
    uint32_t base;
    uint32_t unknown2[8];
    uint32_t width;
    uint32_t height;
} __attribute__((__packed__));

static uint32_t *fb;
static int fb_bytes, scr_w, scr_h;
static struct tiled_fb tfb;      /* shared/tiled.c: only changed blocks get rewritten */

void plat_init(void)
{
    struct ati_info *ai;
    int off_x = 0, off_y = 0;

    xenos_init(VIDEO_MODE_AUTO);
    xenon_make_it_faster(XENON_SPEED_FULL);

    ai = (struct ati_info *)0xec806100ULL;
    fb = (uint32_t *)(long)(ai->base | 0x80000000);
    fb_bytes = (((ai->width + 31) >> 5) << 5) * (((ai->height + 31) >> 5) << 5) * 4;

    if (xenos_is_overscan()) {
        off_x = ai->width / 28;
        off_y = ai->height / 28;
    }
    scr_w = ((ai->width - off_x * 2) / 8) * 8;
    scr_h = ((ai->height - off_y * 2) / 8) * 8;
    tiled_init(&tfb, fb, ai->width, ai->height, off_x, off_y, scr_w, scr_h);

    /* Wipe whatever the loader left on screen, including the overscan border. */
    memset(fb, 0, fb_bytes);
    memdcbst(fb, fb_bytes);

    usb_init();
    usb_do_poll();
}

void plat_screen_size(int *w, int *h)
{
    *w = scr_w;
    *h = scr_h;
}

static void flush_lines(void *addr, int bytes)
{
    memdcbst(addr, bytes);
}

void plat_present(const uint32_t *px)
{
    tiled_present(&tfb, px, flush_lines);
}

/* ------------------------------------------------------------------- time */

uint64_t plat_time_us(void)
{
    /* The timebase runs at 3.192 GHz / 64 = 49.875 MHz, which is 399/8 ticks per microsecond. */
    return (mftb() * 8) / 399;
}

void plat_sleep_us(int us)
{
    if (us > 0) udelay(us);
}

/* ------------------------------------------------------------------ input */

static int seen_pad;

void plat_poll(struct pad *p)
{
    struct controller_data_s c;
    usb_do_poll();
    memset(&c, 0, sizeof c);
    if (get_controller_data(&c, 0)) seen_pad = 1;   /* 1 means new data arrived */

    memset(p, 0, sizeof *p);
    p->connected = seen_pad;
    p->up = c.up; p->down = c.down; p->left = c.left; p->right = c.right;
    p->a = c.a; p->b = c.b; p->x = c.x; p->y = c.y;
    p->lb = c.lb; p->rb = c.rb; p->start = c.start; p->back = c.back; p->guide = c.logo;
    p->ls = c.s1_z; p->rs = c.s2_z;
    p->lx = c.s1_x; p->ly = c.s1_y; p->rx = c.s2_x; p->ry = c.s2_y;
    p->lt = c.lt; p->rt = c.rt;
}

void plat_rumble(int left, int right)
{
    if (left < 0) left = 0;
    if (left > 255) left = 255;
    if (right < 0) right = 0;
    if (right > 255) right = 255;
    set_controller_rumble(0, (uint8_t)left, (uint8_t)right);
}

/* ------------------------------------------------------------------ audio */

void plat_audio_init(void)
{
    xenon_sound_init();
}

int plat_audio_queued(void)
{
    return xenon_sound_get_unplayed() / 4;
}

void plat_audio_submit(const int16_t *stereo, int frames)
{
    /* The sound chip wants little-endian 16-bit stereo; the CPU is big-endian.
     * Submit whole 2 KB blocks: the driver only starts a block once it is full. */
    static uint8_t le[PLAT_AUDIO_BLOCK * 4];
    while (frames >= PLAT_AUDIO_BLOCK) {
        int i;
        for (i = 0; i < PLAT_AUDIO_BLOCK * 2; i++) {
            uint16_t v = (uint16_t)stereo[i];
            le[i * 2] = v & 0xff;
            le[i * 2 + 1] = v >> 8;
        }
        xenon_sound_submit(le, sizeof le);
        stereo += PLAT_AUDIO_BLOCK * 2;
        frames -= PLAT_AUDIO_BLOCK;
    }
}

/* ----------------------------------------------------------- spare thread */

/* Thread 2 is the first thread of the second core; XenithOS itself runs on thread 0.
 * libxenon's secondary threads poll for a task, run it on the stack they're given, and go
 * back to polling when it returns. The ELF loader waits for every thread to be free, so the
 * task must return (plat_thread_stop) before another program is launched. */
#define SPARE_THREAD 2
static unsigned char thread_stack[64 * 1024] __attribute__((aligned(128)));
static void (*volatile thread_step)(void);
static volatile int thread_run, thread_alive, thread_cancel;

static void thread_task(void)
{
    if (thread_cancel) return;
    thread_alive = 1;
    while (thread_run) {
        thread_step();
        udelay(1000);
    }
    thread_alive = 0;
}

int plat_thread_start(void (*step)(void))
{
    uint64_t start;
    if (thread_run) return 1;
    thread_step = step;
    thread_alive = 0;
    thread_cancel = 0;
    thread_run = 1;
    __sync_synchronize();
    if (xenon_run_thread_task(SPARE_THREAD, thread_stack + sizeof thread_stack - 0x100, (void *)thread_task) != 0) {
        thread_run = 0;
        return 0;
    }
    start = mftb();
    while (!thread_alive) {
        if (tb_diff_msec(mftb(), start) > 100) {   /* never picked up: make sure it never runs */
            thread_cancel = 1;
            thread_run = 0;
            __sync_synchronize();
            return 0;
        }
    }
    return 1;
}

void plat_thread_stop(void)
{
    uint64_t start = mftb();
    if (!thread_run) return;
    thread_run = 0;
    __sync_synchronize();
    while (xenon_is_thread_task_running(SPARE_THREAD) && tb_diff_msec(mftb(), start) < 500)
        ;
}

static void (*hook_before)(void), (*hook_failed)(void);

void plat_set_launch_hooks(void (*before)(void), void (*failed)(void))
{
    hook_before = before;
    hook_failed = failed;
}

/* ---------------------------------------------------------------- storage */

void plat_storage_init(void)
{
    xenon_ata_init();
    xenon_atapi_init();
    fatInitDefault();
}

int plat_bdev_next(int handle, const char **name)
{
    return bdev_enum(handle, name);
}

/* ----------------------------------------------------------------- system */

int plat_temps(int c[4])
{
    uint16_t raw[4];
    int i;
    xenon_smc_query_sensors(raw);
    for (i = 0; i < 4; i++) c[i] = raw[i] >> 8;   /* 1/256 degree C */
    return 1;
}

const char *plat_video_out(void)
{
    int pack = xenon_smc_read_avpack();
    if (xenon_avpack_is_hdmi(pack)) return "HDMI";
    switch (xenon_avpack_type(pack)) {
    case AV_VGA:          return "VGA";
    case AV_COMPOSITE:    return "Composite";
    case AV_COMPONENT_HD: return "Component HD";
    case AV_COMPONENT_TV: return "Component SD";
    case AV_SVIDEO:       return "S-Video";
    case AV_SCART_RGB:    return "SCART RGB";
    case AV_AUDIO_ONLY:   return "Audio only";
    default:              return "Unknown";
    }
}

const char *plat_name(void)
{
    return "Xbox 360";
}

void plat_launch(const char *path)
{
    if (hook_before) hook_before();
    plat_rumble(0, 0);
    elf_runFromDisk((char *)path);
    if (hook_failed) hook_failed();
}

void plat_reboot(void)
{
    if (hook_before) hook_before();
    xenon_smc_power_reboot();
}

void plat_power_off(void)
{
    if (hook_before) hook_before();
    xenon_smc_power_shutdown();
}
