/* The XenithOS UI kit. See ui.h. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "avatar.h"
#include "config.h"
#include "gfx.h"
#include "handoff.h"
#include "icons.h"
#include "net.h"
#include "platform.h"
#include "theme.h"
#include "ui.h"
#include "logo_shape.h"

struct pad cur, prev, ui_raw;
int M, HEADER_H;
int ui_desktop;
struct plat_mouse mouse = {-1, -1, 0, 0, 0, 0, 0};
int ui_keys_on, ui_ctrl;
static int keys_down[16], nkeys_down;           /* this frame's presses, with ui_keys_on */

static uint64_t next_frame, fade_start;
static int fading;

/* --------------------------------------------------------------- input */

/* The look (theme.h, and the text size) goes along to the next program as "theme.contrast.text",
 * so the frames it draws before it has read the settings look the same, and on the Xbox the
 * switch still doesn't show. */
static void look_give(void)
{
    char look[40];
    snprintf(look, sizeof look, "%s.%d.%d", theme_id(theme_current()), theme_contrast(), gfx_text_large());
    handoff_set("look", look);
}

static void look_take(void)
{
    const char *l = handoff_get("look");
    char id[24];
    int c = 0, t = 0;
    if (!l || sscanf(l, "%23[a-z0-9].%d.%d", id, &c, &t) < 1) return;
    theme_set_contrast(c);
    theme_apply(theme_find(id));
    gfx_text_size(t);
}

/* -------------------------------------------------------- moved buttons */

typedef char pad_map_fits[sizeof st.pad_map == PADB_COUNT ? 1 : -1];

static const char *const pad_names[PADB_COUNT] = {
    "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "START", "BACK", "LS", "RS",
};

const char *ui_pad_name(int b)
{
    return b >= 0 && b < PADB_COUNT ? pad_names[b] : "";
}

int ui_pad_find(const char *name)
{
    int b;
    for (b = 0; name && b < PADB_COUNT; b++)
        if (!strcmp(name, pad_names[b])) return b;
    return -1;
}

int ui_pad_from(int job)
{
    int m;
    if (job < 0 || job >= PADB_COUNT) return job;
    m = st.pad_map[job];
    return m > 0 && m <= PADB_COUNT ? m - 1 : job;
}

/* A button's state as 0..255, so a trigger and a button can stand in for each other. */
static int pad_get(const struct pad *p, int b)
{
    switch (b) {
    case PADB_A: return p->a ? 255 : 0;
    case PADB_B: return p->b ? 255 : 0;
    case PADB_X: return p->x ? 255 : 0;
    case PADB_Y: return p->y ? 255 : 0;
    case PADB_LB: return p->lb ? 255 : 0;
    case PADB_RB: return p->rb ? 255 : 0;
    case PADB_LT: return p->lt;
    case PADB_RT: return p->rt;
    case PADB_START: return p->start ? 255 : 0;
    case PADB_BACK: return p->back ? 255 : 0;
    case PADB_LS: return p->ls ? 255 : 0;
    case PADB_RS: return p->rs ? 255 : 0;
    }
    return 0;
}

static void pad_set(struct pad *p, int b, int v)
{
    int on = v >= 128;
    switch (b) {
    case PADB_A: p->a = on; break;
    case PADB_B: p->b = on; break;
    case PADB_X: p->x = on; break;
    case PADB_Y: p->y = on; break;
    case PADB_LB: p->lb = on; break;
    case PADB_RB: p->rb = on; break;
    case PADB_LT: p->lt = v; break;
    case PADB_RT: p->rt = v; break;
    case PADB_START: p->start = on; break;
    case PADB_BACK: p->back = on; break;
    case PADB_LS: p->ls = on; break;
    case PADB_RS: p->rs = on; break;
    }
}

int ui_pad_held(const struct pad *p, int b)
{
    return pad_get(p, b) >= 128;
}

