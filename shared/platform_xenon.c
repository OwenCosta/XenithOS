/* platform.h on a real Xbox 360, through libxenon. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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
#include <pci/io.h>

#include "handoff.h"
#include "platform.h"
#include "tiled.h"

int bdev_enum(int handle, const char **name);
extern int xenos_is_hdmi;          /* libxenon: xenon_sound_init turns on HDMI sound when set */

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
static int fb_bytes, fb_w, fb_h, overscan, scr_w, scr_h;
static struct tiled_fb tfb;      /* shared/tiled.c: only changed blocks get rewritten */

/* Started by another XenithOS program that left the screen running in a mode we can use:
 * keep it. Setting a video mode makes the TV drop the picture for a second or more, which is
 * what made every app start (and every return to XenithOS) go black. Only when the handoff
 * says so and the display registers agree; anything else gets the full set-up. */
static int keep_screen(void)
{
    struct ati_info *ai = (struct ati_info *)0xec806100ULL;
    const char *v = handoff_get("video");
    int w, h, over, hdmi;
    if (!v || sscanf(v, "%dx%d:%d:%d", &w, &h, &over, &hdmi) != 4) return 0;
    if (w < 320 || h < 240 || w > 1920 || h > 1080) return 0;
    if ((int)ai->width != w || (int)ai->height != h) return 0;
    if (ai->base != 0x1e000000) return 0;          /* where libxenon puts the framebuffer */
    overscan = over != 0;
    xenos_is_hdmi = hdmi != 0;                     /* xenos_init would have found this out */
    return 1;
}

void plat_init(void)
{
    struct ati_info *ai;
    int off_x = 0, off_y = 0, kept;

    /* If the program before this one left the sound chip running, it's now playing whatever
     * this program was loaded over. Stop it before anything else. */
    plat_audio_stop();
    kept = keep_screen();
    if (!kept) xenos_init(VIDEO_MODE_AUTO);
    xenon_make_it_faster(XENON_SPEED_FULL);

    ai = (struct ati_info *)0xec806100ULL;
    fb = (uint32_t *)(long)(ai->base | 0x80000000);
    fb_w = ai->width;
    fb_h = ai->height;
    fb_bytes = (((ai->width + 31) >> 5) << 5) * (((ai->height + 31) >> 5) << 5) * 4;

    /* xenos_is_overscan() only works after xenos_init (it exits otherwise). */
    if (!kept) overscan = xenos_is_overscan();
    if (overscan) {
        off_x = ai->width / 28;
        off_y = ai->height / 28;
    }
    scr_w = ((ai->width - off_x * 2) / 8) * 8;
    scr_h = ((ai->height - off_y * 2) / 8) * 8;
    tiled_init(&tfb, fb, ai->width, ai->height, off_x, off_y, scr_w, scr_h);

    /* A new mode: wipe whatever the loader left on screen, including the overscan border.
     * A kept one shows the last program's final frame until this one draws (tiled.c rewrites
     * the whole screen on the first present). */
    if (!kept) {
        memset(fb, 0, fb_bytes);
        memdcbst(fb, fb_bytes);
    }

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
    static uint8_t silence[PLAT_AUDIO_BLOCK * 4];
    int rptr, i;
    xenon_sound_init();
    /* libxenon starts writing at the first of its 32 blocks, but the chip goes on reading from
     * wherever the last program left it (after a cold boot that's the first block too). When
     * the two start far apart, the chip plays up to a whole ring of old sound before the new,
     * and the mixer, which judges how far ahead it is from those two positions, keeps them
     * apart: the repeating, popping sound after coming back from a game. So write silence up
     * to the chip's position first; one block of it after a cold boot. */
    rptr = read32(0xea001600 + 4) & 0x1f;
    for (i = 0; i <= rptr; i++) xenon_sound_submit(silence, sizeof silence);
}

int plat_audio_queued(void)
{
    return xenon_sound_get_unplayed() / 4;
}

