/* APPS: everything installed, as a grid of thumbnails. */
#include <stdio.h>

#include "apps.h"
#include "audio.h"
#include "gfx.h"
#include "icons.h"
#include "platform.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

void screen_apps(void)
{
    int sel = 0, cols = 3, gap = 24, first_row = 0;
    float lift[MAX_APPS] = {0};
    char sub[64];

    apps_scan();
    ui_fade_in();
    for (;;) {
        int cw = (SCR_W - 2 * M - gap * (cols - 1)) / cols, ch = cw * 9 / 16, rowh = ch + 70;
        int top = HEADER_H + 20, visible_rows = (SCR_H - top - 70) / rowh, i, dx, dy;
        if (visible_rows < 1) visible_rows = 1;

        ui_poll();
        dx = ui_nav_x();
        dy = ui_nav_y();
        if (napps) {
            int n = sel + dx + dy * cols;
            if ((dx || dy) && n >= 0 && n < napps && !(dx && n / cols != sel / cols)) { sel = n; sfx(SFX_MOVE); }
        }
        if (PRESSED(b)) { sfx(SFX_BACK); return; }
        if (PRESSED(y)) { sfx(SFX_SELECT); apps_scan(); if (sel >= napps) sel = napps ? napps - 1 : 0; }
        if (PRESSED(a) && napps) { sfx(SFX_SELECT); app_launch(&apps[sel]); }

        if (sel / cols < first_row) first_row = sel / cols;
        if (sel / cols >= first_row + visible_rows) first_row = sel / cols - visible_rows + 1;

        ui_bg();
        if (plat_kind() == PLAT_PC) snprintf(sub, sizeof sub, napps == 1 ? "1 app and game" : "%d apps and games", napps);
        else snprintf(sub, sizeof sub, napps == 1 ? "1 app on your USB stick" : "%d apps on your USB stick", napps);
        ui_header("Apps", sub);
        if (!napps) {
            icon_draw(ICON_APPS, SCR_W / 2.0f, SCR_H / 2.0f - 40, 70, C_DIM, C_BG);
            gfx_str_c(FONT_LABEL, SCR_W / 2, SCR_H / 2 + 20, C_TEXT, "No apps yet");
            gfx_str_c(FONT_BODY, SCR_W / 2, SCR_H / 2 + 58, C_SUB, plat_kind() == PLAT_PC ? "Get some from the Store." : "Get some from the Store, or copy .elf32 files into an apps folder.");
        }
        for (i = 0; i < napps; i++) {
            int r = i / cols - first_row, c = i % cols, x, y;
            if (r < 0 || r >= visible_rows) continue;
            x = M + c * (cw + gap);
            y = top + r * rowh;
            lift[i] = ui_ease(lift[i], i == sel ? 1.0f : 0.0f, 0.35f);
            app_card(&apps[i], x, y, cw, ch, i == sel, lift[i]);
            gfx_str_fit(FONT_BODY_SB, x, y + ch + 14, i == sel ? C_TEXT : C_SUB, apps[i].name, cw - 80);
            if (apps[i].version[0]) gfx_str_r(FONT_SMALL, x + cw, y + ch + 17, C_DIM, apps[i].version);
        }
        ui_hints(napps ? "A=Start;Y=Refresh;B=Back" : "Y=Refresh;B=Back");
        ui_end_frame();
    }
}
