/* Avatar editor: pick each part with left/right, Y for a random look. */
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "avatar.h"
#include "gfx.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

int avatar_editor(char *code)
{
    int v[AV_PARTS], sel = 0, i;
    float hl = 0, pop = 0;
    char start[AVATAR_CODE_LEN + 1];

    avatar_parse(code, v);
    avatar_encode(v, start);
    ui_fade_in();

    for (;;) {
        int top = HEADER_H + 16, size = SCR_H - top - 110, rowh, lx, lw, dx, dy;
        if (size > 420) size = 420;
        rowh = (SCR_H - top - 80) / AV_PARTS;
        if (rowh > 58) rowh = 58;
        lx = M + size + 56;
        lw = SCR_W - M - lx;

        ui_poll();
        dy = ui_nav_y();
        dx = ui_nav_x();
        if (dy && sel + dy >= 0 && sel + dy < AV_PARTS) { sel += dy; sfx(SFX_MOVE); }
        if (dx) {
            v[sel] = (v[sel] + dx + avatar_count[sel]) % avatar_count[sel];
            sfx(SFX_MOVE);
            pop = 1;
        }
        if (PRESSED(y)) {
            avatar_random(v, (uint32_t)plat_time_us());
            sfx(SFX_SELECT);
            pop = 1;
        }
        if (PRESSED(a)) {
            avatar_encode(v, code);
            sfx(SFX_SELECT);
            return 1;
        }
        if (PRESSED(b)) {
            char now[AVATAR_CODE_LEN + 1];
            avatar_encode(v, now);
            if (strcmp(now, start) &&
                !ui_confirm("Discard changes?", "Your avatar will stay the way it was.", "Discard", "Keep editing")) continue;
            sfx(SFX_BACK);
            return 0;
        }

        ui_bg();
        ui_header("Avatar", "Make it yours. Left and right change the selected part.");
        pop = ui_ease(pop, 0, 0.25f);
        {
            int grow = (int)(pop * 10);
            avatar_draw_v(v, M - grow, top + 10 - grow, size + 2 * grow);
        }
        hl = ui_ease(hl, (float)sel, 0.45f);
        gfx_fill_rrect((float)lx, top + 10 + hl * rowh, (float)lw, rowh - 6.0f, 12, C_ACCENT, 255);
        for (i = 0; i < AV_PARTS; i++) {
            int y = top + 10 + i * rowh, cy = y + (rowh - 6) / 2, is = i == sel;
            char val[48];
            gfx_str_vc(FONT_BODY_SB, lx + 20, cy, is ? 0xFFFFFF : C_SUB, avatar_part_name[i]);
            snprintf(val, sizeof val, "%s", avatar_option_name(i, v[i]));
            if (is) {
                int vw = gfx_str_w(FONT_BODY_SB, val), rx = lx + lw - 20;
                gfx_line(rx - 6.0f, cy - 7.0f, rx, (float)cy, 3, 0xFFFFFF, 255);
                gfx_line(rx - 6.0f, cy + 7.0f, rx, (float)cy, 3, 0xFFFFFF, 255);
                gfx_str_vc(FONT_BODY_SB, rx - 22 - vw, cy, 0xFFFFFF, val);
                gfx_line(rx - 40.0f - vw, cy - 7.0f, rx - 46.0f - vw, (float)cy, 3, 0xFFFFFF, 255);
                gfx_line(rx - 40.0f - vw, cy + 7.0f, rx - 46.0f - vw, (float)cy, 3, 0xFFFFFF, 255);
            } else {
                gfx_str_vc(FONT_BODY_SB, lx + lw - 20 - gfx_str_w(FONT_BODY_SB, val), cy, C_TEXT, val);
            }
        }
        ui_hints("A=Save;Y=Random;B=Cancel");
        ui_end_frame();
    }
}
