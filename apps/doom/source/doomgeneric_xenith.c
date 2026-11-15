/* DOOM for XENITH: the doomgeneric platform layer.
 *
 * doomgeneric renders 320x200 frames as 0x00RRGGBB pixels. We stretch them to 4:3 in the
 * middle of the TV, turn controller changes into Doom key events, and send you back to
 * XENITH when Doom quits (or when you hold BACK).
 *
 * Controls
 *   Left stick      move / strafe          Right stick / D-pad  turn (D-pad up/down move too)
 *   RT              fire                   LT or L3             run
 *   A               use / menu select      B                    menu back
 *   X               map                    Y                    "yes" in prompts
 *   LB / RB         previous / next weapon START                menu
 *   hold BACK       quit to XENITH
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <xenos/xenos.h>
#include <console/console.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <time/time.h>
#include <elf/elf.h>
#include <libfat/fat.h>
#include <ppc/timebase.h>
#include <xenon_soc/xenon_power.h>

#include "doomgeneric.h"
#include "doomkeys.h"

#include "gfx.h"
#include "theme.h"

#define STICK_TH 16000
#define TRIGGER_TH 100

/* ------------------------------------------------------------- key queue */
#define QSIZE 64
static unsigned short queue[QSIZE];      /* (pressed << 8) | key */
static int q_head, q_tail;

static void push_key(int pressed, unsigned char key)
{
    int next = (q_head + 1) % QSIZE;
    if (next == q_tail) return;          /* full: drop */
    queue[q_head] = (unsigned short)((pressed ? 0x100 : 0) | key);
    q_head = next;
}

/* --------------------------------------------------------- controller map */
enum {
    B_UP, B_DOWN, B_TURN_L, B_TURN_R, B_STRAFE_L, B_STRAFE_R, B_FIRE, B_USE, B_ENTER,
    B_BACKSPACE, B_MENU, B_MAP, B_RUN, B_YES, B_COUNT
};

static const unsigned char bind_key[B_COUNT] = {
    KEY_UPARROW, KEY_DOWNARROW, KEY_LEFTARROW, KEY_RIGHTARROW, KEY_STRAFE_L, KEY_STRAFE_R,
    KEY_FIRE, KEY_USE, KEY_ENTER, KEY_BACKSPACE, KEY_ESCAPE, KEY_TAB, KEY_RSHIFT, 'y',
};

static int held[B_COUNT];
static struct controller_data_s pad, last_pad;
static int weapon = 1;               /* the weapon key we last selected, '1'..'7' */
static int weapon_release_at = -1;   /* poll count when to release the weapon key */
static int polls;
static int back_frames;

static void back_to_xenith(void)
{
    set_controller_rumble(0, 0, 0);
    elf_runFromDisk(XENITH_SHELL_PATH);
}

static void poll_pad(void)
{
    int want[B_COUNT], i;

    usb_do_poll();
    last_pad = pad;
    get_controller_data(&pad, 0);
    polls++;

    want[B_UP]        = pad.up || pad.s1_y > STICK_TH;
    want[B_DOWN]      = pad.down || pad.s1_y < -STICK_TH;
    want[B_TURN_L]    = pad.left || pad.s2_x < -STICK_TH;
    want[B_TURN_R]    = pad.right || pad.s2_x > STICK_TH;
    want[B_STRAFE_L]  = pad.s1_x < -STICK_TH;
    want[B_STRAFE_R]  = pad.s1_x > STICK_TH;
    want[B_FIRE]      = pad.rt > TRIGGER_TH;
    want[B_USE]       = pad.a;
    want[B_ENTER]     = pad.a;
    want[B_BACKSPACE] = pad.b;
    want[B_MENU]      = pad.start;
    want[B_MAP]       = pad.x;
    want[B_RUN]       = pad.lt > TRIGGER_TH || pad.s1_z;
    want[B_YES]       = pad.y;

    for (i = 0; i < B_COUNT; i++) {
        if (want[i] != held[i]) {
            held[i] = want[i];
            push_key(want[i], bind_key[i]);
        }
    }

    /* Weapon cycling: hold the number key for a couple of polls so a game tic sees it. */
    if (weapon_release_at >= 0 && polls >= weapon_release_at) {
        push_key(0, (unsigned char)('0' + weapon));
        weapon_release_at = -1;
    }
    if (weapon_release_at < 0 && ((pad.rb && !last_pad.rb) || (pad.lb && !last_pad.lb))) {
        weapon += pad.rb ? 1 : -1;
        if (weapon > 7) weapon = 1;
        if (weapon < 1) weapon = 7;
        push_key(1, (unsigned char)('0' + weapon));
        weapon_release_at = polls + 3;
    }

    /* Hold BACK for about a second to leave. */
    back_frames = pad.back ? back_frames + 1 : 0;
    if (back_frames > 35) back_to_xenith();
}

