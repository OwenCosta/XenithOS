/* XENITH OS: a custom console shell for the Xbox 360, built on LibXenon.
 *
 * Boots from XeLL, shows the XENITH splash, then a home menu:
 *   APPS        find .elf programs on USB/HDD and launch them
 *   STORE       browse and install apps from XENITH Cloud (screens_cloud.c)
 *   CLOUD       live server stats, inbox, leaderboards (screens_cloud.c)
 *   SYSTEM      live temperatures, video output, hardware info
 *   CONTROLLER  button/stick/trigger tester, triggers drive rumble
 *   RESTART / POWER OFF
 * GUIDE on the home screen exits back to XeLL (handy while developing).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

#include <xenos/xenos.h>
#include <console/console.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <time/time.h>
#include <diskio/ata.h>
#include <elf/elf.h>
#include <libfat/fat.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_smc/xenon_smc.h>

#include "gfx.h"
#include "theme.h"
#include "ui.h"
#include "net.h"
#include "config.h"
#include "screens.h"

#define XENITH_VERSION "0.3.2"

int bdev_enum(int handle, const char **name);

/* ------------------------------------------------------------ boot splash */
static void boot_splash(void)
{
    int L = S * 5;
    int y = SCR_H / 2 - GFX_TEXT_H(L) / 2 - L;
    int bw = GFX_BRAND_W(L);
    int bx = SCR_W / 2 - bw / 2;
    int by = y + GFX_TEXT_H(L) + L * 2;
    int t, i;

    gfx_clear(C_BG);
    gfx_flush();

    for (t = 0; t <= 16; t++) {
        gfx_brand(bx, y, L, gfx_mix(C_BG, C_TEXT, t * 16), gfx_mix(C_BG, C_ACCENT, t * 16));
        gfx_flush();
        usb_do_poll();
        mdelay(35);
    }

    /* Storage init is slow-ish; do it while the accent bar sweeps. */
    for (i = 1; i <= 24; i++) {
        gfx_rect(bx, by, bw * i / 24, S * 2, C_ACCENT);
        gfx_flush();
        usb_do_poll();
        if (i == 8) { xenon_ata_init(); xenon_atapi_init(); fatInitDefault(); config_load(); }
        mdelay(20);
    }

    set_controller_rumble(0, 60, 160);
    mdelay(180);
    set_controller_rumble(0, 0, 0);

    gfx_text_c(SCR_W / 2, by + S * 12, S, C_DIM, "VERSION " XENITH_VERSION);
    gfx_flush();
    mdelay(900);
}

/* ------------------------------------------------------------------- apps */
#define MAX_APPS 64
static char apps[MAX_APPS][256];
static int napps;

static int is_elf(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".elf") || !strcasecmp(dot, ".elf32"));
}

static void scan_dir(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *de;
    if (!d) return;
    while ((de = readdir(d)) && napps < MAX_APPS) {
        if (de->d_type & DT_DIR) continue;
        if (!is_elf(de->d_name)) continue;
        snprintf(apps[napps++], sizeof apps[0], "%s%s", dir, de->d_name);
    }
    closedir(d);
}

static void scan_apps(void)
{
    const char *name;
    char root[64], sub[80];
    int h = -1;

    napps = 0;
    while ((h = bdev_enum(h, &name)) >= 0) {
        snprintf(root, sizeof root, "%s:/", name);
        snprintf(sub, sizeof sub, "%sapps/", root);
        scan_dir(root);
        scan_dir(sub);
    }
}