void ui_pad_apply(struct pad *p)
{
    struct pad in = *p;
    int b;
    for (b = 0; b < PADB_COUNT; b++)
        if (ui_pad_from(b) != b) pad_set(p, b, pad_get(&in, ui_pad_from(b)));
    if (st.swap_sticks) {
        p->lx = in.rx; p->ly = in.ry;
        p->rx = in.lx; p->ry = in.ly;
    }
}

const char *ui_pad_say(const char *job)
{
    int b = ui_pad_find(job);
    return b < 0 || ui_desktop ? job : pad_names[ui_pad_from(b)];
}

/* "a:b,lt:rb": A's job is done by B, LT's by RB. Only the moved ones are written. */
void ui_pad_map_parse(const char *text, signed char map[PADB_COUNT])
{
    char buf[96], *item, *save = NULL;
    int b;
    memset(map, 0, PADB_COUNT);
    snprintf(buf, sizeof buf, "%s", text ? text : "");
    for (item = strtok_r(buf, ",", &save); item; item = strtok_r(NULL, ",", &save)) {
        char *colon = strchr(item, ':'), *k;
        int job, from;
        if (!colon) continue;
        *colon = 0;
        for (k = item; *k; k++) *k = (char)toupper((unsigned char)*k);
        for (k = colon + 1; *k; k++) *k = (char)toupper((unsigned char)*k);
        job = ui_pad_find(item);
        from = ui_pad_find(colon + 1);
        if (job >= 0 && from >= 0 && from != job) map[job] = (signed char)(from + 1);
    }
    /* Only whole swaps make sense (every job still has a button): anything else goes back. */
    for (b = 0; b < PADB_COUNT; b++) {
        int from = map[b] ? map[b] - 1 : b, k, used = 0;
        for (k = 0; k < PADB_COUNT; k++)
            if ((map[k] ? map[k] - 1 : k) == from) used++;
        if (used != 1) { memset(map, 0, PADB_COUNT); return; }
    }
}

void ui_pad_map_text(const signed char map[PADB_COUNT], char *out, int n)
{
    int b, o = 0;
    out[0] = 0;
    for (b = 0; b < PADB_COUNT && o < n; b++) {
        char lower[2][8];
        int k;
        if (!map[b] || map[b] - 1 == b || map[b] > PADB_COUNT) continue;
        for (k = 0; k < 2; k++) {
            const char *s = pad_names[k ? map[b] - 1 : b];
            int i;
            for (i = 0; s[i] && i < 7; i++) lower[k][i] = (char)tolower((unsigned char)s[i]);
            lower[k][i] = 0;
        }
        o += snprintf(out + o, n - o, "%s%s:%s", o ? "," : "", lower[0], lower[1]);
    }
}

void ui_init(void)
{
    M = SCR_W * 6 / 100;
    HEADER_H = 112;
    ui_desktop = plat_desktop();
    look_take();
}

int ui_mouse_in(int x, int y, int w, int h)
{
    return mouse.x >= x && mouse.y >= y && mouse.x < x + w && mouse.y < y + h;
}

int ui_clicked(int x, int y, int w, int h)
{
    return (mouse.clicks & 1) && ui_mouse_in(x, y, w, h);
}

#define WITH_CTRL 0x10000                       /* a press made while Ctrl was held */

int ui_key(int key)
{
    int i;
    for (i = 0; i < nkeys_down; i++)
        if ((keys_down[i] & ~WITH_CTRL) == key) return keys_down[i] & WITH_CTRL ? 2 : 1;
    return 0;
}

/* Ctrl is noted with each press, since Ctrl's own press and release can come in the same frame
 * as the key's (a quick Ctrl+V, or one typed by a program). */
static void take_keys(void)
{
    int key, down;
    nkeys_down = 0;
    if (!ui_keys_on) return;
    while (plat_key_event(&key, &down)) {
        if (key == PLAT_KEY_CTRL) ui_ctrl = down;
        else if (down && nkeys_down < (int)(sizeof keys_down / sizeof keys_down[0]))
            keys_down[nkeys_down++] = key | (ui_ctrl ? WITH_CTRL : 0);
    }
}

