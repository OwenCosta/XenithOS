/* FRIENDS: who's on and what they're playing, friend requests, and adding someone by gamertag.
 * Friends only count once the other person says yes. XenithOS Cloud keeps the list
 * (/v1/friends); the Cloud screen shows the friends who are on from the same list. */
#include <ctype.h>
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

struct friend_row friend_rows[MAX_FRIEND_ROWS];
int nfriend_rows;

int friends_fetch(void)
{
    struct http_resp r;
    char form[96], *line;
    int code;
    if (!prof.signed_in) { nfriend_rows = 0; return 401; }
    snprintf(form, sizeof form, "token=%s", prof.token);
    code = cloud_post("/v1/friends", form, &r);
    if (code == 200) {
        nfriend_rows = 0;
        for (line = r.body; line && *line && nfriend_rows < MAX_FRIEND_ROWS; ) {
            char *next = strchr(line, '\n'), *f[8];
            if (next) *next++ = 0;
            if (split_fields(line, f, 8) == 8) {
                struct friend_row *p = &friend_rows[nfriend_rows++];
                memset(p, 0, sizeof *p);
                snprintf(p->name, sizeof p->name, "%s", f[0]);
                snprintf(p->avatar, sizeof p->avatar, "%s", avatar_valid(f[1]) ? f[1] : AVATAR_DEFAULT);
                p->rel = !strcmp(f[2], "in") ? REL_IN : !strcmp(f[2], "out") ? REL_OUT : REL_FRIEND;
                p->online = atoi(f[3]) != 0;
                snprintf(p->app_name, sizeof p->app_name, "%s", f[5]);
                p->seen = atoi(f[6]);
                snprintf(p->mc, sizeof p->mc, "%s", f[7]);
            }
            line = next;
        }
    }
    http_free(&r);
    return code;
}

/* What's said under a name, and in what colour. */
void friend_status(const struct friend_row *p, char *out, int n, uint32_t *color)
{
    char ago[40];
    if (p->rel == REL_IN) { snprintf(out, n, "Wants to be your friend"); *color = C_ACCENT_HI; }
    else if (p->rel == REL_OUT) { snprintf(out, n, "Asked, waiting for a yes"); *color = C_DIM; }
    else if (p->online && p->app_name[0]) { snprintf(out, n, "Playing %s", p->app_name); *color = C_TEXT; }
    else if (p->online) { snprintf(out, n, "Online"); *color = C_SUB; }
    else {
        cloud_ago(p->seen, ago, sizeof ago);
        ago[0] = (char)tolower((unsigned char)ago[0]);
        snprintf(out, n, "Last seen %s", ago);
        *color = C_DIM;
    }
}

/* ---------------------------------------------------------------- asking */

static void report(int code, struct http_resp *r, const char *title)
{
    sfx(SFX_ERROR);
    ui_message(title, cloud_error_text(r, code));
}

/* POST /v1/friends/<what> name=...; returns 1 if it worked. */
static int ask(const char *what, const char *name, const char *busy, char *now, int now_size, char *who, int who_size)
{
    struct http_resp r;
    char form[160], en[64], path[48];
    int code;
    if (!cloud_ready("Friends")) return 0;
    net_escape(name, en, sizeof en);
    snprintf(form, sizeof form, "token=%s&name=%s", prof.token, en);
    snprintf(path, sizeof path, "/v1/friends/%s", what);
    ui_busy_begin(busy, name);
    code = cloud_post(path, form, &r);
    ui_busy_end();
    if (code == 200) {
        if (now) kv_get(r.body, "now", now, now_size);
        if (who) kv_get(r.body, "name", who, who_size);
    } else if (code == 401) {
        profile_clear();
        sfx(SFX_ERROR);
        ui_message("Signed out", "Your sign-in expired. Sign in again from Profile.");
    } else {
        report(code, &r, !strcmp(what, "add") ? "Couldn't ask" : "That didn't work");
    }
    http_free(&r);
    return code == 200;
}