void plat_audio_stop(void)
{
    /* The sound DMA's control register (xenon_sound_init starts from the same write).
     * Stopped, the chip can't replay its last block over and over. */
    write32(0xea001600 + 8, 0);
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

/* Only the USB ports. libxenon's hard drive set-up always waits a full second and the DVD
 * set-up another 0.35 s, on every start of every program; XenithOS reads neither (the hard
 * drive is FATX, which libfat can't read). The stick can take a moment to show up after
 * usb_init, so keep trying for up to 2 seconds. */
extern DISC_INTERFACE usb2mass_ops_0, usb2mass_ops_1, usb2mass_ops_2;

void plat_storage_init(void)
{
    static const char *const names[3] = {"uda", "udb", "udc"};
    DISC_INTERFACE *const io[3] = {&usb2mass_ops_0, &usb2mass_ops_1, &usb2mass_ops_2};
    int mounted[3] = {0, 0, 0}, any = 0, tries, i;
    for (tries = 0; tries < 40 && !any; tries++) {
        for (i = 0; i < 3; i++)
            if (!mounted[i] && fatMount(names[i], io[i], 0, 32, 64)) mounted[i] = any = 1;
        if (!any) {
            usb_do_poll();
            mdelay(50);
        }
    }
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

const char *plat_cpu_name(void)
{
    return "Xenon, 3 cores at 3.2 GHz";
}

int plat_kind(void)
{
    return PLAT_XBOX;
}

static void (*launch_progress)(int done, int total);

void plat_set_launch_progress(void (*fn)(int done, int total))
{
    launch_progress = fn;
}

/* libxenon's ELF loader hands this area to the next program as its arguments. */
#define ELF_ARGV_AREA ((void *)0x93FC0000)

int plat_launch(const char *path)
{
    char video[48], *argv[HANDOFF_MAX + 2];
    struct stat s;
    unsigned char *buf;
    int f, size, got = 0, argc;

    /* Read the whole program first, while the screen can still show progress and the sound
     * is still playing (elf_runFromDisk would do the same read with the screen frozen). */
    if (stat(path, &s) != 0 || s.st_size <= 0) return 0;
    size = (int)s.st_size;
    f = open(path, O_RDONLY);
    if (f < 0) return 0;
    buf = malloc(size);
    if (!buf) { close(f); return 0; }
    while (got < size) {
        int want = size - got > 256 * 1024 ? 256 * 1024 : size - got;
        int n = read(f, buf + got, want);
        if (n <= 0) break;
        got += n;
        if (launch_progress) launch_progress(got, size);
    }
    close(f);
    if (got != size) { free(buf); return 0; }

    if (hook_before) hook_before();
    plat_audio_stop();
    plat_rumble(0, 0);

    snprintf(video, sizeof video, "%dx%d:%d:%d", fb_w, fb_h, overscan, xenos_is_hdmi ? 1 : 0);
    handoff_set("video", video);
    argc = handoff_argv(path, argv, HANDOFF_MAX + 2);
    elf_setArgcArgv(argc, argv);
    /* elf_setArgcArgv's own cache flush is given a pointer as its length and does nothing,
     * and the loader copies these in real mode, from memory: write them out ourselves. */
    memdcbst(ELF_ARGV_AREA, 4096);

    elf_runFromMemory(buf, size);    /* only comes back if the file isn't a program */
    free(buf);
    if (hook_failed) hook_failed();
    return 0;
}

void plat_text_input(int on) { (void)on; }
int plat_text_take(char *out, int n) { (void)out; (void)n; return 0; }

void plat_reboot(void)
{
    if (hook_before) hook_before();
    plat_audio_stop();
    xenon_smc_power_reboot();
}

void plat_power_off(void)
{
    if (hook_before) hook_before();
    plat_audio_stop();
    xenon_smc_power_shutdown();
}

int plat_can_exit_to_loader(void) { return 1; }
int plat_has_desktop(void) { return 0; }
void plat_switch_to_desktop(void) { }
