/* CLOUD: XenithOS Cloud's status and your friends who are on, the news, and the VOLT
 * leaderboard. XenithOS Cloud is the free online service every copy of XenithOS uses for
 * profiles, friends, the Store and leaderboards. */
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
#include "social.h"
#include "theme.h"
#include "ui.h"

#define MAX_ROWS 6

static char motd[128], version[32], players[16], online[16];
static int ping_ms, have_stats;
static char news[MAX_ROWS][120], news_when[MAX_ROWS][24];
static int nnews;
static struct { char rank[6], name[20], score[16], avatar[16]; } board[MAX_ROWS];
static int nboard;
static int have_friends;          /* the friends list came, so the first card can show who's on */

/* "3 days ago" style, from a unix time and the server's clock. */
static void ago(long t, long now, char *out, int n)
{
    if (t <= 0 || now <= 0) { out[0] = 0; return; }
    cloud_ago(now - t, out, n);
}

static void read_news(struct http_resp *r, long now)
{
    char *line = r->body;
    nnews = 0;
    while (line && *line && nnews < MAX_ROWS) {
        char *next = strchr(line, '\n'), *f[3];
        if (next) *next++ = 0;
        if (split_fields(line, f, 3) == 3) {
            snprintf(news[nnews], sizeof news[0], "%s", f[2]);
            ago(atol(f[1]), now, news_when[nnews], sizeof news_when[0]);
            if (atoi(f[0]) > st.last_inbox) { st.last_inbox = atoi(f[0]); settings_save(); }
            nnews++;
        }
        line = next;
    }
}

static int refresh(void)
{
    struct http_resp r;
    char path[160], name[48], v[32];
    uint64_t t0;
    long now = 0;
    int code;

    net_escape(prof.signed_in ? prof.name : cfg.name, name, sizeof name);
    t0 = ui_ms();
    code = cloud_get("/v1/ping", 1024, &r);
    ping_ms = (int)(ui_ms() - t0);
    http_free(&r);
    if (code != 200) {
        sfx(SFX_ERROR);
        ui_message("XenithOS Cloud isn't answering", cloud_error_text(NULL, code));
        return 0;
    }

    /* With a profile, the hello that keeps friends seeing you on (an older XenithOS Cloud only
     * has the plain one) */
    code = prof.signed_in ? social_hello("", 0, NULL, &r) : 0;
    if (code != 200) {
        http_free(&r);
        snprintf(path, sizeof path, "/v1/hello?name=%s&ver=" XENITH_VERSION, name);
        code = cloud_get(path, 16 * 1024, &r);
    }
    if (code == 200) {
        kv_get(r.body, "motd", motd, sizeof motd);
        kv_get(r.body, "server", version, sizeof version);
        kv_get(r.body, "players", players, sizeof players);
        kv_get(r.body, "online", online, sizeof online);
        kv_get(r.body, "now", v, sizeof v);
        now = atol(v);
        have_stats = 1;
    }
    http_free(&r);

    nnews = 0;
    if (cloud_get("/v1/news", 16 * 1024, &r) == 200) read_news(&r, now);
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

    have_friends = prof.signed_in && friends_fetch() == 200;
    return 1;
}

/* Friends who are on, under the service's numbers: up to rows of them. */
static void draw_friends(int x, int y, int w, int bottom)
{
    int i, shown = 0, on = 0, rowh = 52, fit;
    char v[64];
    uint32_t c;
    for (i = 0; i < nfriend_rows; i++)
        if (friend_rows[i].rel == REL_FRIEND && friend_rows[i].online) on++;
    gfx_str(FONT_BODY_SB, x, y, C_TEXT, "Friends online");
    if (on) {
        snprintf(v, sizeof v, "%d", on);
        gfx_str_r(FONT_BODY_SB, x + w, y, C_SUB, v);
    }
    y += 40;
    if (!prof.signed_in) {
        gfx_wrap(FONT_BODY, x, y, w, 2, C_SUB, "Sign in to see your friends here.", 2);
        return;
    }
    if (!have_friends) return;
    if (!on) {
        int any = 0;
        for (i = 0; i < nfriend_rows; i++) any |= friend_rows[i].rel == REL_FRIEND;
        gfx_wrap(FONT_BODY, x, y, w, 2, C_SUB, any ? "None of your friends are on right now." :
                 "Add friends with X, by their gamertag.", 2);
        return;
    }
    fit = (bottom - y) / rowh;
    if (on > fit) fit--;                                 /* the last line says how many more */
    for (i = 0; i < nfriend_rows && shown < fit; i++) {
        const struct friend_row *p = &friend_rows[i];
        if (p->rel != REL_FRIEND || !p->online) continue;
        avatar_draw(p->avatar, x, y, 40);
        gfx_circle(x + 36.0f, y + 36.0f, 7, C_SURFACE, 255);
        gfx_circle(x + 36.0f, y + 36.0f, 4.5f, C_OK, 255);
        gfx_str_fit(FONT_BODY_SB, x + 54, y - 3, C_TEXT, p->name, w - 54);
        friend_status(p, v, sizeof v, &c);
        gfx_str_fit(FONT_SMALL, x + 54, y + 22, C_SUB, v, w - 54);
        y += rowh;
        shown++;
    }
    if (on > shown) {
        snprintf(v, sizeof v, "and %d more in Friends", on - shown);
        gfx_str(FONT_SMALL, x, y + 4, C_SUB, v);
    }
}

