/* The XenithOS UI kit. See ui.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "avatar.h"
#include "config.h"
#include "gfx.h"
#include "icons.h"
#include "net.h"
#include "theme.h"
#include "ui.h"

struct pad cur, prev;
int M, HEADER_H;

static uint64_t next_frame, fade_start;
static int fading;

/* --------------------------------------------------------------- input */

void ui_init(void)
{
    M = SCR_W * 6 / 100;
    HEADER_H = 112;
}

uint64_t ui_ms(void)
{
    return plat_time_us() / 1000;
}

void ui_poll(void)
{
    audio_update();
    prev = cur;
    plat_poll(&cur);
}

static uint64_t frame_start;
static int frame_us_avg;

int ui_frame_ms(void)
{
    return (frame_us_avg + 500) / 1000;
}

static int nav(int neg, int pos, int *held, uint64_t *next)
{
    int d = neg ? -1 : pos ? 1 : 0;
    uint64_t now = ui_ms();
    if (!d) { *held = 0; return 0; }
    if (d != *held) { *held = d; *next = now + 380; return d; }
    if (now >= *next) { *next = now + 85; return d; }
    return 0;
}

int ui_nav_y(void)
{
    static int held;
    static uint64_t next;
    return nav(cur.up || cur.ly > STICK_TH, cur.down || cur.ly < -STICK_TH, &held, &next);
}

int ui_nav_x(void)
{
    static int held;
    static uint64_t next;
    return nav(cur.left || cur.lx < -STICK_TH, cur.right || cur.lx > STICK_TH, &held, &next);
}

/* How long the last frame really was, in 60 Hz frames. Easing uses it so animations take the
 * same time even when a frame runs long. */
static float frame_scale = 1.0f;

float ui_ease(float value, float target, float rate)
{
    float d = target - value;
    if (fabsf(d) < 0.01f) return target;
    if (frame_scale > 1.0f) rate = 1.0f - powf(1.0f - rate, frame_scale);
    return value + d * rate;
}

void ui_fade_in(void)
{
    fading = 1;
    fade_start = ui_ms();
}

/* --------------------------------------------------------------- toasts */

static char toast_title[64], toast_body[96];
static uint64_t toast_start;
static int toast_on, toast_icon = ICON_CHECK, next_icon = ICON_CHECK;

void ui_toast_icon(int icon)
{
    next_icon = icon;
}

void ui_toast(const char *title, const char *body)
{
    toast_icon = next_icon;
    next_icon = ICON_CHECK;
    snprintf(toast_title, sizeof toast_title, "%s", title);
    snprintf(toast_body, sizeof toast_body, "%s", body ? body : "");
    toast_start = ui_ms();
    toast_on = 1;
}

static void draw_toast(void)
{
    uint64_t t;
    int w = 420, h = 76, x, y, a = 255;
    float slide;
    if (!toast_on) return;
    t = ui_ms() - toast_start;
    if (t > 3400) { toast_on = 0; return; }
    slide = t < 250 ? 1.0f - t / 250.0f : 0.0f;
    if (t > 3000) a = (int)(255 * (3400 - t) / 400);
    slide = slide * slide;
    x = SCR_W - M - w + (int)(slide * (w + M));
    y = 28;
    gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 14, C_SURFACE2, a);
    gfx_circle(x + 38.0f, y + h / 2.0f, 20, C_ACCENT, a);
    if (a > 200) icon_draw(toast_icon, x + 38.0f, y + h / 2.0f, 22, 0xFFFFFF, C_ACCENT);
    gfx_str_a(FONT_BODY_SB, x + 72, y + 12, C_TEXT, a, toast_title);
    gfx_str_a(FONT_SMALL, x + 72, y + 42, C_SUB, a, toast_body);
}

/* ---------------------------------------------------------- frame pacing */

void ui_end_frame(void)
{
    uint64_t now;
    draw_toast();
    if (fading) {
        uint64_t t = ui_ms() - fade_start;
        if (t >= 160) fading = 0;
        else gfx_rect_a(0, 0, SCR_W, SCR_H, C_BG, (int)(255 * (160 - t) / 160));
    }
    gfx_present();
    audio_update();

    now = plat_time_us();
    if (frame_start) {                    /* how long the frame took before waiting, smoothed */
        int us = (int)(now - frame_start);
        frame_us_avg = frame_us_avg ? (frame_us_avg * 7 + us) / 8 : us;
    }
    if (!next_frame || now > next_frame + 100000) next_frame = now;   /* resync after a long stall */
    next_frame += 16667;
    if (next_frame > now) plat_sleep_us((int)(next_frame - now));
    audio_update();
    {
        static uint64_t last_end;
        uint64_t end = plat_time_us();
        if (last_end) {
            float k = (end - last_end) / 16667.0f;
            frame_scale = k < 1.05f ? 1.0f : k > 4.0f ? 4.0f : k;
        }
        last_end = end;
    }
    frame_start = plat_time_us();
}