static void add_friend(void)
{
    char name[20] = "", now[16], who[20], title[64];
    if (!keyboard("Add a friend", "Their gamertag. They get a request and say yes on their console or PC.", name, 15, KB_TEXT))
        return;
    if (!name[0]) return;
    if (!ask("add", name, "Asking", now, sizeof now, who, sizeof who)) return;
    sfx(SFX_SUCCESS);
    ui_toast_icon(ICON_PERSON_ADD);
    if (!strcmp(now, "friends")) {
        snprintf(title, sizeof title, "You and %s are friends", who);
        ui_toast(title, "They'd asked you already.");
    } else {
        snprintf(title, sizeof title, "Asked %s", who[0] ? who : name);
        ui_toast(title, "They'll see it next time they're on.");
    }
}

static void answer(const struct friend_row *p)
{
    char name[20], title[64], body[200];
    snprintf(name, sizeof name, "%s", p->name);            /* the list is fetched again after */
    if (p->rel == REL_IN) {
        static const char *const opts[] = {"Say yes", "Say no", "Decide later"};
        int c;
        snprintf(title, sizeof title, "%s wants to be friends", name);
        c = ui_dialog(title, "Friends see each other online, what they're playing, and their HostMC servers.", opts, 3);
        if (c == 0 && ask("accept", name, "Saying yes", NULL, 0, NULL, 0)) {
            sfx(SFX_SUCCESS);
            ui_toast_icon(ICON_FRIENDS);
            snprintf(title, sizeof title, "You and %s are friends", name);
            ui_toast(title, NULL);
        } else if (c == 1 && ask("remove", name, "Saying no", NULL, 0, NULL, 0)) {
            ui_toast_icon(ICON_CLOSE);
            ui_toast("Said no", "They can't ask again for a week.");
        }
    } else if (p->rel == REL_OUT) {
        snprintf(title, sizeof title, "Take back your request to %s?", name);
        if (ui_confirm(title, "You can ask again later.", "Take it back", "Keep it") &&
            ask("remove", name, "Taking it back", NULL, 0, NULL, 0)) {
            ui_toast_icon(ICON_CLOSE);
            ui_toast("Request taken back", NULL);
        }
    } else {
        snprintf(title, sizeof title, "Remove %s as a friend?", name);
        snprintf(body, sizeof body, "You'll stop seeing each other online. Either of you can ask again later.");
        if (ui_confirm(title, body, "Remove friend", "Cancel") && ask("remove", name, "Removing", NULL, 0, NULL, 0)) {
            ui_toast_icon(ICON_CLOSE);
            snprintf(title, sizeof title, "Removed %s", name);
            ui_toast(title, NULL);
        }
    }
}

/* ---------------------------------------------------------------- drawing */

#define ROW_H 78

static void draw_row(int x, int y, int w, int index, int selected)
{
    int asz = 54, ty = y + (ROW_H - 8) / 2;
    char line[80];
    uint32_t c = C_SUB;
    if (selected) ui_select_box((float)x, (float)y, (float)w, ROW_H - 8.0f, 10);
    if (index == 0) {
        gfx_circle(x + 16.0f + asz / 2, (float)ty, asz / 2.0f, C_SURFACE2, 255);
        icon_draw(ICON_PERSON_ADD, x + 16.0f + asz / 2, (float)ty, 30, selected ? 0xFFFFFF : C_SUB, C_SURFACE2);
        gfx_str(FONT_BODY_SB, x + 16 + asz + 18, ty - 27, C_TEXT, "Add a friend");
        gfx_str(FONT_SMALL, x + 16 + asz + 18, ty + 3, C_SUB, "Find someone by their gamertag");
        return;
    }
    {
        const struct friend_row *p = &friend_rows[index - 1];
        int tx = x + 16 + asz + 18;
        avatar_draw(p->avatar, x + 16, ty - asz / 2, asz);
        if (p->rel == REL_FRIEND && p->online) {           /* a dot on the avatar, as on the home screen */
            gfx_circle(x + 16.0f + asz - 4, ty + asz / 2.0f - 4, 8, selected ? C_SEL : C_BG, 255);
            gfx_circle(x + 16.0f + asz - 4, ty + asz / 2.0f - 4, 5, C_OK, 255);
        }
        gfx_str_fit(FONT_BODY_SB, tx, ty - 27, C_TEXT, p->name, w - (tx - x) - 16);
        friend_status(p, line, sizeof line, &c);
        gfx_str_fit(FONT_SMALL, tx, ty + 3, c, line, w - (tx - x) - 16);
    }
}

