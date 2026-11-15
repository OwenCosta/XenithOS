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
static int fb_bytes, fb_pitch, off_x, off_y, scr_w, scr_h;

/* The framebuffer is stored in 32x32 tiles with a fixed pixel order inside each tile.
 * These tables turn (x, y) into a framebuffer index with two lookups and two adds. */
static uint16_t tile_inner[32 * 32];
static int *col_tile, *col_in, *row_base;

void plat_init(void)
{
    struct ati_info *ai;
    int x, y;

    xenos_init(VIDEO_MODE_AUTO);
    xenon_make_it_faster(XENON_SPEED_FULL);

    ai = (struct ati_info *)0xec806100ULL;
    fb = (uint32_t *)(long)(ai->base | 0x80000000);
    fb_pitch = ((ai->width + 31) >> 5) << 5;
    fb_bytes = fb_pitch * (((ai->height + 31) >> 5) << 5) * 4;

    off_x = off_y = 0;
    if (xenos_is_overscan()) {
        off_x = ai->width / 28;
        off_y = ai->height / 28;
    }
    scr_w = ((ai->width - off_x * 2) / 8) * 8;
    scr_h = ((ai->height - off_y * 2) / 8) * 8;

    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++)
            tile_inner[y * 32 + x] = (uint16_t)(((x & 3) + ((y & 1) << 2) + ((x >> 2) << 3) + ((y >> 1) << 6))
                                                ^ ((y & 8) << 2));
    col_tile = malloc(sizeof(int) * scr_w);
    col_in = malloc(sizeof(int) * scr_w);
    row_base = malloc(sizeof(int) * scr_h);
    for (x = 0; x < scr_w; x++) {
        int X = x + off_x;
        col_tile[x] = (X >> 5) << 10;
        col_in[x] = X & 31;
    }
    for (y = 0; y < scr_h; y++) {
        int Y = y + off_y;
        row_base[y] = (Y >> 5) * 32 * fb_pitch;
    }

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

void plat_present(const uint32_t *px)
{
    int x, y;
    for (y = 0; y < scr_h; y++) {
        const uint16_t *inner = &tile_inner[((y + off_y) & 31) * 32];
        uint32_t *dst = fb + row_base[y];
        const uint32_t *src = px + y * scr_w;
        for (x = 0; x < scr_w; x++)
            dst[col_tile[x] + inner[col_in[x]]] = __builtin_bswap32(src[x]);   /* 0xBBGGRR00 */
    }
    memdcbst(fb, fb_bytes);
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
    plat_rumble(0, 0);
    elf_runFromDisk((char *)path);
}

void plat_reboot(void)
{
    xenon_smc_power_reboot();
}

void plat_power_off(void)
{
    xenon_smc_power_shutdown();
}
