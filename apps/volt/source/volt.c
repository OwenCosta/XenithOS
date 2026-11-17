/* VOLT: a XenithOS game.
 *
 * Steer a streak of light around the grid, collect sparks to grow, and don't hit the
 * walls or yourself. Every 5 sparks the streak speeds up and sparks are worth more.
 *
 * D-pad / left stick steer. START pauses. After a crash: A plays again, X uploads your
 * score to XenithOS Cloud, B goes back to XenithOS.
 *
 * On the desktop (opened from the Apps menu, ui_desktop): the arrows or WASD steer, Space or a
 * click starts, Space or Esc pauses and Q quits from there, and after a crash the buttons can be
 * clicked (Enter plays again, X uploads, Esc closes the window).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "avatar.h"
#include "config.h"
#include "gfx.h"
#include "net.h"
#include "platform.h"
#include "handoff.h"
#include "social.h"
#include "startlog.h"
#include "theme.h"
#include "ui.h"

#define C_SPARK RGB(255, 214, 90)
#define GW_MAX 64
#define GH_MAX 40
#define CAP (GW_MAX * GH_MAX)

enum { UP, DOWN, LEFT, RIGHT };
enum { S_TITLE, S_PLAY, S_PAUSE, S_OVER };

static int CELL, GW, GH, OX, OY;
static unsigned char occ[CAP];
static short bx[CAP], by[CAP];       /* ring buffer, head at index `head` */
static int head, len, grow, dir, want, spark_x, spark_y;
static int score, best, sparks, state;
static uint64_t next_step, rumble_until;
static char result_line[96];

static int idx(int x, int y) { return y * GW + x; }

static int step_ms(void)
{
    int ms = 115 - (sparks / 5) * 9;
    return ms < 55 ? 55 : ms;
}

static void place_spark(void)
{
    int free_cells = GW * GH - len, n, i;
    if (free_cells <= 0) return;
    n = rand() % free_cells;
    for (i = 0; i < GW * GH; i++) {
        if (occ[i]) continue;
        if (n-- == 0) { spark_x = i % GW; spark_y = i / GW; return; }
    }
}

static void new_game(void)
{
    int i, sx = GW / 4, sy = GH / 2;
    memset(occ, 0, sizeof occ);
    head = len = grow = score = sparks = 0;
    dir = want = RIGHT;
    for (i = 0; i < 4; i++) {
        head = i;
        bx[head] = sx + i;
        by[head] = sy;
        occ[idx(sx + i, sy)] = 1;
        len++;
    }
    place_spark();
    result_line[0] = 0;
    next_step = ui_ms() + 400;
    state = S_PLAY;
}

static void read_direction(void)
{
    int d = -1;
    if (cur.up || cur.ly > STICK_TH) d = UP;
    else if (cur.down || cur.ly < -STICK_TH) d = DOWN;
    else if (cur.left || cur.lx < -STICK_TH) d = LEFT;
    else if (cur.right || cur.lx > STICK_TH) d = RIGHT;
    if (d < 0) return;
    if ((d == UP && dir == DOWN) || (d == DOWN && dir == UP) || (d == LEFT && dir == RIGHT) || (d == RIGHT && dir == LEFT))
        return;
    want = d;
}

static void rumble(int l, int r, int ms)
{
    plat_rumble(l, r);
    rumble_until = ui_ms() + ms;
}

/* One step. Returns 0 on a crash. */
static int step(void)
{
    int nx = bx[head], ny = by[head], eating;
    dir = want;
    if (dir == UP) ny--;
    else if (dir == DOWN) ny++;
    else if (dir == LEFT) nx--;
    else nx++;
    if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) return 0;

    eating = nx == spark_x && ny == spark_y;
    if (!eating && grow == 0) {                  /* the tail moves first, so chasing it is fine */
        int tail = (head - len + 1 + CAP) % CAP;
        occ[idx(bx[tail], by[tail])] = 0;
        len--;
    } else if (grow > 0) {
        grow--;
    }
    if (occ[idx(nx, ny)]) return 0;

    head = (head + 1) % CAP;
    bx[head] = nx;
    by[head] = ny;
    occ[idx(nx, ny)] = 1;
    len++;

    if (eating) {
        sparks++;
        score += 10 * (1 + sparks / 5);
        grow += 2;
        if (score > best) best = score;
        sfx(SFX_SPARK);
        rumble(60, 160, 90);
        place_spark();
    }
    return 1;
}

