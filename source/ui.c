#include <stdio.h>
#include <string.h>
#include <usb/usbmain.h>
#include <time/time.h>

#include "gfx.h"
#include "theme.h"
#include "ui.h"

int S, M, TOP;
struct controller_data_s cur, prev;

void ui_init(void)
{
    S = SCR_H >= 600 ? 3 : 2;
    M = SCR_W / 16;
    TOP = M / 2 + GFX_TEXT_H(S) + S * 12;
}

void poll_input(void)
{
    prev = cur;
    usb_do_poll();
    get_controller_data(&cur, 0);
    mdelay(16);
}

int nav_y(void)
{
    static int held, frames;
    int d = 0;

    if (cur.up || cur.s1_y > STICK_TH) d = -1;
    else if (cur.down || cur.s1_y < -STICK_TH) d = 1;

    if (!d) { held = 0; frames = 0; return 0; }
    if (d != held) { held = d; frames = 0; return d; }
    frames++;
    if (frames > 20 && (frames - 20) % 6 == 0) return d;
    return 0;
}

void wait_release(void)
{
    do { poll_input(); } while (cur.a || cur.b || cur.x || cur.y || cur.start || cur.back);
}

void draw_chrome(const char *title, const char *hint)
{
    int y = M / 2;
    gfx_clear(C_BG);
    int x = M + gfx_text(M, y, S, C_ACCENT, "XENITH");
    if (title) {
        x += S * 6;
        x += gfx_text(x, y, S, C_DIM, "/") + S * 6;
        gfx_text(x, y, S, C_TEXT, title);
    }
    gfx_rect(M, y + GFX_TEXT_H(S) + S * 5, SCR_W - 2 * M, 2, C_PANEL);
    if (hint)
        gfx_text(M, SCR_H - M / 2 - GFX_TEXT_H(S - 1), S - 1, C_DIM, hint);
}

void draw_row(int y, int h, int selected, int scale, const char *label, const char *sub)
{
    int max_w = SCR_W - 2 * M - S * 16;
    gfx_rect(M, y, SCR_W - 2 * M, h, selected ? C_PANEL : C_BG);
    if (selected) gfx_rect(M, y, S * 2, h, C_ACCENT);
    int ty = y + (h - GFX_TEXT_H(scale)) / 2;
    int w = gfx_text_fit(M + S * 8, ty, scale, selected ? C_TEXT : C_DIM, label, max_w);
    if (sub && w + S * 8 < max_w)
        gfx_text_fit(M + S * 8 + w + S * 8, y + (h - GFX_TEXT_H(S - 1)) / 2, S - 1, C_DIM, sub,
                     max_w - w - S * 8);
}

static void panel_box(int *x, int *y, int *w, int *h)
{
    *w = SCR_W * 2 / 3;
    *h = GFX_TEXT_H(S + 1) + GFX_TEXT_H(S - 1) + S * 22;
    *x = (SCR_W - *w) / 2;
    *y = (SCR_H - *h) / 2;
}

void ui_panel(const char *big, const char *small, uint32_t big_color)
{
    int x, y, w, h;
    panel_box(&x, &y, &w, &h);
    gfx_rect(x, y, w, h, C_PANEL);
    gfx_rect(x, y, w, S, C_ACCENT);
    gfx_text_c(SCR_W / 2, y + S * 8, S + 1, big_color, big);
    if (small)
        gfx_text_c(SCR_W / 2, y + h - S * 7 - GFX_TEXT_H(S - 1), S - 1, C_DIM, small);
    gfx_flush();
}

void ui_progress(const char *label, int done, int total)
{
    int x, y, w, h, bx, bw, by;
    char pct[64];
    panel_box(&x, &y, &w, &h);
    bx = x + S * 8;
    bw = w - S * 16;
    by = y + h - S * 7 - GFX_TEXT_H(S - 1) - S * 6;

    gfx_rect(x, y, w, h, C_PANEL);
    gfx_rect(x, y, w, S, C_ACCENT);
    gfx_text_c(SCR_W / 2, y + S * 8, S + 1, C_TEXT, label);
    gfx_rect(bx, by, bw, S * 2, C_BG);
    if (total > 0) {
        long long fill = (long long)bw * done / total;
        gfx_rect(bx, by, (int)fill, S * 2, C_ACCENT);
        snprintf(pct, sizeof pct, "%d%%   %d / %d KB", (int)(100LL * done / total), done / 1024, total / 1024);
    } else {
        snprintf(pct, sizeof pct, "%d KB", done / 1024);
    }
    gfx_text_c(SCR_W / 2, y + h - S * 7 - GFX_TEXT_H(S - 1), S - 1, C_DIM, pct);
    gfx_flush();
}

void ui_alert(const char *big, const char *small, uint32_t big_color)
{
    ui_panel(big, small, big_color);
    wait_release();
    for (;;) {
        poll_input();
        if (PRESSED(a) || PRESSED(b)) break;
    }
    wait_release();
}

int confirm(const char *question)
{
    ui_panel(question, "A YES     B NO", C_TEXT);
    wait_release();
    for (;;) {
        poll_input();
        if (PRESSED(a)) return 1;
        if (PRESSED(b)) return 0;
    }
}
