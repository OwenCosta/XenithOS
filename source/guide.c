/* The guide: XenithOS's side panel over a game or app (PCs). The Xbox button, or Esc held for
 * half a second, brings it up; the platform takes a picture of the screen first and puts
 * XenithOS's window in front (plat_set_guide in platform.h, linux/platform_sdl.c).
 *
 * The game stays where it was, darkened, and the panel slides in at the right:
 *   Resume          back to the game (B, or the Xbox button again, does the same)
 *   Return home     the home screen; a game from Flathub or Steam keeps running there
 *   Switch game     the other apps and games: the one chosen starts instead
 *   Controller      the game reads it, it moves the pointer (launchers and menus), or it
 *                   plays the game as a keyboard and mouse (Minecraft)
 *   Close           closes the game
 * XenithOS's own apps quit when you go home, so they only get the first three. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps.h"
#include "audio.h"
#include "gfx.h"
#include "icons.h"
#include "platform.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

#define PANEL_W 440
#define ROW_H 60

enum { ROW_RESUME, ROW_HOME, ROW_SWITCH, ROW_PAD, ROW_CLOSE };

static char closing_title[64];

static const int pad_order[3] = {PAD_GAME, PAD_MOUSE, PAD_KEYS};

/* The next (or previous) controller mode the game can have. */
static int next_mode(const struct plat_guide *g, int dir)
{
    int i, at = 0;
    for (i = 0; i < 3; i++) if (pad_order[i] == g->pad_mode) at = i;
    for (i = 1; i <= 3; i++) {
        int m = pad_order[(at + dir * i + 6) % 3];
        if (g->pad_modes & m) return m;
    }
    return g->pad_mode;
}

static const char *mode_name(int m)
{
    return m == PAD_MOUSE ? "Moves the pointer" : m == PAD_KEYS ? "Keyboard and mouse" : "Plays the game";
}

static const char *mode_help(int m)
{
    return m == PAD_MOUSE ? "For launchers and menus: the left stick moves the pointer, A clicks, B right clicks and the right stick scrolls."
         : m == PAD_KEYS ? "Laid out like Minecraft on a console: the sticks walk and look, RT breaks, LT places, A jumps, B drops, Y opens the inventory and LB and RB pick from the hotbar."
         : "The game reads the controller itself.";
}

/* Leaves "Closing <name>" up: the platform keeps drawing it while the game closes. */
static void say_closing(const struct plat_guide *g)
{
    snprintf(closing_title, sizeof closing_title, "Closing %s", g->name);
    ui_launch_text(closing_title, NULL);
    ui_loading_frame(closing_title, NULL);
}

/* The game as it was, darkened: drawn once, then copied in every frame. */
static uint32_t *make_backdrop(const uint32_t *shot)
{
    size_t bytes = (size_t)SCR_W * SCR_H * 4;
    uint32_t *out = malloc(bytes);
    if (shot) gfx_blit_scaled(shot, 1280, 720, 0, 0, SCR_W, SCR_H);
    else gfx_clear(C_BG);
    gfx_rect_a(0, 0, SCR_W, SCR_H, RGB(0, 0, 0), 150);
    if (out) memcpy(out, gfx_bb, bytes);
    return out;
}

static void draw_hint(int x, int y, const char *button, const char *what)
{
    x += ui_button_glyph(x, y, button) + 10;
    gfx_str(FONT_SMALL, x, y - gfx_line_h(FONT_SMALL) / 2, C_SUB, what);
}