/* ---------------------------------------------------------- doomgeneric */
static int dst_x, dst_y, dst_w, dst_h;

void DG_Init(void)
{
    /* 4:3, as tall as the safe area allows. */
    dst_h = SCR_H;
    dst_w = dst_h * 4 / 3;
    if (dst_w > SCR_W) {
        dst_w = SCR_W;
        dst_h = dst_w * 3 / 4;
    }
    dst_x = (SCR_W - dst_w) / 2;
    dst_y = (SCR_H - dst_h) / 2;
    gfx_clear(0);
    gfx_flush();
}

void DG_DrawFrame(void)
{
    gfx_blit_scaled(DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY, dst_x, dst_y, dst_w, dst_h);
    gfx_flush();
    poll_pad();
}

void DG_SleepMs(uint32_t ms)
{
    mdelay(ms);
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)(mftb() / (PPC_TIMEBASE_FREQ / 1000));
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    if (q_tail == q_head) return 0;
    *pressed = (queue[q_tail] >> 8) & 1;
    *key = (unsigned char)(queue[q_tail] & 0xff);
    q_tail = (q_tail + 1) % QSIZE;
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

/* ------------------------------------------------------------------ main */
static const char *wad_dirs[] = {"uda:/data/", "uda:/doom/", "uda:/"};
static const char *wad_names[] = {
    "doom2.wad", "doom.wad", "plutonia.wad", "tnt.wad", "doom1.wad",
    "freedoom2.wad", "freedoom1.wad",
};

static int find_wad(char *out, int size)
{
    unsigned d, n;
    struct stat st;
    for (n = 0; n < sizeof wad_names / sizeof *wad_names; n++)
        for (d = 0; d < sizeof wad_dirs / sizeof *wad_dirs; d++) {
            snprintf(out, size, "%s%s", wad_dirs[d], wad_names[n]);
            if (stat(out, &st) == 0 && st.st_size > 0) return 1;
        }
    return 0;
}

static void no_wad_screen(void)
{
    int S = SCR_H >= 600 ? 3 : 2;
    int y = SCR_H / 2 - GFX_TEXT_H(S + 3);
    struct controller_data_s c, p;

    gfx_clear(C_BG);
    gfx_text_c(SCR_W / 2, y, S + 3, C_BAD, "DOOM");
    y += GFX_TEXT_H(S + 3) + S * 8;
    gfx_text_c(SCR_W / 2, y, S, C_TEXT, "NO GAME DATA (WAD) FOUND");
    y += GFX_TEXT_H(S) + S * 6;
    gfx_text_c(SCR_W / 2, y, S - 1, C_DIM, "INSTALL FREEDOOM FROM THE XENITHOS STORE, OR COPY");
    y += GFX_TEXT_H(S - 1) + S * 3;
    gfx_text_c(SCR_W / 2, y, S - 1, C_DIM, "DOOM1.WAD / DOOM2.WAD INTO A DOOM FOLDER ON THE USB STICK");
    y += GFX_TEXT_H(S - 1) + S * 10;
    gfx_text_c(SCR_W / 2, y, S - 1, C_DIM, "PRESS A TO RETURN TO XENITHOS");
    gfx_flush();

    memset(&p, 0, sizeof p);
    for (;;) {
        usb_do_poll();
        get_controller_data(&c, 0);
        if ((c.a && !p.a) || (c.b && !p.b) || (c.back && !p.back)) break;
        p = c;
        mdelay(16);
    }
}

int main(void)
{
    static char wad[64];
    static char *args[] = {"doom", "-iwad", wad, NULL};

    xenos_init(VIDEO_MODE_AUTO);
    console_init();
    console_close();
    xenon_make_it_faster(XENON_SPEED_FULL);
    gfx_init();

    usb_init();
    usb_do_poll();
    fatInitDefault();

    if (!find_wad(wad, sizeof wad)) {
        no_wad_screen();
        back_to_xenith();
        return 0;
    }

    mkdir("uda:/doom", 0777);
    atexit(back_to_xenith);       /* Doom's Quit, and its fatal errors, return to XENITH */

    doomgeneric_Create(3, args);
    for (;;)
        doomgeneric_Tick();
    return 0;
}