/* ------------------------------------------------------------------ cloud */

static void upload(void)
{
    char form[160];
    struct http_resp r;
    int code;
    if (!cfg.server[0]) { snprintf(result_line, sizeof result_line, "XenithOS Cloud isn't set up on this machine"); return; }
    if (prof.signed_in) snprintf(form, sizeof form, "token=%s&score=%d", prof.token, score);
    else snprintf(form, sizeof form, "name=%s&score=%d", cfg.name, score);
    ui_busy_begin("Uploading your score", prof.signed_in ? prof.name : cfg.name);
    audio_prepare_stall();
    if (!net_up()) {
        ui_busy_end();
        snprintf(result_line, sizeof result_line, "No network. Plug in an Ethernet cable.");
        sfx(SFX_ERROR);
        return;
    }
    code = http_request(cfg.server, "POST", "/v1/scores/volt", form, (int)strlen(form), 4096, NULL, NULL, &r);
    ui_busy_end();
    if (code == 200) {
        int rank = atoi(r.body);
        if (rank > 0) snprintf(result_line, sizeof result_line, "Uploaded. You're #%d on the leaderboard.", rank);
        else snprintf(result_line, sizeof result_line, "Uploaded. Not in the top 50 yet.");
        sfx(SFX_SUCCESS);
        if (rank > 0) social_earn("volt-board");
        social_flush();                    /* the network's up now: what's waiting goes too */
    } else {
        char err[96] = "";
        if (r.body) {
            const char *e = strstr(r.body, "error=");
            if (e) {
                snprintf(err, sizeof err, "%s", e + 6);
                if (strchr(err, '\n')) *strchr(err, '\n') = 0;
            }
        }
        snprintf(result_line, sizeof result_line, "%s", err[0] ? err : code < 0 ? net_error(code) : "XenithOS Cloud didn't take it.");
        sfx(SFX_ERROR);
    }
    http_free(&r);
}

/* ------------------------------------------------------------------ drawing */

static void cell(int x, int y, uint32_t c, float pad)
{
    gfx_fill_rrect(OX + x * CELL + pad, OY + y * CELL + pad, CELL - 2 * pad, CELL - 2 * pad, CELL * 0.28f, c, 255);
}

#define CARD_W 520
#define CARD_H 250

static void overlay(const char *title, const char *big, const char *line1, const char *line2)
{
    int w = CARD_W, h = CARD_H, x = (SCR_W - w) / 2, y = OY + (GH * CELL - h) / 2;
    gfx_fill_rrect(OX - 8.0f, OY - 8.0f, GW * CELL + 16.0f, GH * CELL + 16.0f, 16, C_BG, 150);
    gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 18, C_SURFACE, 255);
    gfx_str_c(FONT_BODY_SB, SCR_W / 2, y + 26, C_SUB, title);
    gfx_str_c(FONT_BIG, SCR_W / 2, y + 56, C_TEXT, big);
    if (line1) gfx_str_c(FONT_BODY, SCR_W / 2, y + 150, C_SUB, line1);
    if (line2) gfx_str_c(FONT_BODY_SB, SCR_W / 2, y + 186, C_ACCENT_HI, line2);
}

/* On the desktop, the crash card's buttons: Play again, and Upload score when there's one to
 * send. Where button i is: */
#define BTN_W 200
#define BTN_H 44

static int can_upload(int uploaded)
{
    return cfg.server[0] && score && !uploaded;
}

static void button_box(int i, int n, int *x, int *y)
{
    *x = SCR_W / 2 - (n * BTN_W + (n - 1) * 16) / 2 + i * (BTN_W + 16);
    *y = OY + (GH * CELL - CARD_H) / 2 + CARD_H - BTN_H - 24;
}

