/* platform.h on a real Xbox 360, through libxenon. */
#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <console/console.h>
#include <xenon_uart/xenon_uart.h>
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

#include "crash.h"
#include "handoff.h"
#include "platform.h"
#include "startlog.h"
#include "theme.h"
#include "tiled.h"

int bdev_enum(int handle, const char **name);
static volatile int crash_pending;               /* crashes, at the end of this file */
static void crash_finish(void);
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
    if (crash_pending) crash_finish();           /* the sound mixer's thread crashed */
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
    if (crash_pending) crash_finish();
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

int plat_install_update(const char *tarball, void (*step)(const char *what, int seconds), char *why, int why_len)
{
    (void)tarball;
    (void)step;
    snprintf(why, why_len, "The Xbox updates by replacing xenon.elf.");
    return 0;
}

/* Flathub apps and Steam are Linux programs: there are none on the Xbox. */
const char *plat_arch(void) { return ""; }
int plat_flatpak_ready(void) { return 0; }
int plat_flatpak_installed(const char *ref) { (void)ref; return 0; }
int plat_flatpak(const char *verb, const char *ref, int (*step)(int percent, int seconds), char *why, int why_len)
{
    (void)verb;
    (void)ref;
    (void)step;
    snprintf(why, why_len, "Apps from Flathub are for PCs and Raspberry Pis.");
    return 0;
}
int plat_steam_ready(void) { return 0; }
int plat_steam_open(const char *url) { (void)url; return 0; }
void plat_sync_apps(void) { }

/* XenithOS's apps come to the Xbox as downloads from the Store: none are built here. */
int plat_local_app(const char *id) { (void)id; return 0; }
int plat_local_app_set(const char *id, int on) { (void)id; (void)on; return 0; }
int plat_local_app_file(const char *id, const char *ext, char *out, int n)
{
    (void)id;
    (void)ext;
    (void)out;
    (void)n;
    return 0;
}
const char *plat_launch_error(void) { return ""; }

/* The guide is for PCs: on the Xbox an app replaces XenithOS, and leaves through its own menu. */
void plat_set_guide(int (*fn)(struct plat_guide *g)) { (void)fn; }
int plat_has_guide(void) { return 0; }
const char *plat_running(void) { return NULL; }
int plat_close_running(void (*step)(void)) { (void)step; return 1; }
const char *plat_next_launch(void) { return NULL; }

void plat_text_input(int on) { (void)on; }
int plat_text_take(char *out, int n) { (void)out; (void)n; return 0; }

/* No desktop on a console. */
int plat_desktop(void) { return 0; }
void plat_mouse(struct plat_mouse *m) { memset(m, 0, sizeof *m); m->x = m->y = -1; }
void plat_mouse_grab(int on) { (void)on; }
int plat_key_event(int *key, int *down) { (void)key; (void)down; return 0; }

/* No Bluetooth for controllers on the 360: its own wireless controllers aren't Bluetooth. */
int plat_bt_open(void) { return 0; }
void plat_bt_poll(struct plat_bt_status *s)
{
    memset(s, 0, sizeof *s);
    s->state = BT_FAILED;
    snprintf(s->why, sizeof s->why, "The Xbox 360 has no Bluetooth.");
}
int plat_bt_devices(struct plat_bt_device *out, int max) { (void)out; (void)max; return 0; }
void plat_bt_pair(const char *addr) { (void)addr; }
void plat_bt_connect(const char *addr) { (void)addr; }
void plat_bt_disconnect(const char *addr) { (void)addr; }
void plat_bt_forget(const char *addr) { (void)addr; }
void plat_bt_close(void) { }

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

/* ---------------------------------------------------------------- crashes
 *
 * libxenon sends every exception it can't deal with (a bad pointer, an illegal instruction, a
 * trap) to crashdump(): its own shows the registers on a blue screen and waits for a key on the
 * serial port, which nobody playing has. This crashdump() takes its place (libxenon keeps its own
 * in a file of its own, c_except.o, which the linker then leaves out). It:
 *   saves a crash file (crash.h) for XenithOS to send next time it starts: the program, the
 *   version, the exception, the registers, the stack as addresses and the start-up steps it got
 *   through. The addresses are the program's own: xenon-addr2line -e xenith.elf (or the app's
 *   .elf, unstripped, from the same build) turns them into functions and lines;
 *   puts the same on the serial port, for anyone with a cable;
 *   says on the screen what happened: A restarts the console, B turns it off, X goes back to XeLL.
 * It runs on the thread that crashed, with its registers in context (libxenon's layout: r0 to r31,
 * then lr, ctr, cr, xer, srr0 (where it stopped), srr1, dar (the address it used), pir (the
 * thread)). The sound mixer has a thread of its own: if that's the one, the main thread is still
 * running and may be in the middle of using the USB stick, so the crash waits in memory until the
 * main thread's next frame or controller read (crash_pending). A crash in here (a broken USB
 * stack, say) comes back in here once more, and then only the screen is shown. */
