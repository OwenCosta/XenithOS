/* Avatar editor: three tabs of parts (LB / RB), up and down to pick a part, left and right to
 * change it, Y for a random look. Colour parts show every colour as a swatch. */
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "avatar.h"
#include "gfx.h"
#include "platform.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

static const char *const tab_name[3] = {"Face", "Hair", "Outfit"};
static const int tab_parts[3][7] = {
    {AV_SKIN, AV_EYES, AV_EYE_COLOR, AV_BROWS, AV_MOUTH, AV_CHEEKS, -1},
    {AV_HAIR, AV_HAIR_COLOR, AV_FACIAL_HAIR, -1},
    {AV_TOP, AV_TOP_COLOR, AV_ACCESSORY, AV_BACKGROUND, -1},
};

static int tab_len(int t)
{
    int n = 0;
    while (n < 7 && tab_parts[t][n] >= 0) n++;
    return n;
}

static void arrows(int x0, int x1, int cy, uint32_t c)
{
    gfx_line(x0 + 6.0f, cy - 7.0f, (float)x0, (float)cy, 3, c, 255);
    gfx_line(x0 + 6.0f, cy + 7.0f, (float)x0, (float)cy, 3, c, 255);
    gfx_line(x1 - 6.0f, cy - 7.0f, (float)x1, (float)cy, 3, c, 255);
    gfx_line(x1 - 6.0f, cy + 7.0f, (float)x1, (float)cy, 3, c, 255);
}

/* Every colour of a part as a dot, the chosen one ringed. Right-aligned at rx. */
static void swatches(int part, int value, int rx, int cy, int max_w, int on_accent)
{
    int n = avatar_count[part], step = 26, i, x;
    if (n * step > max_w) step = max_w / n;
    x = rx - n * step + step / 2;
    for (i = 0; i < n; i++, x += step) {
        uint32_t c = avatar_option_color(part, i);
        if (i == value) gfx_ring((float)x, (float)cy, 11, 2.5f, on_accent ? 0xFFFFFF : C_ACCENT_HI, 255);
        gfx_circle((float)x, (float)cy, i == value ? 7.5f : 8, c, 255);
    }
}

int avatar_editor(char *code)
{
    int v[AV_PARTS], tab = 0, sel = 0, i;
    float hl = 0, pop = 0, tab_hl = 0;
    char start[AVATAR_CODE_LEN + 1];

    avatar_parse(code, v);
    avatar_encode(v, start);
    ui_fade_in();

    for (;;) {
        int top = HEADER_H + 16, size = SCR_H - top - 110, rowh = 56, lx, lw, dx, dy, n, part, rows_y, tw;
        if (size > 420) size = 420;
        lx = M + size + 56;
        lw = SCR_W - M - lx;
        n = tab_len(tab);
        rows_y = top + 76;

        ui_poll();
        dy = ui_nav_y();
        dx = ui_nav_x();
        if (PRESSED(lb) || PRESSED(rb)) {
            tab = (tab + (PRESSED(rb) ? 1 : 2)) % 3;
            sel = 0;
            hl = 0;
            n = tab_len(tab);
            sfx(SFX_MOVE);
        }
        if (dy && sel + dy >= 0 && sel + dy < n) { sel += dy; sfx(SFX_MOVE); }
        part = tab_parts[tab][sel];
        if (dx) {
            v[part] = (v[part] + dx + avatar_count[part]) % avatar_count[part];
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
        ui_header("Avatar", "Make it yours. LB and RB switch between face, hair and outfit.");
        pop = ui_ease(pop, 0, 0.25f);
        avatar_draw_v(v, M, top + 10, size);                 /* fixed size, so it stays cached */
        if (pop > 0.02f) {                                   /* a ring flashes out on each change */
            float g = (1 - pop) * 12;
            gfx_stroke_rrect(M - 4 - g, top + 6 - g, size + 8 + 2 * g, size + 8 + 2 * g, size * 0.22f + 4 + g, 3, 0xFFFFFF, (int)(200 * pop));
        }

        /* Tabs */
        tw = (lw - 2 * 10) / 3;
        tab_hl = ui_ease(tab_hl, (float)tab, 0.4f);
        gfx_fill_rrect((float)lx, top + 10.0f, (float)lw, 48, 24, C_SURFACE, 255);
        gfx_fill_rrect(lx + 4 + tab_hl * (tw + 10), top + 14.0f, tw - 8.0f + 10, 40, 20, C_ACCENT, 255);
        for (i = 0; i < 3; i++)
            gfx_str_c(FONT_BODY_SB, lx + 4 + i * (tw + 10) + (tw + 2) / 2, top + 21, i == tab ? 0xFFFFFF : C_SUB, tab_name[i]);

        /* Parts in this tab */
        hl = ui_ease(hl, (float)sel, 0.45f);
        gfx_fill_rrect((float)lx, rows_y + hl * rowh, (float)lw, rowh - 6.0f, 12, C_ACCENT, 255);
        for (i = 0; i < n; i++) {
            int p = tab_parts[tab][i], y = rows_y + i * rowh, cy = y + (rowh - 6) / 2, is = i == sel;
            int rx = lx + lw - 20;
            gfx_str_vc(FONT_BODY_SB, lx + 20, cy, is ? 0xFFFFFF : C_SUB, avatar_part_name[p]);
            if (avatar_part_is_color(p)) {
                swatches(p, v[p], rx, cy, lw - 210, is);
            } else {
                char val[48], count[16];
                int vw;
                snprintf(val, sizeof val, "%s", avatar_option_name(p, v[p]));
                snprintf(count, sizeof count, "%d/%d", v[p] + 1, avatar_count[p]);
                vw = gfx_str_w(FONT_BODY_SB, val);
                if (is) {
                    arrows(rx - 46 - vw, rx, cy, 0xFFFFFF);
                    gfx_str_vc(FONT_BODY_SB, rx - 22 - vw, cy, 0xFFFFFF, val);
                    gfx_str_vc(FONT_SMALL, rx - 64 - vw - gfx_str_w(FONT_SMALL, count), cy, 0xDDE8FF, count);
                } else {
                    gfx_str_vc(FONT_BODY_SB, rx - vw, cy, C_TEXT, val);
                }
            }
        }
        ui_hints("A=Save;Y=Random;LB/RB=Face, hair, outfit;B=Cancel");
        ui_end_frame();
    }
}