static void draw_button(int i, int n, const char *label, int primary)
{
    int x, y, over;
    button_box(i, n, &x, &y);
    over = ui_mouse_in(x, y, BTN_W, BTN_H);
    gfx_fill_rrect((float)x, (float)y, BTN_W, BTN_H, BTN_H / 2.0f,
                   primary ? (over ? 0xFFFFFF : C_TEXT) : (over ? C_SEL : C_SURFACE2), 255);
    gfx_str_c(FONT_BODY_SB, x + BTN_W / 2, y + (BTN_H - gfx_line_h(FONT_BODY_SB)) / 2, primary ? C_INK : C_TEXT, label);
}

static int button_clicked(int i, int n)
{
    int x, y;
    button_box(i, n, &x, &y);
    return ui_clicked(x, y, BTN_W, BTN_H);
}

static void draw(int uploaded)
{
    int i;
    char buf[64];
    float t = (ui_ms() % 100000) / 1000.0f;

    ui_bg();
    gfx_str(FONT_TITLE, M, 26, C_TEXT, "VOLT");
    snprintf(buf, sizeof buf, "Score %d", score);
    gfx_str(FONT_LABEL, M + 120, 30, C_TEXT, buf);
    snprintf(buf, sizeof buf, "Best %d", best);
    gfx_str(FONT_BODY, M + 300, 33, C_SUB, buf);
    ui_profile_chip(SCR_W - M, 46);

    gfx_fill_rrect(OX - 8.0f, OY - 8.0f, GW * CELL + 16.0f, GH * CELL + 16.0f, 16, C_SURFACE, 255);
    for (i = 0; i < GW * GH; i++)
        gfx_rect(OX + (i % GW) * CELL + CELL / 2, OY + (i / GW) * CELL + CELL / 2, 2, 2, C_SURFACE2);

    for (i = len - 1; i >= 0; i--) {
        int k = (head - i + CAP) % CAP;
        cell(bx[k], by[k], i == 0 ? 0xFFFFFF : gfx_mix(C_ACCENT, RGB(30, 80, 190), i * 128 / (len + 1)), 1.5f);
    }
    {
        float pulse = 3.0f + 1.5f * sinf(t * 7);
        cell(spark_x, spark_y, C_SPARK, pulse);
    }

    if (state == S_TITLE)
        overlay("XenithOS game", "VOLT", "Grab the sparks. Don't hit the walls or yourself.",
                ui_desktop ? "Press Space or click to start" : "Press A to start");
    else if (state == S_PAUSE)
        overlay("Take a breather", "Paused", ui_desktop ? "Press Space to keep going" : "Press START to keep going", NULL);
    else if (state == S_OVER) {
        snprintf(buf, sizeof buf, "%d", score);
        overlay("You crashed", buf, result_line[0] ? result_line : (score >= best && score ? "New best!" : "So close."),
                !ui_desktop && cfg.server[0] && score && !result_line[0] ? "X uploads your score" : NULL);
        if (ui_desktop) {
            int n = can_upload(uploaded) ? 2 : 1;
            draw_button(0, n, "Play again", 1);
            if (n == 2) draw_button(1, n, "Upload score", 0);
        }
    }

    /* (On the desktop, ui_hints names the keys: Enter for A, Esc for B, Q for LB.) */
    if (ui_desktop) {
        if (state == S_OVER) ui_hints(can_upload(uploaded) ? "A=Play again;X=Upload score;B=Close" : "A=Play again;B=Close");
        else if (state == S_PLAY) ui_hints("Esc=Pause");
        else if (state == S_PAUSE) ui_hints("Space=Keep going;LB=Quit");
        else ui_hints("Space=Start;B=Close");
    } else if (state == S_OVER) ui_hints(cfg.server[0] ? "A=Play again;X=Upload score;B=XenithOS" : "A=Play again;B=XenithOS");
    else if (state == S_PLAY) ui_hints("START=Pause");
    else ui_hints("A=Start;B=XenithOS");
}

