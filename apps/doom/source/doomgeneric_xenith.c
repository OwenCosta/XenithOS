/* DOOM for XenithOS: the doomgeneric platform layer.
 *
 * doomgeneric renders 320x200 frames as 0x00RRGGBB pixels. They're stretched to 4:3 in
 * the middle of the TV, controller changes become Doom key events, and quitting Doom
 * (or holding BACK) goes back to XenithOS.
 *
 * Controls
 *   Left stick      move / strafe          Right stick / D-pad  turn (D-pad up/down move too)
 *   RT              fire                   LT or L3             run
 *   A               use / menu select      B                    menu back
 *   X               map                    Y                    "yes" in prompts
 *   LB / RB         previous / next weapon START                menu
 *   hold BACK       quit to XenithOS
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "doomgeneric.h"
#include "doomkeys.h"

#include "gfx.h"
#include "platform.h"
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
    if (next == q_tail) return;
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
static struct pad pad, last_pad;
static int weapon = 1, weapon_release_at = -1, polls;
static uint64_t back_since;

static void back_to_xenith(void)
{
    plat_rumble(0, 0);
    plat_launch(XENITH_SHELL_PATH);
}

static void poll_pad(void)
{
    int want[B_COUNT], i;

    last_pad = pad;
    plat_poll(&pad);
    polls++;

    want[B_UP]        = pad.up || pad.ly > STICK_TH;
    want[B_DOWN]      = pad.down || pad.ly < -STICK_TH;
    want[B_TURN_L]    = pad.left || pad.rx < -STICK_TH;
    want[B_TURN_R]    = pad.right || pad.rx > STICK_TH;
    want[B_STRAFE_L]  = pad.lx < -STICK_TH;
    want[B_STRAFE_R]  = pad.lx > STICK_TH;
    want[B_FIRE]      = pad.rt > TRIGGER_TH;
    want[B_USE]       = pad.a;
    want[B_ENTER]     = pad.a;
    want[B_BACKSPACE] = pad.b;
    want[B_MENU]      = pad.start;
    want[B_MAP]       = pad.x;
    want[B_RUN]       = pad.lt > TRIGGER_TH || pad.ls;
    want[B_YES]       = pad.y;

    for (i = 0; i < B_COUNT; i++)
        if (want[i] != held[i]) {
            held[i] = want[i];
            push_key(want[i], bind_key[i]);
        }

    /* Weapon cycling: hold the number key for a few polls so a game tic sees it. */
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
    if (pad.back) {
        if (!back_since) back_since = plat_time_us();
        if (plat_time_us() - back_since > 1000000) back_to_xenith();
    } else {
        back_since = 0;
    }
}

/* ---------------------------------------------------------- doomgeneric */
static int dst_x, dst_y, dst_w, dst_h;

void DG_Init(void)
{
    dst_h = SCR_H;
    dst_w = dst_h * 4 / 3;
    if (dst_w > SCR_W) {
        dst_w = SCR_W;
        dst_h = dst_w * 3 / 4;
    }
    dst_x = (SCR_W - dst_w) / 2;
    dst_y = (SCR_H - dst_h) / 2;
    gfx_clear(0);
    gfx_present();
}

void DG_DrawFrame(void)
{
    gfx_blit_scaled(DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY, dst_x, dst_y, dst_w, dst_h);
    gfx_present();
    poll_pad();
}

void DG_SleepMs(uint32_t ms)
{
    plat_sleep_us((int)ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)(plat_time_us() / 1000);
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
    "doom2.wad", "doom.wad", "plutonia.wad", "tnt.wad", "doom1.wad", "freedoom2.wad", "freedoom1.wad",
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
    int y = SCR_H / 2 - 120;
    struct pad c, p;

    gfx_clear(C_BG);
    gfx_str_c(FONT_HEAD, SCR_W / 2, y, C_TEXT, "DOOM needs game data");
    y += gfx_line_h(FONT_HEAD) + 18;
    gfx_str_c(FONT_BODY, SCR_W / 2, y, C_SUB, "Install Freedoom from the Store. It's free.");
    y += 36;
    gfx_str_c(FONT_BODY, SCR_W / 2, y, C_SUB, "Or copy your own doom1.wad or doom2.wad into a doom folder on the USB stick.");
    y += 80;
    gfx_str_c(FONT_BODY_SB, SCR_W / 2, y, C_ACCENT_HI, "Press A to go back to XenithOS");
    gfx_present();

    memset(&p, 0, sizeof p);
    for (;;) {
        plat_poll(&c);
        if ((c.a && !p.a) || (c.b && !p.b) || (c.back && !p.back)) break;
        p = c;
        plat_sleep_us(16000);
    }
}

int main(void)
{
    static char wad[64];
    static char *args[] = {"doom", "-iwad", wad, NULL};

    plat_init();
    gfx_init();
    plat_storage_init();

    if (!find_wad(wad, sizeof wad)) {
        no_wad_screen();
        back_to_xenith();
        return 0;
    }

    mkdir("uda:/doom", 0777);
    atexit(back_to_xenith);       /* Doom's Quit, and its fatal errors, return to XenithOS */

    doomgeneric_Create(3, args);
    for (;;)
        doomgeneric_Tick();
    return 0;
}
