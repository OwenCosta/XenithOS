/* APPS: everything installed, one to a line going down: its picture, its name, and its version
 * (or "App" for one from Flathub, or "Running" for a game the guide left running). Up and
 * down go through them, and the list slides to keep the one chosen in view. */
#include <stdio.h>
#include <string.h>

#include "apps.h"
#include "audio.h"
#include "gfx.h"
#include "icons.h"
#include "platform.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

#define ROW_H   84
#define THUMB_W 112
#define THUMB_H 63

void screen_apps(void)
{
    int sel = 0, first = 0;
    float hl = 0, scroll = -1;
    char sub[64], where[24];

    apps_scan();
    ui_fade_in();
    for (;;) {
        int top = HEADER_H + 14, w = SCR_W - 2 * M, visible = (SCR_H - top - 70) / ROW_H, i, dy, clip[4];
        const char *running = plat_running();
        if (visible < 1) visible = 1;

        ui_poll();
        dy = ui_nav_y();
        if (dy && napps && sel + dy >= 0 && sel + dy < napps) { sel += dy; sfx(SFX_MOVE); }
        if (PRESSED(b)) { sfx(SFX_BACK); return; }
        if (PRESSED(y)) { sfx(SFX_SELECT); apps_scan(); if (sel >= napps) sel = napps ? napps - 1 : 0; }
        if (PRESSED(a) && napps) { sfx(SFX_SELECT); app_launch(&apps[sel]); }

        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;
        if (first > napps - visible) first = napps > visible ? napps - visible : 0;
        scroll = scroll < 0 ? (float)(first * ROW_H) : ui_ease(scroll, (float)(first * ROW_H), 0.3f);
        hl = ui_ease(hl, (float)sel, 0.45f);

        ui_bg();
        if (plat_kind() == PLAT_PC) snprintf(sub, sizeof sub, napps == 1 ? "1 app and game" : "%d apps and games", napps);
        else snprintf(sub, sizeof sub, napps == 1 ? "1 app on your USB stick" : "%d apps on your USB stick", napps);
        ui_header("Apps", sub);
        if (!napps) {
            icon_draw(ICON_APPS, SCR_W / 2.0f, SCR_H / 2.0f - 40, 70, C_DIM, C_BG);
            gfx_str_c(FONT_LABEL, SCR_W / 2, SCR_H / 2 + 20, C_TEXT, "No apps yet");
            gfx_str_c(FONT_BODY, SCR_W / 2, SCR_H / 2 + 58, C_SUB, plat_kind() == PLAT_PC ? "Get some from the Store." : "Get some from the Store, or copy .elf32 files into an apps folder.");
        }

        gfx_get_clip(clip);
        gfx_clip_intersect(0, top - 6, SCR_W, visible * ROW_H + 6);
        if (napps) ui_select_box((float)M - 14, top + hl * ROW_H - scroll, (float)w + 28, ROW_H - 10.0f, 10);
        for (i = 0; i < napps; i++) {
            const struct app *a = &apps[i];
            int y = top + i * ROW_H - (int)(scroll + 0.5f), cy = y + (ROW_H - 10) / 2;
            if (y + ROW_H < top - 6) continue;
            if (y > top + visible * ROW_H) break;
            app_card(a, M, cy - THUMB_H / 2, THUMB_W, THUMB_H, 0, 0);
            gfx_str_vc(FONT_LABEL, M + THUMB_W + 24, cy, i == sel ? C_TEXT : gfx_mix(C_SUB, C_TEXT, 128), a->name);
            if (running && !strcmp(running, a->path)) {        /* left running from the guide */
                int tw = gfx_str_w(FONT_SMALL, "Running");
                gfx_circle((float)(M + w - 16 - tw - 12), (float)cy, 4, C_OK, 255);
                gfx_str_vc(FONT_SMALL, M + w - 16 - tw, cy, C_TEXT, "Running");
            } else {
                const char *what = a->version[0] ? a->version : "App";
                gfx_str_vc(FONT_SMALL, M + w - 16 - gfx_str_w(FONT_SMALL, what), cy, C_SUB, what);
            }
        }
        gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);

        if (napps > visible) {                       /* where in the list you are */
            snprintf(where, sizeof where, "%d of %d", sel + 1, napps);
            gfx_str_r(FONT_SMALL, SCR_W - M, SCR_H - 34 - gfx_line_h(FONT_SMALL) / 2, C_DIM, where);
        }
        ui_hints(napps ? "A=Start;Y=Refresh;B=Back" : "Y=Refresh;B=Back");
        ui_end_frame();
    }
}