static void back_to_shell(void)
{
    plat_rumble(0, 0);
    ui_back_to_xenithos("volt");
}

/* Achievements for the score of a run that's just ended. They go to XenithOS Cloud now if the
 * network's up (a PC), or when XenithOS next starts. */
static void earn_for_score(void)
{
    static const struct { int score; const char *id; } marks[] = {
        {100, "volt-100"}, {500, "volt-500"}, {1000, "volt-1000"},
    };
    int i, any = 0;
    for (i = 0; i < (int)(sizeof marks / sizeof marks[0]); i++)
        if (score >= marks[i].score) any |= social_earn(marks[i].id);
    if (any) social_flush();
}

int main(int argc, char **argv)
{
    int uploaded = 0, started = 0;

    handoff_args(argc, argv);
    startlog_mark("start");
    plat_init();
    startlog_mark("video");
    gfx_init();
    ui_init();
    ui_loading_frame("VOLT", "Loading");       /* something on screen while the rest starts */
    plat_storage_init();
    startlog_mark("storage");
    config_load();
    settings_load();
    profile_load();
    audio_init();
    audio_set_volume(st.volume);
    startlog_mark("sounds");
    srand((unsigned)plat_time_us());

    CELL = SCR_H >= 600 ? 24 : 16;
    GW = (SCR_W - 2 * M) / CELL;
    GH = (SCR_H - 120 - 70) / CELL;
    if (GW > GW_MAX) GW = GW_MAX;
    if (GH > GH_MAX) GH = GH_MAX;
    OX = (SCR_W - GW * CELL) / 2;
    OY = 120;
    new_game();
    state = S_TITLE;
    ui_fade_in();

    for (;;) {
        ui_poll();
        if (rumble_until && ui_ms() > rumble_until) { plat_rumble(0, 0); rumble_until = 0; }

        switch (state) {
        case S_TITLE:
            if (PRESSED(a) || PRESSED(start) || (ui_desktop && ui_clicked(OX, OY, GW * CELL, GH * CELL))) {
                sfx(SFX_SELECT);
                new_game();
            } else if (PRESSED(b) || PRESSED(back)) { sfx(SFX_BACK); back_to_shell(); return 0; }
            break;
        case S_PLAY:
            /* On the desktop Space (A) and Esc (B) pause too: there's no START key to look for. */
            if (PRESSED(start) || (ui_desktop && (PRESSED(a) || PRESSED(b)))) { state = S_PAUSE; sfx(SFX_SELECT); break; }
            read_direction();
            while (ui_ms() >= next_step && state == S_PLAY) {
                next_step += step_ms();
                if (!step()) {
                    state = S_OVER;
                    uploaded = 0;
                    sfx(SFX_CRASH);
                    rumble(200, 255, 350);
                    earn_for_score();
                }
            }
            break;
        case S_PAUSE:
            if (PRESSED(start) ||
                (ui_desktop && (PRESSED(a) || PRESSED(b) || ui_clicked(OX, OY, GW * CELL, GH * CELL)))) {
                state = S_PLAY;
                next_step = ui_ms() + 300;
                sfx(SFX_SELECT);
            } else if (PRESSED(back) || (ui_desktop && PRESSED(lb))) { back_to_shell(); return 0; }
            break;
        case S_OVER: {
            int n = can_upload(uploaded) ? 2 : 1;
            if (PRESSED(a) || (ui_desktop && button_clicked(0, n))) { sfx(SFX_SELECT); new_game(); }
            else if ((PRESSED(x) || (ui_desktop && n == 2 && button_clicked(1, n))) && can_upload(uploaded)) {
                sfx(SFX_SELECT);
                upload();
                uploaded = 1;
            } else if (PRESSED(b) || PRESSED(back)) { sfx(SFX_BACK); back_to_shell(); return 0; }
            break;
        }
        }
        draw(uploaded);
        ui_end_frame();
        if (!started) { started = 1; startlog_ready("VOLT"); }
    }
}