uint64_t ui_ms(void)
{
    return plat_time_us() / 1000;
}

/* Key repeat for ui_nav_x/ui_nav_y. */
static int held_x, held_y;
static uint64_t next_x, next_y;

/* Set for a program's first frame and when XenithOS gets the controller back from an app:
 * whatever is held then was pressed for the other program (the A that started an app, the
 * BACK held to leave DOOM), so it only counts once it has been let go and pressed again. */
static int settle = 1;

void ui_settle_input(void)
{
    settle = 1;
}

void ui_poll(void)
{
    audio_update();
    prev = cur;
    plat_poll(&cur);
    ui_raw = cur;
    if (!ui_desktop) ui_pad_apply(&cur);
    plat_mouse(&mouse);
    take_keys();
    if (settle) {
        settle = 0;
        prev = cur;
        held_y = cur.up || cur.ly > STICK_TH ? -1 : cur.down || cur.ly < -STICK_TH ? 1 : 0;
        held_x = cur.left || cur.lx < -STICK_TH ? -1 : cur.right || cur.lx > STICK_TH ? 1 : 0;
        next_x = next_y = ui_ms() + 380;
    }
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
    return nav(cur.up || cur.ly > STICK_TH, cur.down || cur.ly < -STICK_TH, &held_y, &next_y);
}

int ui_nav_x(void)
{
    return nav(cur.left || cur.lx < -STICK_TH, cur.right || cur.lx > STICK_TH, &held_x, &next_x);
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

static char toast_title[64], toast_body[240];   /* long enough that the wrap's "..." does the cutting */
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

/* As wide as its words need, from 420 up to half the screen. Past that the words wrap onto a
 * second line and a long title ends in "...", so nothing runs off the edge of the screen. */
static void draw_toast(void)
{
    uint64_t t;
    int w, h = 76, x, y, a = 255, tw, need, lines = 0, clip[4];
    float slide;
    if (!toast_on) return;
    t = ui_ms() - toast_start;
    if (t > 3400) { toast_on = 0; return; }
    slide = t < 250 ? 1.0f - t / 250.0f : 0.0f;
    if (t > 3000) a = (int)(255 * (3400 - t) / 400);
    slide = slide * slide;
    need = gfx_str_w(FONT_BODY_SB, toast_title);
    if (gfx_str_w(FONT_SMALL, toast_body) > need) need = gfx_str_w(FONT_SMALL, toast_body);
    w = need + 72 + 24;
    if (w < 420) w = 420;
    if (w > SCR_W / 2) w = SCR_W / 2;
    tw = w - 72 - 20;
    if (toast_body[0]) {
        gfx_get_clip(clip);
        gfx_set_clip(0, 0, 0, 0);                    /* measure: nothing is drawn */
        lines = gfx_wrap(FONT_SMALL, 0, 0, tw, 2, 0, toast_body, 2);
        gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
    }
    if (lines > 1) h += gfx_line_h(FONT_SMALL) + 2;
    x = SCR_W - M - w + (int)(slide * (w + M));
    y = 28;
    gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 14, C_SURFACE, a);
    gfx_stroke_rrect((float)x, (float)y, (float)w, (float)h, 14, 1, C_LINE, a);
    gfx_circle(x + 38.0f, y + h / 2.0f, 20, C_ACCENT, a);
    if (a > 200) icon_draw(toast_icon, x + 38.0f, y + h / 2.0f, 22, 0xFFFFFF, C_ACCENT);
    gfx_str_fit_a(FONT_BODY_SB, x + 72, y + 12, C_TEXT, a, toast_title, tw);
    gfx_wrap_a(FONT_SMALL, x + 72, y + 42, tw, 2, C_SUB, a, toast_body, 2);
}

/* ---------------------------------------------------------- frame pacing */

