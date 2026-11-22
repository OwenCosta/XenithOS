/* SETTINGS > THEME: the colour theme (all of XenithOS and its apps) and the home screen's
 * wallpaper. Moving through either row puts it on straight away, so the screen itself is the
 * preview: its colours change and the wallpaper is behind it. Leaving saves it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "config.h"
#include "gfx.h"
#include "img.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"
#include "wallpaper.h"

#define GAP 16

/* The wallpapers' cards, drawn once for the colours in use. */
static struct img previews[WALL_COUNT];
static struct xenith_palette previews_pal;
static int previews_w;

static void make_previews(int w, int h)
{
    int i;
    if (previews[0].px && previews_w == w && !memcmp(&previews_pal, &pal, sizeof pal)) return;
    for (i = 0; i < WALL_COUNT; i++) {
        uint32_t *px = previews[i].px && previews_w == w ? previews[i].px : malloc((size_t)w * h * 4);
        if (px != previews[i].px) free(previews[i].px);
        previews[i].px = px;
        previews[i].w = w;
        previews[i].h = h;
        previews[i].id = gfx_new_image_id();      /* drawn again: not what the image cache has */
        if (!px || !gfx_target(px, w, h)) continue;
        wallpaper_draw(i, 0, 0, w, h);
        gfx_target(NULL, 0, 0);
        {
            int k;                                /* opaque, for gfx_image */
            for (k = 0; k < w * h; k++) px[k] |= 0xFF000000u;
        }
    }
    previews_w = w;
    previews_pal = pal;
}

static void free_previews(void)
{
    int i;
    for (i = 0; i < WALL_COUNT; i++) img_free(&previews[i]);
}

/* A colour theme's card: its background, one of its cards with the accent on it, its name. */
static void theme_card(int t, int x, int y, int w, int h)
{
    struct xenith_palette p;
    int cw = w * 62 / 100, ch = h * 52 / 100, cx = x + (w - cw) / 2, cy = y + h * 18 / 100;
    theme_colors(t, &p);
    gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 10, p.bg, 255);
    gfx_stroke_rrect((float)x, (float)y, (float)w, (float)h, 10, 1, p.line, 255);
    gfx_fill_rrect((float)cx, (float)cy, (float)cw, (float)ch, 7, p.surface, 255);
    gfx_stroke_rrect((float)cx, (float)cy, (float)cw, (float)ch, 7, 1, p.line, 255);
    gfx_fill_rrect(cx + 10.0f, cy + ch - 16.0f, cw * 0.55f, 6, 3, p.accent, 255);
    gfx_circle(cx + cw - 18.0f, cy + 16.0f, 7, p.accent, 255);
}

void screen_theme(void)
{
    int row = 0, sel[2], changed = 0, i;
    float lift[2][8];
    memset(lift, 0, sizeof lift);
    sel[0] = theme_current();
    sel[1] = wallpaper_find(st.wallpaper);
    ui_fade_in();
    for (;;) {
        int n = row == 0 ? THEME_COUNT : WALL_COUNT, cols = 6, d, dx;
        int w = (SCR_W - 2 * M - GAP * (cols - 1)) / cols, th = w * 62 / 100, wh = w * 9 / 16;
        int top = HEADER_H + 24, y0 = top + 40, y1 = y0 + th + gfx_line_h(FONT_BODY_SB) + 70;

        ui_poll();
        d = ui_nav_y();
        dx = ui_nav_x();
        if (d && row + d >= 0 && row + d <= 1) { row += d; sfx(SFX_MOVE); }
        if (dx && sel[row] + dx >= 0 && sel[row] + dx < n) {
            sel[row] += dx;
            sfx(SFX_MOVE);
            changed = 1;
            if (row == 0) {
                theme_apply(sel[0]);
                snprintf(st.theme, sizeof st.theme, "%s", theme_id(sel[0]));
            } else {
                snprintf(st.wallpaper, sizeof st.wallpaper, "%s", wallpaper_id(sel[1]));
            }
        }
        if (PRESSED(b) || PRESSED(a)) {
            if (changed) settings_save();
            sfx(PRESSED(a) ? SFX_SELECT : SFX_BACK);
            free_previews();
            return;
        }
        make_previews(w, wh);

        wallpaper_bg(sel[1]);
        ui_header("Theme", "Colours for XenithOS and its apps, and a wallpaper for the home screen");
        for (i = 0; i < 2; i++) {
            int k, cnt = i == 0 ? THEME_COUNT : WALL_COUNT, y = i == 0 ? y0 : y1, h = i == 0 ? th : wh;
            gfx_str(FONT_BODY_SB, M, y - 36, row == i ? C_TEXT : C_SUB, i == 0 ? "Colour" : "Wallpaper");
            for (k = 0; k < cnt && k < cols; k++) {
                int x = M + k * (w + GAP), cur = sel[i] == k, focus = row == i && cur;
                float l = lift[i][k] = ui_ease(lift[i][k], focus ? 1.0f : 0.0f, 0.35f), g = (1 - l) * 6;
                int ly = y - (int)(l * 6);
                if (i == 0) theme_card(k, x, ly, w, h);
                else if (previews[k].px) gfx_image_rounded(&previews[k], x, ly, w, h, 10, 255);
                if (i == 1) gfx_stroke_rrect((float)x, (float)ly, (float)w, (float)h, 10, 1, C_LINE, 255);
                if (focus) gfx_stroke_rrect(x - 5 - g, ly - 5 - g, w + 10 + 2 * g, h + 10 + 2 * g, 15 + g, 3, 0xFFFFFF, (int)(255 * l));
                else if (cur) gfx_stroke_rrect(x - 4.0f, ly - 4.0f, w + 8.0f, h + 8.0f, 14, 2, C_ACCENT, 255);
                gfx_str_fit(FONT_SMALL, x + 2, ly + h + 10, cur ? C_TEXT : C_SUB,
                            i == 0 ? theme_name(k) : wallpaper_name(k), w - 4);
            }
        }
        ui_hints("LEFT/RIGHT=Choose;UP/DOWN=Colour or wallpaper;B=Done");
        ui_end_frame();
    }
}
