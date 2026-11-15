/* VOLT: the first XENITH app.
 *
 * Steer a streak of light around the grid, collect sparks to grow, and don't hit
 * the walls or yourself. Every 5 sparks the streak gets faster.
 *
 * Controls: D-pad / left stick steer, START pause, A start / retry, BACK returns to XENITH.
 * After a crash, X uploads the score to the XENITH Cloud leaderboard.
 *
 * Drawing is incremental (only the head, the old tail and the spark change per step),
 * which keeps it fast and flicker-free on a single framebuffer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xenos/xenos.h>
#include <console/console.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <time/time.h>
#include <elf/elf.h>
#include <libfat/fat.h>
#include <ppc/timebase.h>
#include <xenon_soc/xenon_power.h>

#include "gfx.h"
#include "theme.h"
#include "net.h"
#include "config.h"

static const uint32_t C_FIELD = RGB(16, 16, 21);
static const uint32_t C_SPARK = RGB(255, 214, 90);

/* ------------------------------------------------------------------ layout */
static int S, M, TOP;
static int CELL, GW, GH, OX, OY;

#define GW_MAX 160
#define GH_MAX 90
#define CAP (GW_MAX * GH_MAX)

/* ------------------------------------------------------------------- state */
enum { UP, DOWN, LEFT, RIGHT };

static unsigned char occ[CAP];          /* 1 where the streak is */
static short body_x[CAP], body_y[CAP];  /* ring buffer, head at index `head` */
static int head, len, grow;
static int dir, want;
static int spark_x, spark_y;
static int score, best, sparks_eaten;
static int period;                      /* frames per step */
static int rumble_frames;

/* ------------------------------------------------------------------- input */
static struct controller_data_s cur, prev;
#define PRESSED(f) (cur.f && !prev.f)
#define STICK_TH 20000

static void poll_input(void)
{
    prev = cur;
    usb_do_poll();
    get_controller_data(&cur, 0);
    mdelay(16);
    if (rumble_frames > 0 && --rumble_frames == 0)
        set_controller_rumble(0, 0, 0);
}

static void read_direction(void)
{
    int d = -1;
    if (cur.up || cur.s1_y > STICK_TH) d = UP;
    else if (cur.down || cur.s1_y < -STICK_TH) d = DOWN;
    else if (cur.left || cur.s1_x < -STICK_TH) d = LEFT;
    else if (cur.right || cur.s1_x > STICK_TH) d = RIGHT;
    if (d < 0) return;
    /* no reversing straight into yourself */
    if ((d == UP && dir == DOWN) || (d == DOWN && dir == UP) ||
        (d == LEFT && dir == RIGHT) || (d == RIGHT && dir == LEFT))
        return;
    want = d;
}

static void buzz(int strength, int frames)
{
    set_controller_rumble(0, strength / 2, strength);
    rumble_frames = frames;
}

/* --------------------------------------------------------------- drawing */
static void cell(int x, int y, uint32_t c)
{
    gfx_rect(OX + x * CELL + 1, OY + y * CELL + 1, CELL - 2, CELL - 2, c);
}

static void draw_hud(void)
{
    char buf[48];
    int y = M / 2;
    gfx_rect(SCR_W / 2, y, SCR_W / 2 - M, GFX_TEXT_H(S), C_BG);
    snprintf(buf, sizeof buf, "SCORE %d   BEST %d", score, best);
    gfx_text(SCR_W - M - gfx_text_w(S, buf), y, S, C_TEXT, buf);
}

static void draw_chrome(void)
{
    int y = M / 2;
    gfx_clear(C_BG);
    int x = M + gfx_brand(M, y, S, C_ACCENT, C_TEXT);
    x += S * 6;
    x += gfx_text(x, y, S, C_DIM, "/") + S * 6;
    gfx_text(x, y, S, C_TEXT, "VOLT");
    gfx_text(M, SCR_H - M / 2 - GFX_TEXT_H(S - 1), S - 1, C_DIM,
             "STEER WITH STICK OR D-PAD     START PAUSE     BACK RETURN TO XENITHOS");
    gfx_rect(OX - 2, OY - 2, GW * CELL + 4, GH * CELL + 4, C_PANEL);
    gfx_rect(OX, OY, GW * CELL, GH * CELL, C_FIELD);
    draw_hud();
}