static char crash_text[4096], crash_program[40];
static int crash_len;
static volatile int crash_depth, crash_saving;

static void crash_add(const char *fmt, ...)
{
    va_list ap;
    int n;
    if (crash_len >= (int)sizeof crash_text - 1) return;
    va_start(ap, fmt);
    n = vsnprintf(crash_text + crash_len, sizeof crash_text - crash_len, fmt, ap);
    va_end(ap);
    if (n > 0) crash_len += n < (int)sizeof crash_text - crash_len ? n : (int)sizeof crash_text - 1 - crash_len;
}

static const char *exception_name(uint32_t vector)
{
    switch (vector) {
    case 0: return "segfault";                        /* a page fault: a bad pointer */
    case 0x200: return "machine check";
    case 0x300: return "data storage";
    case 0x380: return "data segment";
    case 0x400: return "instruction storage";
    case 0x480: return "instruction segment";
    case 0x600: return "alignment";
    case 0x700: return "program";                     /* an illegal instruction or a trap */
    case 0x800: return "floating point";
    case 0x900: return "decrementer";
    case 0xC00: return "system call";
    case 0xD00: return "trace";
    case 0xF00: return "performance monitor";
    case 0x1300: return "instruction breakpoint";
    case 0x1700: return "thermal";
    }
    return "exception";
}

/* Code and stack live in the RAM libxenon maps at 0x80000000 (app.lds); nothing else is read. */
static int crash_addr_ok(uint32_t a)
{
    return a >= 0x80000000u && a < 0xa0000000u && !(a & 3);
}

static void crash_build(uint32_t vector, const uint64_t *ctx)
{
    const char *self = handoff_self(), *b = strrchr(self, '/'), *steps[16];
    uint64_t at[16], now = plat_time_us();
    uint32_t sp = (uint32_t)ctx[1], lr = (uint32_t)ctx[32];
    int i, n, nsteps;
    char *p;

    /* "uda:/apps/volt.elf32" is volt; XeLL starting xenon.elf gives no path: XenithOS itself. */
    if (!b) b = strchr(self, ':');
    snprintf(crash_program, sizeof crash_program, "%s", b ? b + 1 : self);
    if ((p = strchr(crash_program, '.'))) *p = 0;
    for (p = crash_program; *p; p++) *p = isalnum((unsigned char)*p) ? (char)tolower((unsigned char)*p) : '-';
    if (!crash_program[0] || !strcmp(crash_program, "xenon")) snprintf(crash_program, sizeof crash_program, "xenithos");

    crash_len = 0;
    crash_add("# XenithOS crash report\nprogram=%s\nversion=%s\nmachine=xenon\nexception=%s\nvector=0x%x\n",
              crash_program, XENITH_VERSION, exception_name(vector), (unsigned)vector);
    crash_add("pc=0x%08x\nlr=0x%08x\naddress=0x%08x\nthread=%d\nmsr=0x%016llx\n", (unsigned)ctx[36], (unsigned)lr,
              (unsigned)ctx[38], (int)ctx[39], (unsigned long long)ctx[37]);
    nsteps = startlog_steps(steps, at, 16);
    if (nsteps) {
        uint64_t up = now > at[0] ? now - at[0] : 0;
        crash_add("seconds=%u.%02u\n", (unsigned)(up / 1000000), (unsigned)(up % 1000000 / 10000));
    }
    for (i = 0; i < nsteps; i++) {
        uint64_t t = at[i] - at[0];
        crash_add("step=%s %u.%02u\n", steps[i], (unsigned)(t / 1000000), (unsigned)(t % 1000000 / 10000));
    }
    for (i = 0; i < 32; i++) crash_add("r%d=0x%016llx\n", i, (unsigned long long)ctx[i]);
    crash_add("ctr=0x%016llx\ncr=0x%08x\nxer=0x%016llx\n", (unsigned long long)ctx[33], (unsigned)ctx[34],
              (unsigned long long)ctx[35]);
    /* The stack: where it stopped, lr (the caller, if the function hadn't saved it yet), then the
     * back chain. 32-bit PowerPC frames start with a pointer to the caller's frame, and the word
     * after that holds the return address of the call the caller made. */
    crash_add("frame=0x%08x\n", (unsigned)ctx[36]);
    if (crash_addr_ok(lr)) crash_add("frame=0x%08x\n", (unsigned)lr);
    for (n = 0; n < 24 && crash_addr_ok(sp); n++) {
        uint32_t up = *(volatile uint32_t *)(uintptr_t)sp, ret;
        if (!crash_addr_ok(up) || up <= sp) break;
        ret = *(volatile uint32_t *)(uintptr_t)(up + 4);
        if (crash_addr_ok(ret)) crash_add("frame=0x%08x\n", (unsigned)ret);
        sp = up;
    }
}