static void draw_detail(int x, int y, int w, int h, int sel)
{
    int pad = 28, ty;
    char line[200];
    uint32_t c;
    ui_panel(x, y, w, h);
    if (sel == 0) {
        icon_draw(ICON_FRIENDS, x + pad + 48.0f, y + pad + 48.0f, 84, C_SUB, C_SURFACE);
        gfx_str(FONT_HEAD, x + pad, y + pad + 118, C_TEXT, "Add a friend");
        gfx_wrap(FONT_BODY, x + pad, y + pad + 118 + gfx_line_h(FONT_HEAD) + 8, w - 2 * pad, 4, C_SUB,
                 "Type their gamertag and they get a request. Once they say yes, you see each other online, "
                 "what you're playing, and each other's HostMC servers to join.", 5);
        return;
    }
    {
        const struct friend_row *p = &friend_rows[sel - 1];
        int asz = 132;
        avatar_draw(p->avatar, x + pad, y + pad, asz);
        gfx_str_fit(FONT_HEAD, x + pad + asz + 24, y + pad + 18, C_TEXT, p->name, w - 2 * pad - asz - 24);
        friend_status(p, line, sizeof line, &c);
        if (p->rel == REL_FRIEND && p->online) gfx_circle(x + pad + asz + 30.0f, y + pad + 18 + gfx_line_h(FONT_HEAD) + 21, 5, C_OK, 255);
        gfx_str_fit(FONT_BODY, x + pad + asz + 24 + (p->rel == REL_FRIEND && p->online ? 18 : 0),
                    y + pad + 18 + gfx_line_h(FONT_HEAD) + 8, c == C_DIM ? C_SUB : c, line, w - 2 * pad - asz - 42);
        ty = y + pad + asz + 30;
        if (p->rel == REL_IN) {
            snprintf(line, sizeof line, "%s asked to be friends. Friends see each other online, what they're playing, and their HostMC servers.", p->name);
            gfx_wrap(FONT_BODY, x + pad, ty, w - 2 * pad, 4, C_SUB, line, 4);
        } else if (p->rel == REL_OUT) {
            snprintf(line, sizeof line, "You asked %s to be friends. They'll see it next time they're on.", p->name);
            gfx_wrap(FONT_BODY, x + pad, ty, w - 2 * pad, 4, C_SUB, line, 4);
        } else if (p->mc[0]) {
            /* Their HostMC server, which people on the internet can reach. The address is what
             * friends type, so all of it shows: a smaller size, or two lines, when it's long. */
            static const int sizes[] = {FONT_LABEL, FONT_BODY_SB, FONT_SMALL};
            int bw = w - 2 * pad - 44, font = FONT_SMALL, lines = 2, bh, k;
            for (k = 0; k < 3; k++)
                if (gfx_str_w(sizes[k], p->mc) <= bw) { font = sizes[k]; lines = 1; break; }
            bh = 132 + lines * (gfx_line_h(font) + 2);
            gfx_fill_rrect((float)(x + pad), (float)ty, (float)(w - 2 * pad), (float)bh, 12, C_SURFACE2, 255);
            icon_draw(ICON_SERVER, x + pad + 34.0f, ty + 36.0f, 34, C_SUB, C_SURFACE2);
            gfx_str(FONT_BODY_SB, x + pad + 64, ty + 20, C_TEXT, "Their Minecraft server");
            gfx_wrap(font, x + pad + 22, ty + 66, bw, 2, C_TEXT, p->mc, 2);
            gfx_wrap(FONT_SMALL, x + pad + 22, ty + 66 + lines * (gfx_line_h(font) + 2) + 10, bw, 2, C_SUB,
                     "To join, in Minecraft: Multiplayer, then Add Server with this address.", 2);
        } else if (!p->online) {
            gfx_wrap(FONT_BODY, x + pad, ty, w - 2 * pad, 4, C_DIM,
                     "When they're on, you'll see it here, with what they're playing.", 3);
        }
    }
}