/* --------------------------------------------------------------- pieces */

void ui_bg(void)
{
    gfx_clear(C_BG);
}

void ui_panel(int x, int y, int w, int h)
{
    gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 16, C_SURFACE, 255);
}

void ui_profile_chip(int right_x, int cy)
{
    const char *name = prof.signed_in ? prof.name : "Guest";
    int asz = 44, nx;
    uint32_t dot = net_is_up() ? C_OK : C_DIM;
    avatar_draw(prof.avatar, right_x - asz, cy - asz / 2, asz);
    nx = right_x - asz - 14 - gfx_str_w(FONT_BODY_SB, name);
    gfx_str(FONT_BODY_SB, nx, cy - gfx_line_h(FONT_BODY_SB) + 6, C_TEXT, name);
    gfx_circle((float)(right_x - asz - 14 - gfx_str_w(FONT_SMALL, net_is_up() ? "Online" : "Offline") - 12),
               cy + 13.0f, 4, dot, 255);
    gfx_str_r(FONT_SMALL, right_x - asz - 14, cy + 2, C_SUB, net_is_up() ? "Online" : "Offline");
}

void ui_header(const char *title, const char *subtitle)
{
    gfx_str(FONT_TITLE, M, 30, C_TEXT, title);
    if (subtitle) gfx_str(FONT_SMALL, M, 30 + gfx_line_h(FONT_TITLE) + 2, C_SUB, subtitle);
    ui_profile_chip(SCR_W - M, 54);
}

int ui_button_glyph(int x, int cy, const char *b)
{
    uint32_t c = 0;
    if (!strcmp(b, "A")) c = C_BTN_A;
    else if (!strcmp(b, "B")) c = C_BTN_B;
    else if (!strcmp(b, "X")) c = C_BTN_X;
    else if (!strcmp(b, "Y")) c = C_BTN_Y;
    if (c) {
        gfx_circle(x + 13.0f, (float)cy, 13, c, 255);
        gfx_str_c(FONT_SMALL, x + 13, cy - gfx_line_h(FONT_SMALL) / 2, RGB(16, 18, 24), b);
        return 26;
    } else {
        int w = gfx_str_w(FONT_SMALL, b) + 20;
        gfx_fill_rrect((float)x, cy - 13.0f, (float)w, 26, 13, C_SURFACE2, 255);
        gfx_str_c(FONT_SMALL, x + w / 2, cy - gfx_line_h(FONT_SMALL) / 2, C_SUB, b);
        return w;
    }
}

void ui_hints(const char *spec)
{
    char buf[200], *item, *save = NULL;
    int x = M, cy = SCR_H - 34;
    snprintf(buf, sizeof buf, "%s", spec);
    for (item = strtok_r(buf, ";", &save); item; item = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(item, '=');
        if (!eq) continue;
        *eq = 0;
        x += ui_button_glyph(x, cy, item) + 10;
        x += gfx_str(FONT_SMALL, x, cy - gfx_line_h(FONT_SMALL) / 2, C_SUB, eq + 1) + 30;
    }
}

void ui_spinner(int cx, int cy, int r, uint32_t color)
{
    float t = (float)(ui_ms() % 100000) / 1000.0f;
    float a0 = t * 5.2f, len = 1.6f + 1.1f * sinf(t * 2.4f);
    gfx_ring((float)cx, (float)cy, (float)r, 5, C_SURFACE2, 255);
    gfx_arc((float)cx, (float)cy, (float)r, 5, a0, a0 + len, color, 255);
}

/* ---------------------------------------------------- full-screen states */

void ui_loading_frame(const char *title, const char *detail)
{
    int cy = SCR_H / 2;
    ui_bg();
    ui_spinner(SCR_W / 2, cy - 44, 24, C_ACCENT);
    gfx_str_c(FONT_LABEL, SCR_W / 2, cy + 6, C_TEXT, title);
    if (detail) gfx_str_c(FONT_BODY, SCR_W / 2, cy + 46, C_SUB, detail);
    ui_end_frame();
}

static char busy_title[64], busy_detail[96];

static void busy_tick(void)
{
    ui_loading_frame(busy_title, busy_detail[0] ? busy_detail : NULL);
}

void ui_busy_begin(const char *title, const char *detail)
{
    snprintf(busy_title, sizeof busy_title, "%s", title);
    snprintf(busy_detail, sizeof busy_detail, "%s", detail ? detail : "");
    net_set_idle_hook(busy_tick);
    busy_tick();
}

void ui_busy_end(void)
{
    net_set_idle_hook(NULL);
}