/* Centered message box over the field. Pass NULL for line2 to skip it. */
static void banner(const char *line1, const char *line2, uint32_t c1)
{
    int w = SCR_W / 2, h = GFX_TEXT_H(S + 2) + GFX_TEXT_H(S - 1) + S * 18;
    int x = (SCR_W - w) / 2, y = OY + (GH * CELL - h) / 2;
    gfx_rect(x, y, w, h, C_PANEL);
    gfx_rect(x, y, w, S, C_ACCENT);
    gfx_text_c(SCR_W / 2, y + S * 7, S + 2, c1, line1);
    if (line2)
        gfx_text_c(SCR_W / 2, y + h - S * 6 - GFX_TEXT_H(S - 1), S - 1, C_DIM, line2);
}

/* ------------------------------------------------------------------ logic */
static int idx(int x, int y) { return y * GW + x; }

static void place_spark(void)
{
    int free_cells = GW * GH - len, n, i;
    if (free_cells <= 0) return;
    n = rand() % free_cells;
    for (i = 0; i < GW * GH; i++) {
        if (occ[i]) continue;
        if (n-- == 0) {
            spark_x = i % GW;
            spark_y = i / GW;
            cell(spark_x, spark_y, C_SPARK);
            return;
        }
    }
}

static void new_game(void)
{
    int i, sx = GW / 4, sy = GH / 2;

    memset(occ, 0, sizeof occ);
    head = 0;
    len = 0;
    grow = 0;
    score = 0;
    sparks_eaten = 0;
    period = 7;
    dir = want = RIGHT;

    draw_chrome();
    for (i = 0; i < 4; i++) {                /* start 4 cells long, heading right */
        head = i;
        body_x[head] = sx + i;
        body_y[head] = sy;
        occ[idx(sx + i, sy)] = 1;
        len++;
        cell(sx + i, sy, i == 3 ? C_TEXT : C_ACCENT);
    }
    place_spark();
    gfx_flush();
}

/* Advance one step. Returns 0 when the streak crashes. */
static int step(void)
{
    int hx = body_x[head], hy = body_y[head];
    int nx = hx, ny = hy;

    dir = want;
    if (dir == UP) ny--;
    else if (dir == DOWN) ny++;
    else if (dir == LEFT) nx--;
    else nx++;

    if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) return 0;

    int eating = (nx == spark_x && ny == spark_y);

    /* Move the tail first (unless growing), so following your own tail is legal. */
    if (!eating && grow == 0) {
        int tail = (head - len + 1 + CAP) % CAP;
        occ[idx(body_x[tail], body_y[tail])] = 0;
        cell(body_x[tail], body_y[tail], C_FIELD);
        len--;
    } else if (grow > 0) {
        grow--;
    }

    if (occ[idx(nx, ny)]) return 0;

    cell(hx, hy, C_ACCENT);                   /* old head becomes body */
    head = (head + 1) % CAP;
    body_x[head] = nx;
    body_y[head] = ny;
    occ[idx(nx, ny)] = 1;
    len++;
    cell(nx, ny, C_TEXT);

    if (eating) {
        sparks_eaten++;
        score += 10 * (8 - period);           /* faster = more points */
        grow += 2;
        if (sparks_eaten % 5 == 0 && period > 3) period--;
        if (score > best) best = score;
        draw_hud();
        buzz(180, 6);
        place_spark();
    }
    return 1;
}