static void draw(int sel, int first, int signed_out)
{
    int top = HEADER_H + 16, h = SCR_H - top - 70, lw = (SCR_W - 2 * M) * 52 / 100, rx = M + lw + 24;
    int visible = h / ROW_H, i, online = 0, asking = 0;
    char sub[64];
    for (i = 0; i < nfriend_rows; i++) {
        if (friend_rows[i].rel == REL_FRIEND && friend_rows[i].online) online++;
        if (friend_rows[i].rel == REL_IN) asking++;
    }
    if (signed_out) snprintf(sub, sizeof sub, "Sign in to add friends");
    else if (asking) snprintf(sub, sizeof sub, asking == 1 ? "1 friend request" : "%d friend requests", asking);
    else if (online) snprintf(sub, sizeof sub, online == 1 ? "1 friend online" : "%d friends online", online);
    else snprintf(sub, sizeof sub, "See who's on and what they're playing");
    ui_bg();
    ui_header("Friends", sub);

    if (signed_out) {
        int cy = top + h / 2 - 60;
        icon_draw(ICON_FRIENDS, SCR_W / 2.0f, cy - 40.0f, 96, C_SUB, C_BG);
        gfx_str_c(FONT_HEAD, SCR_W / 2, cy + 30, C_TEXT, "Sign in to add friends");
        gfx_str_c(FONT_BODY, SCR_W / 2, cy + 30 + gfx_line_h(FONT_HEAD) + 6, C_SUB,
                  "Friends see each other online and what they're playing.");
        ui_hints("A=Sign in;B=Back");
        return;
    }

    for (i = 0; i < visible && first + i <= nfriend_rows; i++)
        draw_row(M, top + i * ROW_H, lw, first + i, first + i == sel);
    if (first + visible <= nfriend_rows) {                  /* more below */
        snprintf(sub, sizeof sub, "%d more", nfriend_rows + 1 - first - visible);
        gfx_str_r(FONT_SMALL, M + lw, top + visible * ROW_H - 4, C_DIM, sub);
    }
    draw_detail(rx, top, SCR_W - M - rx, h, sel);
    if (sel == 0) ui_hints("A=Add a friend;Y=Refresh;B=Back");
    else if (friend_rows[sel - 1].rel == REL_IN) ui_hints("A=Answer;Y=Refresh;B=Back");
    else if (friend_rows[sel - 1].rel == REL_OUT) ui_hints("A=Take back;Y=Refresh;B=Back");
    else ui_hints("A=Options;Y=Refresh;B=Back");
}

void screen_friends(void)
{
    int sel = 0, first = 0, code;
    uint64_t last = 0;

    if (prof.signed_in) {
        if (!cloud_ready("Friends")) return;
        ui_busy_begin("Getting your friends", NULL);
        code = friends_fetch();
        ui_busy_end();
        if (code != 200) {
            sfx(SFX_ERROR);
            if (code == 401) {
                profile_clear();
                ui_message("Signed out", "Your sign-in expired. Sign in again from Profile.");
            } else {
                ui_message("Couldn't get your friends", cloud_error_text(NULL, code));
                return;
            }
        }
        last = ui_ms();
    }
    ui_fade_in();
    for (;;) {
        int top = HEADER_H + 16, visible = (SCR_H - top - 70) / ROW_H, d, act = 0;
        ui_poll();
        if (PRESSED(b)) { sfx(SFX_BACK); return; }
        if (!prof.signed_in) {
            if (PRESSED(a)) {
                sfx(SFX_SELECT);
                screen_profile();
                if (prof.signed_in && cloud_ready("Friends")) { friends_fetch(); last = ui_ms(); }
                sel = first = 0;
                ui_fade_in();
            }
            draw(0, 0, 1);
            ui_end_frame();
            continue;
        }
        d = ui_nav_y();
        if (d && sel + d >= 0 && sel + d <= nfriend_rows) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(a)) {
            sfx(SFX_SELECT);
            if (sel == 0) add_friend();
            else answer(&friend_rows[sel - 1]);
            act = 1;
        }
        if (act || PRESSED(y) || ui_ms() - last > 30000) {
            char name[20] = "";
            int i;
            if (PRESSED(y)) sfx(SFX_SELECT);
            if (sel > 0 && sel <= nfriend_rows) snprintf(name, sizeof name, "%s", friend_rows[sel - 1].name);
            if (prof.signed_in && net_is_up()) friends_fetch();
            last = ui_ms();
            /* stay on the same person if they're still there */
            if (sel > nfriend_rows) sel = nfriend_rows;
            for (i = 0; name[0] && i < nfriend_rows; i++)
                if (!strcmp(friend_rows[i].name, name)) sel = i + 1;
            if (act) ui_fade_in();
        }
        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;
        draw(sel, first, 0);
        ui_end_frame();
    }
}
