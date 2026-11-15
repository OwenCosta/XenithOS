/* CLOUD: your server's live stats, the inbox, and the VOLT leaderboard. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "avatar.h"
#include "config.h"
#include "gfx.h"
#include "icons.h"
#include "net.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

#define MAX_ROWS 6

static char motd[128], uptime[48], load[48], when[48];
static long mem_used, mem_total, disk_used, disk_total;
static int ping_ms, have_stats;
static char inbox[MAX_ROWS][120];
static int ninbox;
static struct { char rank[6], name[20], score[16], avatar[12]; } board[MAX_ROWS];
static int nboard;

static int refresh(void)
{
    struct http_resp r;
    char path[160], name[48], v[48];
    uint64_t t0;
    int code;

    net_escape(prof.signed_in ? prof.name : cfg.name, name, sizeof name);
    t0 = ui_ms();
    code = cloud_get("/v1/ping", 1024, &r);
    ping_ms = (int)(ui_ms() - t0);
    http_free(&r);
    if (code != 200) {
        sfx(SFX_ERROR);
        ui_message("Your server isn't answering", cloud_error_text(NULL, code));
        return 0;
    }

    snprintf(path, sizeof path, "/v1/hello?name=%s&ver=" XENITH_VERSION, name);
    if (cloud_get(path, 16 * 1024, &r) == 200) {
        kv_get(r.body, "motd", motd, sizeof motd);
        kv_get(r.body, "uptime", uptime, sizeof uptime);
        kv_get(r.body, "load", load, sizeof load);
        kv_get(r.body, "time", when, sizeof when);
        kv_get(r.body, "mem_used_mb", v, sizeof v); mem_used = atol(v);
        kv_get(r.body, "mem_total_mb", v, sizeof v); mem_total = atol(v);
        kv_get(r.body, "disk_used_gb", v, sizeof v); disk_used = atol(v);
        kv_get(r.body, "disk_total_gb", v, sizeof v); disk_total = atol(v);
        have_stats = 1;
    }
    http_free(&r);

    ninbox = 0;
    snprintf(path, sizeof path, "/v1/inbox?key=%s", cfg.key);
    if (cloud_get(path, 16 * 1024, &r) == 200) {
        char *line = r.body;
        while (line && *line && ninbox < MAX_ROWS) {
            char *next = strchr(line, '\n'), *f[3];
            if (next) *next++ = 0;
            if (split_fields(line, f, 3) == 3) {
                snprintf(inbox[ninbox++], sizeof inbox[0], "%s", f[2]);
                if (atoi(f[0]) > st.last_inbox) { st.last_inbox = atoi(f[0]); settings_save(); }
            }
            line = next;
        }
    }
    http_free(&r);

    nboard = 0;
    if (cloud_get("/v1/scores/volt", 16 * 1024, &r) == 200) {
        char *line = r.body;
        while (line && *line && nboard < MAX_ROWS) {
            char *next = strchr(line, '\n'), *f[4];
            int n;
            if (next) *next++ = 0;
            n = split_fields(line, f, 4);
            if (n >= 3) {
                snprintf(board[nboard].rank, sizeof board[0].rank, "%s", f[0]);
                snprintf(board[nboard].name, sizeof board[0].name, "%s", f[1]);
                snprintf(board[nboard].score, sizeof board[0].score, "%s", f[2]);
                snprintf(board[nboard].avatar, sizeof board[0].avatar, "%s", n >= 4 ? f[3] : "");
                nboard++;
            }
            line = next;
        }
    }
    http_free(&r);
    return 1;
}

static void stat_row(int x, int y, int w, const char *k, const char *v)
{
    gfx_str(FONT_SMALL, x, y, C_SUB, k);
    gfx_str_r(FONT_BODY_SB, x + w, y - 3, C_TEXT, v);
}

static void bar(int x, int y, int w, long used, long total)
{
    gfx_fill_rrect((float)x, (float)y, (float)w, 6, 3, C_SURFACE2, 255);
    if (total > 0) {
        int pct = (int)(used * 100 / total);
        uint32_t c = pct >= 90 ? C_BAD : pct >= 70 ? C_WARN : C_OK;
        gfx_fill_rrect((float)x, (float)y, w * (float)used / total, 6, 3, c, 255);
    }
}

static void card_title(int x, int y, int icon, const char *t)
{
    icon_draw(icon, x + 14.0f, y + 15.0f, 26, C_ACCENT_HI, C_SURFACE);
    gfx_str(FONT_BODY_SB, x + 38, y + 2, C_TEXT, t);
}

static void draw(void)
{
    int top = HEADER_H + 16, gap = 20, w = (SCR_W - 2 * M - 2 * gap) / 3, h = SCR_H - top - 70, i;
    int x1 = M, x2 = M + w + gap, x3 = M + 2 * (w + gap), pad = 24, y;
    char v[64];

    ui_bg();
    ui_header("Cloud", motd[0] ? motd : "Your XenithOS Cloud server");

    /* Server */
    ui_panel(x1, top, w, h);
    card_title(x1 + pad, top + pad, ICON_SERVER, "Your server");
    y = top + pad + 56;
    if (have_stats) {
        int iw = w - 2 * pad;
        stat_row(x1 + pad, y, iw, "Uptime", uptime); y += 44;
        stat_row(x1 + pad, y, iw, "Load", load); y += 44;
        snprintf(v, sizeof v, "%.1f / %.0f GB", mem_used / 1024.0, mem_total / 1024.0);
        stat_row(x1 + pad, y, iw, "Memory", v); y += 30;
        bar(x1 + pad, y, iw, mem_used, mem_total); y += 28;
        snprintf(v, sizeof v, "%ld / %ld GB", disk_used, disk_total);
        stat_row(x1 + pad, y, iw, "Disk", v); y += 30;
        bar(x1 + pad, y, iw, disk_used, disk_total); y += 28;
        snprintf(v, sizeof v, "%d ms", ping_ms);
        stat_row(x1 + pad, y, iw, "Ping", v); y += 44;
        gfx_str_fit(FONT_SMALL, x1 + pad, top + h - pad - 22, C_DIM, when, iw);
    }

    /* Inbox */
    ui_panel(x2, top, w, h);
    card_title(x2 + pad, top + pad, ICON_MAIL, "Inbox");
    y = top + pad + 56;
    if (!ninbox) gfx_str(FONT_BODY, x2 + pad, y, C_SUB, cfg.key[0] ? "No messages" : "Add key= to config.txt");
    for (i = 0; i < ninbox && y < top + h - 60; i++) {
        int lines = gfx_wrap(FONT_BODY, x2 + pad, y, w - 2 * pad, 2, i == 0 ? C_TEXT : C_SUB, inbox[i], 3);
        y += lines * (gfx_line_h(FONT_BODY) + 2) + 16;
    }

    /* Leaderboard */
    ui_panel(x3, top, w, h);
    card_title(x3 + pad, top + pad, ICON_TROPHY, "VOLT top scores");
    y = top + pad + 56;
    if (!nboard) gfx_str(FONT_BODY, x3 + pad, y, C_SUB, "No scores yet");
    for (i = 0; i < nboard && y < top + h - 50; i++) {
        int mine = prof.signed_in && !strcmp(board[i].name, prof.name);
        gfx_str_vc(FONT_BODY_SB, x3 + pad, y + 20, i == 0 ? C_WARN : C_DIM, board[i].rank);
        if (board[i].avatar[0]) avatar_draw(board[i].avatar, x3 + pad + 30, y, 40);
        else gfx_fill_rrect(x3 + pad + 30.0f, (float)y, 40, 40, 9, C_SURFACE2, 255);
        gfx_str_fit(FONT_BODY_SB, x3 + pad + 84, y + 2, mine ? C_ACCENT_HI : C_TEXT, board[i].name, w - 2 * pad - 170);
        gfx_str_r(FONT_BODY_SB, x3 + w - pad, y + 2, C_TEXT, board[i].score);
        y += 56;
    }
    ui_hints("Y=Refresh;B=Back");
}

void screen_cloud(void)
{
    uint64_t last;
    if (!cloud_ready("Cloud")) return;
    ui_busy_begin("Contacting your server", NULL);
    if (!refresh()) { ui_busy_end(); return; }
    ui_busy_end();
    last = ui_ms();
    ui_fade_in();
    for (;;) {
        ui_poll();
        if (PRESSED(b)) { sfx(SFX_BACK); return; }
        if (PRESSED(y) || ui_ms() - last > 15000) {
            if (PRESSED(y)) sfx(SFX_SELECT);
            if (!refresh()) return;
            last = ui_ms();
        }
        draw();
        ui_end_frame();
    }
}