/* One choice; the selection box under it is drawn separately, so it can glide. */
static void row(int x, int y, int w, int icon, const char *label, const char *value, int selected)
{
    icon_draw(icon, x + 16.0f, y + (ROW_H - 8) / 2.0f, 26, selected ? C_TEXT : C_SUB, selected ? C_SEL : C_SURFACE);
    gfx_str_vc(FONT_BODY_SB, x + 46, y + (ROW_H - 8) / 2, C_TEXT, label);
    if (value) {
        int vw = gfx_str_w(FONT_SMALL, value) + 24, cy = y + (ROW_H - 8) / 2;
        gfx_fill_rrect((float)(x + w - vw), cy - 14.0f, (float)vw, 28, 14, selected ? C_TEXT : C_SURFACE2, 255);
        gfx_str_c(FONT_SMALL, x + w - vw / 2, cy - gfx_line_h(FONT_SMALL) / 2, selected ? C_INK : C_TEXT, value);
    }
}

/* The other apps and games, for Switch game: indexes into apps[]. */
static int others(const struct plat_guide *g, int *out, int max)
{
    int i, n = 0;
    for (i = 0; i < napps && n < max; i++)
        if (!g->path || strcmp(apps[i].path, g->path)) out[n++] = i;
    return n;
}

int guide_run(struct plat_guide *g)
{
    int rows[5], nrows = 0, sel = 0, page = 0, pick = 0, first = 0, result = -1, i;
    int list[MAX_APPS], nlist = others(g, list, MAX_APPS);
    uint32_t *back = make_backdrop(g->backdrop);
    float slide = 0, hl = 0, hl2 = 0;

    if (!g->name) {                          /* XenithOS's own apps: the name it shows for them */
        g->name = "App";
        for (i = 0; i < napps; i++)
            if (g->path && !strcmp(apps[i].path, g->path)) g->name = apps[i].name;
    }
    rows[nrows++] = ROW_RESUME;
    rows[nrows++] = ROW_HOME;
    if (nlist) rows[nrows++] = ROW_SWITCH;
    if (next_mode(g, 1) != g->pad_mode) rows[nrows++] = ROW_PAD;
    if (g->keeps_running) rows[nrows++] = ROW_CLOSE;

    ui_settle_input();                       /* the Esc or Xbox button that opened it */
    sfx(SFX_SELECT);
    while (result < 0) {
        int x, y, w = PANEL_W - 64, d;
        ui_poll();
        d = ui_nav_y();
        if (page == 0) {
            if (d && sel + d >= 0 && sel + d < nrows) { sel += d; sfx(SFX_MOVE); }
            int dx = rows[sel] == ROW_PAD ? ui_nav_x() : 0;
            if (rows[sel] == ROW_PAD && (PRESSED(a) || dx)) {
                g->pad_mode = next_mode(g, dx < 0 ? -1 : 1);
                sfx(SFX_SELECT);
            } else if (PRESSED(a)) {
                sfx(SFX_SELECT);
                switch (rows[sel]) {
                case ROW_RESUME: result = GUIDE_RESUME; break;
                case ROW_HOME:   result = GUIDE_HOME; break;
                case ROW_SWITCH: page = 1; pick = 0; first = 0; hl2 = 0; break;
                case ROW_CLOSE:  result = GUIDE_CLOSE; break;
                }
            } else if (PRESSED(b) || PRESSED(guide)) {
                sfx(SFX_BACK);
                result = GUIDE_RESUME;
            }
        } else {
            if (d && pick + d >= 0 && pick + d < nlist) { pick += d; sfx(SFX_MOVE); }
            if (PRESSED(a)) {
                sfx(SFX_SELECT);
                snprintf(g->switch_to, sizeof g->switch_to, "%s", apps[list[pick]].path);
                result = GUIDE_SWITCH;
            } else if (PRESSED(b)) {
                sfx(SFX_BACK);
                page = 0;
            } else if (PRESSED(guide)) {
                sfx(SFX_BACK);
                result = GUIDE_RESUME;
            }
        }
        if (result >= 0) break;

        /* Draw: the game, then the panel sliding in from the right. */
        if (back) memcpy(gfx_bb, back, (size_t)SCR_W * SCR_H * 4);
        else gfx_clear(C_BG);
        slide = ui_ease(slide, 1, 0.35f);
        x = SCR_W - (int)(PANEL_W * slide);
        gfx_rect(x, 0, PANEL_W, SCR_H, C_SURFACE);
        gfx_rect(x, 0, 1, SCR_H, C_LINE);
        x += 32;
        ui_brand(x, 34, FONT_BODY_SB, 0);
        gfx_str(FONT_SMALL, x, 92, C_SUB, page == 0 ? (g->keeps_running ? "Playing" : "In the app") : "Switch to");
        gfx_str_fit(FONT_TITLE, x, 112, C_TEXT, page == 0 ? g->name : "Another game", w);
        y = 184;
        if (page == 0) {
            hl = ui_ease(hl, (float)sel, 0.45f);
            ui_select_box((float)x - 14, y + hl * ROW_H, (float)w + 28, ROW_H - 8.0f, 10);
            for (i = 0; i < nrows; i++) {
                int yy = y + i * ROW_H;
                switch (rows[i]) {
                case ROW_RESUME: row(x, yy, w, ICON_PLAY, "Resume", NULL, i == sel); break;
                case ROW_HOME:   row(x, yy, w, ICON_HOME, "Return home", NULL, i == sel); break;
                case ROW_SWITCH: row(x, yy, w, ICON_SWITCH, "Switch game", NULL, i == sel); break;
                case ROW_PAD:    row(x, yy, w, g->pad_mode == PAD_MOUSE ? ICON_POINTER : ICON_CONTROLLER, "Controller",
                                     mode_name(g->pad_mode), i == sel); break;
                case ROW_CLOSE:  row(x, yy, w, ICON_CLOSE, "Close game", NULL, i == sel); break;
                }
            }
            if (g->keeps_running && rows[sel] == ROW_HOME)
                gfx_wrap(FONT_SMALL, x, y + nrows * ROW_H + 14, w, 4, C_DIM,
                         "It keeps running. Choose it on the home screen to come back.", 3);
            else if (rows[sel] == ROW_PAD)
                gfx_wrap(FONT_SMALL, x, y + nrows * ROW_H + 14, w, 4, C_DIM, mode_help(g->pad_mode), 4);
            draw_hint(x, SCR_H - 40, "A", rows[sel] == ROW_PAD ? "Change" : "Select");
            draw_hint(x + 150, SCR_H - 40, "B", "Back to game");
        } else {
            int visible = (SCR_H - 60 - y) / 72, th = 50, tw = 88;
            if (pick < first) first = pick;
            if (pick >= first + visible) first = pick - visible + 1;
            hl2 = ui_ease(hl2, (float)(pick - first), 0.45f);
            ui_select_box((float)x - 14, y + hl2 * 72 - 4, (float)w + 28, 66, 10);
            for (i = first; i < nlist && i < first + visible; i++) {
                const struct app *a = &apps[list[i]];
                int yy = y + (i - first) * 72;
                app_card(a, x, yy + 4, tw, th, 0, 0);
                gfx_str_fit(FONT_BODY_SB, x + tw + 16, yy + 10, C_TEXT, a->name, w - tw - 16);
                gfx_str(FONT_SMALL, x + tw + 16, yy + 36, C_SUB,
                        app_is_other(a) ? (a->version[0] ? a->version : "Game") : "XenithOS app");
            }
            draw_hint(x, SCR_H - 40, "A", "Start");
            draw_hint(x + 130, SCR_H - 40, "B", "Back");
        }
        ui_end_frame();
    }
    free(back);
    do { ui_poll(); } while (cur.a || cur.b);
    if (result == GUIDE_CLOSE || result == GUIDE_SWITCH || (result == GUIDE_HOME && !g->keeps_running)) {
        say_closing(g);
    } else {
        /* Back to the game: its picture as it was, until it draws again. Home: a plain frame
         * until the home screen's first. */
        if (result == GUIDE_RESUME && g->backdrop) gfx_blit_scaled(g->backdrop, 1280, 720, 0, 0, SCR_W, SCR_H);
        else ui_bg();
        ui_end_frame();
    }
    return result;
}