/* Send the last score to XENITH Cloud and show the rank. */
static void upload_score(void)
{
    char body[64];
    struct http_resp r;
    int st;

    if (!cfg.server[0]) {
        banner("NO SERVER", "SET server= IN XENITH/CONFIG.TXT", C_WARN);
        gfx_flush();
        return;
    }
    banner("UPLOADING", "CONNECTING TO XENITHOS CLOUD", C_TEXT);
    gfx_flush();
    snprintf(body, sizeof body, "name=%s&score=%d", cfg.name, score);
    st = http_request(cfg.server, "POST", "/v1/scores/volt", body, strlen(body), 4096, NULL, NULL, &r);
    if (st == 200) {
        char msg[64];
        int rank = atoi(r.body);
        if (rank > 0) snprintf(msg, sizeof msg, "%s IS RANK #%d     A PLAY AGAIN", cfg.name, rank);
        else snprintf(msg, sizeof msg, "NOT IN THE TOP 50 YET     A PLAY AGAIN");
        banner("UPLOADED", msg, C_OK);
    } else {
        banner("UPLOAD FAILED", st < 0 ? net_error(st) : st == 429 ? "WAIT A FEW SECONDS" : "SERVER REFUSED IT", C_BAD);
    }
    http_free(&r);
    gfx_flush();
}

static void return_to_xenith(void)
{
    set_controller_rumble(0, 0, 0);
    draw_chrome();
    banner("RETURNING", NULL, C_TEXT);
    gfx_flush();
    elf_runFromDisk(XENITH_SHELL_PATH);
    /* If XENITH isn't found, falling out of main() drops back to XeLL. */
}

/* ------------------------------------------------------------------- main */
int main(void)
{
    int frame = 0, paused = 0, alive = 0, crashed = 0, uploaded = 0;

    xenos_init(VIDEO_MODE_AUTO);
    console_init();
    console_close();
    xenon_make_it_faster(XENON_SPEED_FULL);
    gfx_init();

    S = SCR_H >= 600 ? 3 : 2;
    M = SCR_W / 16;
    TOP = M / 2 + GFX_TEXT_H(S) + S * 10;
    CELL = S * 6;
    GW = (SCR_W - 2 * M) / CELL;
    GH = (SCR_H - TOP - M) / CELL;
    if (GW > GW_MAX) GW = GW_MAX;
    if (GH > GH_MAX) GH = GH_MAX;
    OX = (SCR_W - GW * CELL) / 2;
    OY = TOP;

    usb_init();
    usb_do_poll();
    fatInitDefault();
    config_load();
    srand((unsigned)mftb());

    /* Title */
    draw_chrome();
    banner("VOLT", "PRESS A TO START", C_ACCENT);
    gfx_flush();

    for (;;) {
        poll_input();

        if (PRESSED(back)) { return_to_xenith(); return 0; }

        if (!alive) {
            if (PRESSED(a) || PRESSED(start)) {
                new_game();
                alive = 1;
                paused = 0;
                frame = 0;
                crashed = 0;
                uploaded = 0;
            } else if (PRESSED(x) && crashed && !uploaded && score > 0) {
                set_controller_rumble(0, 0, 0);
                upload_score();
                uploaded = 1;
            }
            continue;
        }

        if (PRESSED(start)) {
            paused = !paused;
            if (paused) {
                banner("PAUSED", "START TO RESUME", C_TEXT);
                gfx_flush();
            } else {
                /* Redraw the whole field under the banner. */
                int i;
                gfx_rect(OX, OY, GW * CELL, GH * CELL, C_FIELD);
                for (i = 0; i < len; i++) {
                    int k = (head - i + CAP) % CAP;
                    cell(body_x[k], body_y[k], i == 0 ? C_TEXT : C_ACCENT);
                }
                cell(spark_x, spark_y, C_SPARK);
                gfx_flush();
            }
        }
        if (paused) continue;

        read_direction();
        if (++frame < period) continue;
        frame = 0;

        if (!step()) {
            char buf[48];
            alive = 0;
            crashed = 1;
            cell(body_x[head], body_y[head], C_BAD);
            buzz(255, 20);
            if (score > 0 && cfg.server[0])
                snprintf(buf, sizeof buf, "SCORE %d   A RETRY   X UPLOAD", score);
            else
                snprintf(buf, sizeof buf, "SCORE %d     A TO RETRY", score);
            banner("CRASHED", buf, C_BAD);
        }
        gfx_flush();
    }
}