static int crash_save(void)
{
    char path[96];
    FILE *f;
    mkdir("uda:/xenith", 0777);
    mkdir(CRASH_DIR, 0777);
    snprintf(path, sizeof path, CRASH_DIR "/%s.txt", crash_program);
    f = fopen(path, "w");
    if (!f) return 0;
    fwrite(crash_text, 1, crash_len, f);
    return fclose(f) == 0;
}

static void crash_print(const char *s)
{
    for (; *s; s++) console_putch(*s);
}

/* The controller is only read when that hasn't crashed already (can_poll); without it, the power
 * button is the way out. */
static void crash_screen(int saved, int can_poll)
{
    static const char *const ids[] = {"xenithos", "volt", "doom", "tube", "photos"};
    static const char *const names[] = {"XenithOS", "VOLT", "DOOM", "Tube", "Photos"};
    const char *name = "The app";
    struct controller_data_s c;
    char line[120];
    int i, let_go = 0;

    for (i = 0; i < (int)(sizeof ids / sizeof ids[0]); i++)
        if (!strcmp(crash_program, ids[i])) name = names[i];
    if (can_poll)
        for (i = 0; i < crash_len; i++) putch((unsigned char)crash_text[i]);    /* the serial port */

    if (!xenos_is_initialized()) xenos_init(VIDEO_MODE_AUTO);
    console_set_colors(0x0a080700, 0xffffff00);       /* XenithOS's near-black, white text */
    console_init();
    console_clrscr();
    snprintf(line, sizeof line, "\n\n\n   %s stopped working.\n\n", name);
    crash_print(line);
    crash_print(saved ? "   What happened is saved on your USB stick. When XenithOS starts again it can\n"
                        "   send a crash report to the people who make it, so it can be fixed.\n\n\n"
                      : "   What happened couldn't be saved this time.\n\n\n");
    if (!can_poll) {
        crash_print("   Hold the power button to turn the console off.\n");
        for (;;) mdelay(100);
    }
    crash_print("   A   Restart the console\n   B   Turn it off\n   X   Go back to XeLL\n");

    for (;;) {                                        /* buttons held as it crashed don't count */
        memset(&c, 0, sizeof c);
        usb_do_poll();
        get_controller_data(&c, 0);
        if (!c.a && !c.b && !c.x) let_go = 1;
        else if (let_go && c.a) { xenon_smc_power_reboot(); for (;;) mdelay(100); }
        else if (let_go && c.b) { xenon_smc_power_shutdown(); for (;;) mdelay(100); }
        else if (let_go && c.x) exit(0);
        mdelay(20);
    }
}

static void crash_finish(void)
{
    int saved = 0;
    crash_pending = 0;
    plat_audio_stop();                                /* or the chip loops its last block */
    if (crash_len && !crash_saving) {
        crash_saving = 1;                             /* if saving crashes, not again */
        saved = crash_save();
    }
    crash_screen(saved, crash_depth < 3);
}

void crashdump(uint32_t exception, uint64_t *context);

void crashdump(uint32_t exception, uint64_t *context)
{
    int depth = ++crash_depth;
    if (depth > 4) for (;;) udelay(1000);              /* even the screen crashes: stop here */
    if (context[39] != 0) {                           /* not the main thread: it takes over */
        if (depth == 1) {
            crash_build(exception, context);
            __sync_synchronize();
            crash_pending = 1;
            __sync_synchronize();
        }
        for (;;) udelay(1000);
    }
    if (depth == 1) crash_build(exception, context);
    crash_finish();
}