static void stat_row(int x, int y, int w, const char *k, const char *v)
{
    gfx_str(FONT_SMALL, x, y, C_SUB, k);
    gfx_str_r(FONT_BODY_SB, x + w, y - 3, C_TEXT, v);
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
    ui_header("Cloud", motd[0] ? motd : "Profiles, news and leaderboards for everyone on XenithOS");

    /* The service */
    ui_panel(x1, top, w, h);
    card_title(x1 + pad, top + pad, ICON_CLOUD, "XenithOS Cloud");
    y = top + pad + 56;
    if (have_stats) {
        int iw = w - 2 * pad;
        gfx_str(FONT_SMALL, x1 + pad, y, C_SUB, "Status");
        gfx_circle(x1 + w - pad - gfx_str_w(FONT_BODY_SB, "Online") - 12.0f, y + 9.0f, 4, C_OK, 255);
        gfx_str_r(FONT_BODY_SB, x1 + w - pad, y - 3, C_TEXT, "Online");
        y += 38;
        stat_row(x1 + pad, y, iw, "Players", players[0] ? players : "-"); y += 38;
        stat_row(x1 + pad, y, iw, "Online now", online[0] ? online : "-"); y += 38;
        snprintf(v, sizeof v, "%d ms", ping_ms);
        stat_row(x1 + pad, y, iw, "Ping", v); y += 38;
        gfx_rect(x1 + pad, y + 4, iw, 1, C_LINE);
        draw_friends(x1 + pad, y + 22, iw, top + h - pad);
    }

    /* News */
    ui_panel(x2, top, w, h);
    card_title(x2 + pad, top + pad, ICON_MAIL, "News");
    y = top + pad + 56;
    if (!nnews) gfx_str(FONT_BODY, x2 + pad, y, C_SUB, "Nothing new right now");
    for (i = 0; i < nnews && y < top + h - 70; i++) {
        int lines;
        if (news_when[i][0]) {
            gfx_str(FONT_SMALL, x2 + pad, y, C_DIM, news_when[i]);
            y += gfx_line_h(FONT_SMALL) + 2;
        }
        lines = gfx_wrap(FONT_BODY, x2 + pad, y, w - 2 * pad, 2, i == 0 ? C_TEXT : C_SUB, news[i], 3);
        y += lines * (gfx_line_h(FONT_BODY) + 2) + 18;
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
    ui_hints("X=Friends;Y=Refresh;B=Back");
}

void screen_cloud(void)
{
    uint64_t last;
    if (!cloud_ready("Cloud")) return;
    ui_busy_begin("Connecting to XenithOS Cloud", NULL);
    if (!refresh()) { ui_busy_end(); return; }
    ui_busy_end();
    last = ui_ms();
    ui_fade_in();
    for (;;) {
        ui_poll();
        if (PRESSED(b)) { sfx(SFX_BACK); return; }
        if (PRESSED(x)) {
            sfx(SFX_SELECT);
            screen_friends();
            have_friends = prof.signed_in && friends_fetch() == 200;
            ui_fade_in();
        }
        if (PRESSED(y) || ui_ms() - last > 15000) {
            if (PRESSED(y)) sfx(SFX_SELECT);
            if (!refresh()) return;
            last = ui_ms();
        }
        draw();
        ui_end_frame();
    }
}