void ui_progress_frame(const char *title, const char *detail, int done, int total)
{
    int w = SCR_W / 3, x = (SCR_W - w) / 2, y = SCR_H / 2 + 10;
    char pct[64];
    ui_bg();
    gfx_str_c(FONT_LABEL, SCR_W / 2, y - 70, C_TEXT, title);
    if (detail) gfx_str_c(FONT_BODY, SCR_W / 2, y - 34, C_SUB, detail);
    gfx_fill_rrect((float)x, (float)y, (float)w, 8, 4, C_SURFACE2, 255);
    if (total > 0) {
        float f = (float)done / total;
        if (f > 1) f = 1;
        if (f > 0) gfx_fill_rrect((float)x, (float)y, w * f, 8, 4, C_ACCENT, 255);
        if (total >= 1048576)
            snprintf(pct, sizeof pct, "%.1f of %.1f MB", done / 1048576.0, total / 1048576.0);
        else
            snprintf(pct, sizeof pct, "%d of %d KB", done / 1024, total / 1024);
    } else {
        snprintf(pct, sizeof pct, "%d KB", done / 1024);
    }
    gfx_str_c(FONT_SMALL, SCR_W / 2, y + 22, C_SUB, pct);
    ui_end_frame();
}

/* ----------------------------------------------------------------- dialogs */

int ui_dialog(const char *title, const char *body, const char *const *options, int n)
{
    size_t bytes = (size_t)SCR_W * SCR_H * 4;
    uint32_t *under = malloc(bytes), *dimmed = NULL;
    int sel = 0, w = SCR_W * 46 / 100, row = 54, x, y, h, body_lines = 0, result = -1, i;
    float hl;

    if (under) {
        memcpy(under, gfx_bb, bytes);
        dimmed = malloc(bytes);                /* darken the screen behind once, not every frame */
        if (dimmed) {
            gfx_rect_a(0, 0, SCR_W, SCR_H, RGB(0, 0, 0), 150);
            memcpy(dimmed, gfx_bb, bytes);
        }
    }
    if (w < 520) w = 520;
    if (body) {
        /* Measure the body by drawing it off to the side of a scratch clip. */
        int clip[4];
        gfx_get_clip(clip);
        gfx_set_clip(0, 0, 0, 0);
        body_lines = gfx_wrap(FONT_BODY, 0, 0, w - 64, 4, 0, body, 6);
        gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
    }
    h = 32 + gfx_line_h(FONT_TITLE) + 10 + body_lines * (gfx_line_h(FONT_BODY) + 4) + 22 + n * row + 24;
    x = (SCR_W - w) / 2;
    y = (SCR_H - h) / 2;
    hl = (float)sel;

    for (;;) {
        int d, oy;
        ui_poll();
        d = ui_nav_y();
        if (d) {
            int nsel = sel + d;
            if (nsel >= 0 && nsel < n) { sel = nsel; sfx(SFX_MOVE); }
        }
        if (PRESSED(a)) { sfx(SFX_SELECT); result = sel; break; }
        if (PRESSED(b)) { sfx(SFX_BACK); result = -1; break; }

        if (dimmed) memcpy(gfx_bb, dimmed, bytes);
        else {
            if (under) memcpy(gfx_bb, under, bytes);
            gfx_rect_a(0, 0, SCR_W, SCR_H, RGB(0, 0, 0), 150);
        }
        gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 18, C_SURFACE, 255);
        gfx_str(FONT_TITLE, x + 32, y + 26, C_TEXT, title);
        oy = y + 32 + gfx_line_h(FONT_TITLE) + 10;
        if (body) gfx_wrap(FONT_BODY, x + 32, oy, w - 64, 4, C_SUB, body, 6);
        oy += body_lines * (gfx_line_h(FONT_BODY) + 4) + 22;
        hl = ui_ease(hl, (float)sel, 0.45f);
        gfx_fill_rrect(x + 20.0f, oy + hl * row, w - 40.0f, row - 8.0f, 10, C_ACCENT, 255);
        for (i = 0; i < n; i++)
            gfx_str_vc(FONT_BODY_SB, x + 40, oy + i * row + (row - 8) / 2, i == sel ? 0xFFFFFF : C_TEXT, options[i]);
        ui_end_frame();
    }
    if (under) {
        memcpy(gfx_bb, under, bytes);
        free(under);
    }
    free(dimmed);
    do { ui_poll(); } while (cur.a || cur.b);
    return result;
}

void ui_message(const char *title, const char *body)
{
    static const char *const ok[] = {"OK"};
    ui_dialog(title, body, ok, 1);
}

int ui_confirm(const char *title, const char *body, const char *yes, const char *no)
{
    const char *opts[2];
    opts[0] = yes;
    opts[1] = no;
    return ui_dialog(title, body, opts, 2) == 0;
}