static void (*frame_hook)(void);

void ui_set_frame_hook(void (*fn)(void))
{
    frame_hook = fn;
}

void ui_end_frame(void)
{
    uint64_t now;
    if (frame_hook) frame_hook();
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
    gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 14, C_SURFACE, 255);
    gfx_stroke_rrect((float)x, (float)y, (float)w, (float)h, 14, 1, C_LINE, 255);
}

/* Selected, the way the streaming site's cards look under the pointer: a little lighter, with
 * a white edge, instead of a colour fill. */
void ui_select_box(float x, float y, float w, float h, float r)
{
    gfx_fill_rrect(x, y, w, h, r, C_SEL, 255);
    gfx_stroke_rrect(x, y, w, h, r, (float)UI_EDGE, 0xFFFFFF, theme_contrast() ? 255 : 235);
}

/* The XenithOS mark. Two chevrons, one pointing down onto the middle and one pointing up at it,
 * make an X that doesn't quite meet: the red point is where it would (the zenith). Drawn from
 * round-capped strokes, so it's crisp at any size. */
/* How far angle a is from the nearest of 3, 6, 9 and 12 o'clock, where the ticks are. */
static float off_tick(float a)
{
    const float Q = 1.5707963f;
    return fabsf(a - Q * floorf(a / Q + 0.5f));
}

void ui_logo(float cx, float cy, float size, float build, float spin, int alpha)
{
    float R = size / (2 * LOGO_HALF);
    int i;
    if (build > 1) build = 1;
    if (spin > 1) spin = 1;
    /* the outer ring: four streaks spin in from far out, each a little after the one before,
     * thinning as they close, and lock into place */
    for (i = 0; spin > 0 && i < 4; i++) {
        const struct logo_arc *k = &logo_ring[i];
        float p = (spin * 980.0f - i * 70.0f) / (980.0f - i * 70.0f), e, len, end;
        if (p <= 0) continue;
        if (p > 1) p = 1;
        e = p * p;
        len = (k->a1 - k->a0) * (0.31f + 0.69f * p);
        end = k->a1 - (1 - p) * 6.6f;
        gfx_arc(cx, cy, R * (k->r + 2.3f * (1 - e)), R * (k->w + 0.046f * (1 - e)), end - len, end,
                k->red ? C_BRAND : C_TEXT, (int)(alpha * (p < 0.15f ? p / 0.15f : 1)));
    }
    if (build > 0) {
        /* the double ring: each piece sweeps round from its end nearest 3, 6, 9 or 12 o'clock */
        for (i = 0; i < (int)(sizeof logo_arcs / sizeof logo_arcs[0]); i++) {
            const struct logo_arc *k = &logo_arcs[i];
            float len = (k->a1 - k->a0) * build;
            uint32_t c = k->red ? C_BRAND : LOGO_GREY;
            if (off_tick(k->a0) <= off_tick(k->a1)) gfx_sector(cx, cy, k->r * R, k->w * R, k->a0, k->a0 + len, c, alpha);
            else gfx_sector(cx, cy, k->r * R, k->w * R, k->a1 - len, k->a1, c, alpha);
        }
        /* the X: its pieces come in from outside, the way each one points, and stop a little apart */
        for (i = 0; i < 3; i++) {
            const struct logo_poly *p = &logo_x[i];
            float d = sqrtf(p->ox * p->ox + p->oy * p->oy), o = (1 - build) * 0.7f / d;
            gfx_poly(p->xy, p->n, cx + p->ox * (1 + o) * R, cy + p->oy * (1 + o) * R, R,
                     p->red ? C_BRAND : LOGO_GREY, (int)(alpha * build));
        }
    }
}