static const char *basename_of(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static void screen_apps(void)
{
    int rowh = GFX_TEXT_H(S) + S * 10;
    int visible = (SCR_H - TOP - M) / rowh;
    int sel = 0, first = -1, i;

    draw_chrome("APPS", "A LAUNCH     B BACK     Y RESCAN");
    gfx_text(M, TOP, S, C_DIM, "SCANNING...");
    gfx_flush();
    scan_apps();

redraw:
    gfx_rect(0, TOP, SCR_W, SCR_H - TOP - M, C_BG);
    if (!napps) {
        gfx_text(M, TOP, S, C_TEXT, "NO APPS FOUND");
        gfx_text(M, TOP + rowh, S - 1, C_DIM, "PUT .ELF FILES ON A USB STICK, IN THE ROOT OR AN APPS FOLDER.");
    }
    first = -1;

    for (;;) {
        if (napps) {
            int top = sel < visible ? 0 : sel - visible + 1;
            if (top != first) {
                first = top;
                for (i = 0; i < visible; i++) {
                    int idx = first + i;
                    if (idx < napps)
                        draw_row(TOP + i * rowh, rowh - S * 2, idx == sel, S, basename_of(apps[idx]), apps[idx]);
                    else
                        gfx_rect(M, TOP + i * rowh, SCR_W - 2 * M, rowh, C_BG);
                }
                gfx_flush();
            }
        }
        gfx_flush();
        poll_input();

        if (PRESSED(b)) return;
        if (PRESSED(y)) {
            gfx_rect(0, TOP, SCR_W, SCR_H - TOP - M, C_BG);
            gfx_text(M, TOP, S, C_DIM, "SCANNING...");
            gfx_flush();
            scan_apps();
            sel = 0;
            goto redraw;
        }
        if (!napps) continue;

        int d = nav_y();
        if (d) {
            int old = sel;
            sel = (sel + d + napps) % napps;
            int top = sel < visible ? 0 : sel - visible + 1;
            if (top == first) {   /* same page: redraw just the two rows */
                draw_row(TOP + (old - first) * rowh, rowh - S * 2, 0, S, basename_of(apps[old]), apps[old]);
                draw_row(TOP + (sel - first) * rowh, rowh - S * 2, 1, S, basename_of(apps[sel]), apps[sel]);
            }
        }

        if (PRESSED(a)) {
            draw_chrome("APPS", NULL);
            gfx_text_c(SCR_W / 2, SCR_H / 2 - GFX_TEXT_H(S), S, C_DIM, "LAUNCHING");
            gfx_text_c(SCR_W / 2, SCR_H / 2 + S * 4, S + 1, C_TEXT, basename_of(apps[sel]));
            gfx_flush();
            set_controller_rumble(0, 0, 0);
            elf_runFromDisk(apps[sel]);           /* only returns if loading failed */
            draw_chrome("APPS", NULL);
            gfx_text_c(SCR_W / 2, SCR_H / 2, S, C_BAD, "COULD NOT LOAD THAT FILE");
            gfx_flush();
            mdelay(1500);
            draw_chrome("APPS", "A LAUNCH     B BACK     Y RESCAN");
            goto redraw;
        }
    }
}

/* ----------------------------------------------------------------- system */
static const char *av_name(int pack)
{
    if (xenon_avpack_is_hdmi(pack)) return "HDMI";
    switch (xenon_avpack_type(pack)) {
    case AV_VGA:          return "VGA";
    case AV_COMPOSITE:    return "COMPOSITE";
    case AV_AUDIO_ONLY:   return "AUDIO ONLY";
    case AV_COMPONENT_HD: return "COMPONENT HD";
    case AV_COMPONENT_TV: return "COMPONENT SD";
    case AV_SVIDEO:       return "S-VIDEO";
    case AV_SCART_RGB:    return "SCART RGB";
    default:              return "NONE";
    }
}

static void screen_system(void)
{
    static const char *labels[4] = {"CPU", "GPU", "MEMORY", "BOARD"};
    char buf[64];
    int rowh = GFX_TEXT_H(S) + S * 9;
    int barx = M + S * 6 * 8;            /* after the label column */
    int barw = SCR_W - barx - M - S * 6 * 6;
    int info_y = TOP + rowh * 5;
    int tick = 0, i;

    draw_chrome("SYSTEM", "B BACK");
    gfx_text(M, TOP, S - 1, C_DIM, "TEMPERATURES");

    snprintf(buf, sizeof buf, "VIDEO OUT    %s    %dX%d", av_name(xenon_smc_read_avpack()), SCR_W, SCR_H);
    gfx_text(M, info_y, S, C_TEXT, buf);
    gfx_text(M, info_y + rowh, S, C_TEXT, "CPU          XENON  3 CORES  6 THREADS  3.2 GHZ");
    gfx_text(M, info_y + rowh * 2, S, C_TEXT, "SOFTWARE     XENITHOS " XENITH_VERSION "  ON LIBXENON");

    for (;;) {
        if (tick-- <= 0) {
            uint16_t raw[4];
            tick = 60;
            xenon_smc_query_sensors(raw);
            for (i = 0; i < 4; i++) {
                int t = raw[i] >> 8;              /* sensor reports 1/256 degree C */
                int y = TOP + S * 10 + i * rowh;
                uint32_t c = t >= 85 ? C_BAD : t >= 70 ? C_WARN : C_OK;
                int fill = barw * (t > 100 ? 100 : t) / 100;

                gfx_rect(M, y, SCR_W - 2 * M, GFX_TEXT_H(S), C_BG);
                gfx_text(M, y, S, C_DIM, labels[i]);
                gfx_rect(barx, y + S * 2, barw, S * 3, C_PANEL);
                gfx_rect(barx, y + S * 2, fill, S * 3, c);
                snprintf(buf, sizeof buf, "%d C", t);
                gfx_text(barx + barw + S * 6, y, S, C_TEXT, buf);
            }
            gfx_flush();
        }
        poll_input();
        if (PRESSED(b)) return;
    }
}

/* ------------------------------------------------------------- controller */
struct btn { const char *name; int on; };

static void draw_stick(int x, int y, int size, int sx, int sy, int click)
{
    int r = size / 2;
    int dot = S * 4;
    int px = x + r + (sx * (r - dot)) / 32768;
    int py = y + r - (sy * (r - dot)) / 32768;
    gfx_rect(x, y, size, size, C_PANEL);
    gfx_rect(x + r, y, 1, size, C_BG);
    gfx_rect(x, y + r, size, 1, C_BG);
    gfx_rect(px - dot / 2, py - dot / 2, dot, dot, click ? C_ACCENT : C_TEXT);
}

static void draw_trigger(int x, int y, int w, int h, int v, const char *name)
{
    gfx_rect(x, y, w, h, C_PANEL);
    gfx_rect(x, y + h - h * v / 255, w, h * v / 255, C_ACCENT);
    gfx_text_c(x + w / 2, y + h + S * 3, S - 1, C_DIM, name);
}

static void screen_controller(void)
{
    int last_on[16];
    int bw = S * 6 * 6, bh = GFX_TEXT_H(S) + S * 6, gap = S * 3;
    int stick = SCR_H / 4;
    int right_x = M + 4 * (bw + gap) + S * 16;
    int trig_w = S * 8, trig_h = stick;
    int hold_b = 0, first = 1, i;
    struct controller_data_s drawn;

    memset(last_on, -1, sizeof last_on);
    memset(&drawn, 0, sizeof drawn);
    draw_chrome("CONTROLLER", "TRIGGERS RUMBLE THE PAD     HOLD B TO EXIT");

    for (;;) {
        poll_input();

        struct btn b[] = {
            {"A", cur.a}, {"B", cur.b}, {"X", cur.x}, {"Y", cur.y},
            {"LB", cur.lb}, {"RB", cur.rb}, {"START", cur.start}, {"BACK", cur.back},
            {"UP", cur.up}, {"DOWN", cur.down}, {"LEFT", cur.left}, {"RIGHT", cur.right},
            {"LS", cur.s1_z}, {"RS", cur.s2_z}, {"GUIDE", cur.logo},
        };
        int n = sizeof b / sizeof b[0];
        int dirty = 0;

        for (i = 0; i < n; i++) {
            int on = b[i].on ? 1 : 0;
            if (on == last_on[i]) continue;
            last_on[i] = on;
            int x = M + (i % 4) * (bw + gap), y = TOP + (i / 4) * (bh + gap);
            gfx_rect(x, y, bw, bh, on ? C_ACCENT : C_PANEL);
            gfx_text_c(x + bw / 2, y + (bh - GFX_TEXT_H(S)) / 2, S, on ? C_TEXT : C_DIM, b[i].name);
            dirty = 1;
        }

        if (first || cur.s1_x != drawn.s1_x || cur.s1_y != drawn.s1_y || cur.s1_z != drawn.s1_z) {
            draw_stick(right_x, TOP, stick, cur.s1_x, cur.s1_y, cur.s1_z);
            dirty = 1;
        }
        if (first || cur.s2_x != drawn.s2_x || cur.s2_y != drawn.s2_y || cur.s2_z != drawn.s2_z) {
            draw_stick(right_x + stick + S * 8, TOP, stick, cur.s2_x, cur.s2_y, cur.s2_z);
            dirty = 1;
        }
        if (first || cur.lt != drawn.lt || cur.rt != drawn.rt) {
            int tx = right_x + 2 * stick + S * 20;
            draw_trigger(tx, TOP, trig_w, trig_h, cur.lt, "LT");
            draw_trigger(tx + trig_w + S * 8, TOP, trig_w, trig_h, cur.rt, "RT");
            set_controller_rumble(0, cur.lt, cur.rt);
            dirty = 1;
        }

        /* Hold-B progress bar under the grid. */
        hold_b = cur.b ? hold_b + 1 : 0;
        {
            int y = TOP + 4 * (bh + gap) + S * 4;
            int w = 4 * (bw + gap) - gap;
            gfx_rect(M, y, w, S, C_BG);
            if (hold_b) gfx_rect(M, y, w * hold_b / 60, S, C_ACCENT);
            dirty = 1;
        }

        drawn = cur;
        first = 0;
        if (dirty) gfx_flush();

        if (hold_b >= 60) {
            set_controller_rumble(0, 0, 0);
            wait_release();
            return;
        }
    }
}

/* ------------------------------------------------------------------- home */
enum { ITEM_APPS, ITEM_STORE, ITEM_CLOUD, ITEM_SYSTEM, ITEM_CONTROLLER, ITEM_RESTART, ITEM_POWER, ITEM_COUNT };

static const char *home_items[ITEM_COUNT] = {"APPS", "STORE", "CLOUD", "SYSTEM", "CONTROLLER", "RESTART", "POWER OFF"};
static const char *home_desc[ITEM_COUNT] = {
    "LAUNCH PROGRAMS FROM USB OR HARD DRIVE",
    "GET NEW APPS AND GAMES FROM XENITHOS CLOUD",
    "YOUR SERVER: LIVE STATS, MESSAGES, LEADERBOARDS",
    "TEMPERATURES, VIDEO OUTPUT AND HARDWARE",
    "TEST EVERY BUTTON, STICK AND TRIGGER",
    "REBOOT THE CONSOLE",
    "TURN THE CONSOLE OFF",
};

static int home_rowh(void) { return GFX_TEXT_H(S + 1) + S * 10; }

static void draw_home_item(int i, int sel)
{
    draw_row(TOP + i * home_rowh(), home_rowh() - S * 2, i == sel, S + 1, home_items[i], NULL);
}

static void draw_home_desc(int sel)
{
    int y = TOP + ITEM_COUNT * home_rowh() + S * 6;
    gfx_rect(M, y, SCR_W - 2 * M, GFX_TEXT_H(S), C_BG);
    gfx_text(M + S * 8, y, S, C_DIM, home_desc[sel]);
}

static void draw_home(int sel)
{
    int i;
    draw_chrome(NULL, "A SELECT     GUIDE EXIT TO XELL");
    for (i = 0; i < ITEM_COUNT; i++) draw_home_item(i, sel);
    draw_home_desc(sel);
    gfx_flush();
}

int main(void)
{
    int sel = 0;

    xenos_init(VIDEO_MODE_AUTO);
    console_init();
    console_close();                 /* stop printf from drawing over the UI */
    xenon_make_it_faster(XENON_SPEED_FULL);

    gfx_init();
    ui_init();

    usb_init();
    usb_do_poll();

    boot_splash();
    draw_home(sel);

    for (;;) {
        poll_input();

        int d = nav_y();
        if (d) {
            int old = sel;
            sel = (sel + d + ITEM_COUNT) % ITEM_COUNT;
            draw_home_item(old, sel);
            draw_home_item(sel, sel);
            draw_home_desc(sel);
            gfx_flush();
        }

        if (PRESSED(logo)) {
            set_controller_rumble(0, 0, 0);
            return 0;                /* back to XeLL */
        }

        if (!PRESSED(a)) continue;
        wait_release();

        switch (sel) {
        case ITEM_APPS:       screen_apps(); break;
        case ITEM_STORE:      screen_store(); break;
        case ITEM_CLOUD:      screen_cloud(); break;
        case ITEM_SYSTEM:     screen_system(); break;
        case ITEM_CONTROLLER: screen_controller(); break;
        case ITEM_RESTART:
            if (confirm("RESTART THE CONSOLE?")) xenon_smc_power_reboot();
            break;
        case ITEM_POWER:
            if (confirm("TURN THE CONSOLE OFF?")) xenon_smc_power_shutdown();
            break;
        }
        wait_release();
        draw_home(sel);
    }
}