void ui_mark(float cx, float cy, float size, int alpha)
{
    float R = size / (LOGO_X_BOTTOM - LOGO_X_TOP);
    float ox = cx - (LOGO_X_LEFT + LOGO_X_RIGHT) / 2 * R, oy = cy - (LOGO_X_TOP + LOGO_X_BOTTOM) / 2 * R;
    int i;
    for (i = 0; i < 3; i++)
        gfx_poly(logo_x[i].xy, logo_x[i].n, ox + logo_x[i].ox * R, oy + logo_x[i].oy * R, R,
                 logo_x[i].red ? C_BRAND : LOGO_GREY_SMALL, alpha);
}

/* The wordmark: the X, then the name in bold. Returns its width; measure_only draws nothing. */
int ui_brand(int x, int y, int font, int measure_only)
{
    int lh = gfx_cap_h(font), gap = lh / 2;
    float mh = lh * 1.4f, mw = mh * (LOGO_X_RIGHT - LOGO_X_LEFT) / (LOGO_X_BOTTOM - LOGO_X_TOP);
    if (!measure_only) {
        ui_mark(x + mw / 2, y + font_table[font].ascent - lh / 2.0f, mh, 255);
        gfx_str(font, x + (int)mw + gap, y, C_TEXT, "XenithOS");
    }
    return (int)mw + gap + gfx_str_w(font, "XenithOS");
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

/* On the desktop the hints name the keys that do what the controller's buttons do (the
 * keyboard map in linux/platform_sdl.c). */
static const char *desktop_key(const char *b)
{
    static const char *const map[][2] = {
        {"A", "Enter"}, {"B", "Esc"}, {"START", "F1"}, {"BACK", "Tab"}, {"LB", "Q"}, {"RB", "E"},
        {"LT", "PgUp"}, {"RT", "PgDn"},
    };
    int i;
    for (i = 0; i < (int)(sizeof map / sizeof map[0]); i++)
        if (!strcmp(b, map[i][0])) return map[i][1];
    return b;
}

/* A mouse, its left or right button or its wheel lit, for the hints of desktop apps. */
static int mouse_glyph(int x, int cy, int part)
{
    gfx_fill_rrect(x + 2.0f, cy - 12.0f, 16, 24, 8, C_SURFACE2, 255);
    gfx_stroke_rrect(x + 2.0f, cy - 12.0f, 16, 24, 8, 1.5f, C_SUB, 255);
    gfx_line(x + 3.0f, cy - 2.0f, x + 17.0f, cy - 2.0f, 1.2f, C_SUB, 255);
    gfx_line(x + 10.0f, cy - 11.0f, x + 10.0f, cy - 2.0f, 1.2f, C_SUB, 255);
    if (part == 0) gfx_fill_rrect(x + 4.0f, cy - 10.0f, 5, 7, 2, C_TEXT, 255);
    else if (part == 1) gfx_fill_rrect(x + 11.0f, cy - 10.0f, 5, 7, 2, C_TEXT, 255);
    else gfx_fill_rrect(x + 8.5f, cy - 9.0f, 3, 8, 1.5f, C_TEXT, 255);
    return 20;
}

/* The buttons a hint names, each as the button that does that job now (Accessibility >
 * Buttons): "A" is "B" once A and B have swapped, "LB/RB" each half on its own. */
static const char *moved(const char *b, char *out, int n)
{
    char buf[48], *t, *save = NULL;
    int o = 0;
    snprintf(buf, sizeof buf, "%s", b);
    out[0] = 0;
    for (t = strtok_r(buf, "/", &save); t && o < n; t = strtok_r(NULL, "/", &save)) {
        int k = ui_pad_find(t);
        o += snprintf(out + o, n - o, "%s%s", o ? "/" : "", k >= 0 ? pad_names[ui_pad_from(k)] : t);
    }
    return out;
}

/* A, B, X and Y as the controller's coloured buttons, anything else as a key cap. Sized to the
 * text (bigger with bigger text). */
static int glyph(int x, int cy, const char *b, int caps_only)
{
    uint32_t c = 0;
    int r = (gfx_line_h(FONT_SMALL) + 4) / 2;
    if (caps_only) c = 0;
    else if (!strcmp(b, "A")) c = C_BTN_A;
    else if (!strcmp(b, "B")) c = C_BTN_B;
    else if (!strcmp(b, "X")) c = C_BTN_X;
    else if (!strcmp(b, "Y")) c = C_BTN_Y;
    if (c) {
        gfx_circle((float)(x + r), (float)cy, (float)r, c, 255);
        gfx_str_c(FONT_SMALL, x + r, cy - gfx_line_h(FONT_SMALL) / 2, RGB(16, 18, 24), b);
        return 2 * r;
    } else {
        int w = gfx_str_w(FONT_SMALL, b) + 20;
        gfx_fill_rrect((float)x, (float)(cy - r), (float)w, (float)(2 * r), (float)r, C_SURFACE2, 255);
        gfx_str_c(FONT_SMALL, x + w / 2, cy - gfx_line_h(FONT_SMALL) / 2, C_SUB, b);
        return w;
    }
}

int ui_button_glyph(int x, int cy, const char *b)
{
    char m[48];
    if (ui_desktop && !strcmp(b, "Click")) return mouse_glyph(x, cy, 0);
    if (ui_desktop && !strcmp(b, "Right click")) return mouse_glyph(x, cy, 1);
    if (ui_desktop && !strcmp(b, "Wheel")) return mouse_glyph(x, cy, 2);
    if (ui_desktop) return glyph(x, cy, desktop_key(b), 1);   /* a key, drawn as a key cap */
    return glyph(x, cy, moved(b, m, sizeof m), 0);
}

int ui_button_glyph_as(int x, int cy, const char *b)
{
    return glyph(x, cy, b, 0);
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

void ui_loading_progress(const char *title, const char *detail, int done, int total)
{
    int cy = SCR_H / 2, w = 240, x = (SCR_W - w) / 2;
    ui_bg();
    ui_spinner(SCR_W / 2, cy - 44, 24, C_ACCENT);
    gfx_str_c(FONT_LABEL, SCR_W / 2, cy + 6, C_TEXT, title);
    if (detail) gfx_str_c(FONT_BODY, SCR_W / 2, cy + 46, C_SUB, detail);
    if (total > 0) {
        float f = (float)done / total;
        if (f > 1) f = 1;
        gfx_fill_rrect((float)x, cy + 96.0f, (float)w, 4, 2, C_SURFACE2, 255);
        if (f > 0) gfx_fill_rrect((float)x, cy + 96.0f, w * f, 4, 2, C_ACCENT, 255);
    }
    ui_end_frame();
}

void ui_loading_frame(const char *title, const char *detail)
{
    ui_loading_progress(title, detail, 0, 0);
}

static const char *launch_title, *launch_detail;
static uint64_t launch_drawn;

static void launch_frame(int done, int total)
{
    uint64_t now = ui_ms();
    if (now - launch_drawn < 30 && done < total) return;
    launch_drawn = now;
    ui_loading_progress(launch_title, launch_detail, done, total);
}

void ui_launch_text(const char *title, const char *detail)
{
    launch_title = title;
    launch_detail = detail;
    launch_drawn = 0;
}

int ui_launch(const char *path, const char *title, const char *detail)
{
    int started;
    launch_title = title;
    launch_detail = detail;
    launch_drawn = 0;
    ui_loading_progress(title, detail, 0, 0);
    look_give();
    plat_set_launch_progress(launch_frame);
    started = plat_launch(path);
    plat_set_launch_progress(NULL);
    ui_settle_input();             /* on a PC the app has just quit: its last presses aren't ours */
    return started;
}

void ui_back_to_xenithos(const char *app_id)
{
    /* A desktop app has no XenithOS to go back to: it just closes (plat_launch does that). */
    if (ui_desktop) {
        plat_launch(XENITH_SHELL_PATH);
        return;
    }
    handoff_set("from", app_id);
    ui_launch(XENITH_SHELL_PATH, "XenithOS", "Going back");
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
    int sel = 0, w = SCR_W * 46 / 100, row = 54, x, y, h, body_lines = 0, body_max = 6, title_lines = 1, result = -1, i;
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
    {
        /* Measure the title and the body by drawing them off to the side of a scratch clip. A
         * body that fills every line gets a wider box, and a long title wraps, so the words stay
         * inside the box and on the screen. */
        int clip[4];
        gfx_get_clip(clip);
        gfx_set_clip(0, 0, 0, 0);
        if (body) {
            body_lines = gfx_wrap(FONT_BODY, 0, 0, w - 64, 4, 0, body, body_max);
            if (body_lines == body_max && w < SCR_W * 64 / 100) {
                w = SCR_W * 64 / 100;
                body_max = 8;
                body_lines = gfx_wrap(FONT_BODY, 0, 0, w - 64, 4, 0, body, body_max);
            }
        }
        title_lines = gfx_wrap(FONT_TITLE, 0, 0, w - 64, 2, 0, title, 2);
        gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
    }
    h = 32 + title_lines * (gfx_line_h(FONT_TITLE) + 2) + 8 + body_lines * (gfx_line_h(FONT_BODY) + 4) + 22 + n * row + 24;
    x = (SCR_W - w) / 2;
    y = (SCR_H - h) / 2;
    hl = (float)sel;

    for (;;) {
        int d, oy, clicked = -1;
        ui_poll();
        d = ui_nav_y();
        if (d) {
            int nsel = sel + d;
            if (nsel >= 0 && nsel < n) { sel = nsel; sfx(SFX_MOVE); }
        }
        if (ui_desktop) {
            /* The pointer picks a choice when it moves onto it (so it doesn't fight the arrow
             * keys while it rests), and a click chooses it. */
            int top = y + 32 + title_lines * (gfx_line_h(FONT_TITLE) + 2) + 8 + body_lines * (gfx_line_h(FONT_BODY) + 4) + 22;
            for (i = 0; i < n; i++)
                if (ui_mouse_in(x + 20, top + i * row, w - 40, row - 8)) {
                    if ((mouse.dx || mouse.dy) && sel != i) { sel = i; sfx(SFX_MOVE); }
                    if (mouse.clicks & 1) clicked = i;
                }
        }
        if (clicked >= 0) { sfx(SFX_SELECT); result = clicked; break; }
        if (PRESSED(a)) { sfx(SFX_SELECT); result = sel; break; }
        if (PRESSED(b)) { sfx(SFX_BACK); result = -1; break; }

        if (dimmed) memcpy(gfx_bb, dimmed, bytes);
        else {
            if (under) memcpy(gfx_bb, under, bytes);
            gfx_rect_a(0, 0, SCR_W, SCR_H, RGB(0, 0, 0), 150);
        }
        gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 14, C_SURFACE, 255);
        gfx_stroke_rrect((float)x, (float)y, (float)w, (float)h, 14, 1, C_LINE, 255);
        gfx_wrap(FONT_TITLE, x + 32, y + 26, w - 64, 2, C_TEXT, title, 2);
        oy = y + 32 + title_lines * (gfx_line_h(FONT_TITLE) + 2) + 8;
        if (body) gfx_wrap(FONT_BODY, x + 32, oy, w - 64, 4, C_SUB, body, body_max);
        oy += body_lines * (gfx_line_h(FONT_BODY) + 4) + 22;
        hl = ui_ease(hl, (float)sel, 0.45f);
        /* The selected choice is a white button, like the site's Play */
        gfx_fill_rrect(x + 20.0f, oy + hl * row, w - 40.0f, row - 8.0f, 8, C_TEXT, 255);
        for (i = 0; i < n; i++)
            gfx_str_vc(FONT_BODY_SB, x + 40, oy + i * row + (row - 8) / 2, i == sel ? C_INK : C_TEXT, options[i]);
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
