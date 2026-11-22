/* PROFILE > ACHIEVEMENTS: VOLT's, DOOM's, Tube's and HostMC's, the ones earned and the ones still
 * to get, with how many players have each. The apps earn them (shared/social.c) and XenithOS
 * Cloud keeps them (/v1/achievements); the Profile and Cloud screens show them too. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "config.h"
#include "gfx.h"
#include "icons.h"
#include "net.h"
#include "screens.h"
#include "social.h"
#include "theme.h"
#include "ui.h"

struct achievement ach_list[MAX_ACHIEVEMENTS];
int nach;

static const struct { const char *id, *title; } app_titles[] = {
    {"volt", "VOLT"}, {"doom", "DOOM"}, {"tube", "Tube"}, {"hostmc", "MineNest"},
};

const char *achievement_app(const char *id)
{
    unsigned i;
    for (i = 0; i < sizeof app_titles / sizeof app_titles[0]; i++) {
        size_t n = strlen(app_titles[i].id);
        if (!strncmp(id, app_titles[i].id, n) && (id[n] == '-' || !id[n])) return app_titles[i].title;
    }
    return "XenithOS";
}

int achievements_fetch(const char *player)
{
    struct http_resp r;
    char path[96], en[64], *line;
    int code, i;
    code = cloud_get("/v1/achievements", 16 * 1024, &r);
    if (code == 200) {
        nach = 0;
        for (line = r.body; line && *line && nach < MAX_ACHIEVEMENTS; ) {
            char *next = strchr(line, '\n'), *f[5];
            if (next) *next++ = 0;
            if (split_fields(line, f, 5) == 5) {
                struct achievement *a = &ach_list[nach++];
                memset(a, 0, sizeof *a);
                snprintf(a->id, sizeof a->id, "%s", f[0]);
                snprintf(a->app, sizeof a->app, "%s", f[1]);
                snprintf(a->name, sizeof a->name, "%s", f[2]);
                snprintf(a->what, sizeof a->what, "%s", f[3]);
                a->players = atoi(f[4]);
            }
            line = next;
        }
    }
    http_free(&r);
    if (code != 200 || !player || !player[0]) return code;
    net_escape(player, en, sizeof en);
    snprintf(path, sizeof path, "/v1/players/%s/achievements", en);
    code = cloud_get(path, 16 * 1024, &r);
    if (code == 200) {
        for (line = r.body; line && *line; ) {
            char *next = strchr(line, '\n'), *f[2];
            if (next) *next++ = 0;
            if (split_fields(line, f, 2) == 2)
                for (i = 0; i < nach; i++)
                    if (!strcmp(ach_list[i].id, f[0])) { ach_list[i].earned = 1; ach_list[i].ago = atol(f[1]); }
            line = next;
        }
    }
    http_free(&r);
    return code;
}

int achievements_earned(void)
{
    int i, n = 0;
    for (i = 0; i < nach; i++) n += ach_list[i].earned;
    return n;
}

/* ---------------------------------------------------------------- screen */

/* One app's achievements in a card: each with a trophy, lit once it's earned. With bigger text a
 * row is its name and how it stands, on one line: two lines of the bigger sizes don't leave room
 * for four rows in a card, so what each one asks for is left out there. */
static void draw_app(int x, int y, int w, int h, const char *app)
{
    int i, pad = 22, rowh = 42, ry, got = 0, all = 0, big = gfx_text_large();
    char line[64], ago[40];
    ui_panel(x, y, w, h);
    for (i = 0; i < nach; i++)
        if (!strcmp(ach_list[i].app, app)) { all++; got += ach_list[i].earned; }
    gfx_str(FONT_BODY_SB, x + pad, y + 14, C_TEXT, achievement_app(app));
    snprintf(line, sizeof line, "%d of %d", got, all);
    gfx_str_r(FONT_SMALL, x + w - pad, y + 17, got == all && all ? C_OK : C_SUB, line);
    ry = y + 52;
    for (i = 0; i < nach && ry + rowh <= y + h; i++) {
        const struct achievement *a = &ach_list[i];
        uint32_t ring = a->earned ? C_ACCENT : C_SURFACE2;
        if (strcmp(a->app, app)) continue;
        gfx_circle(x + pad + 15.0f, ry + 17.0f, 15, ring, 255);
        icon_draw(ICON_TROPHY, x + pad + 15.0f, ry + 17.0f, 19, a->earned ? 0xFFFFFF : C_DIM, ring);
        if (!big) gfx_str_fit(FONT_BODY_SB, x + pad + 44, ry - 2, a->earned ? C_TEXT : C_SUB, a->name, w - 2 * pad - 200);
        if (a->earned) {
            cloud_ago(a->ago, ago, sizeof ago);
            ago[0] = (char)tolower((unsigned char)ago[0]);
            snprintf(line, sizeof line, "Earned %s", ago);
        } else {
            snprintf(line, sizeof line, a->players == 1 ? "1 player has it" : "%d players have it", a->players);
        }
        if (big) {
            gfx_str_fit(FONT_BODY_SB, x + pad + 44, ry + 1, a->earned ? C_TEXT : C_SUB, a->name,
                        w - 2 * pad - 60 - gfx_str_w(FONT_SMALL, line));
            gfx_str_r(FONT_SMALL, x + w - pad, ry + 3, a->earned ? C_SUB : C_DIM, line);
        } else {
            gfx_str_r(FONT_SMALL, x + w - pad, ry, a->earned ? C_SUB : C_DIM, line);
            gfx_str_fit(FONT_SMALL, x + pad + 44, ry + 19, a->earned ? C_SUB : C_DIM, a->what, w - 2 * pad - 44);
        }
        ry += rowh;
    }
}

void screen_achievements(void)
{
    int code;
    char sub[128];
    if (!cloud_ready("Achievements")) return;
    social_flush();                          /* any earned while this machine was offline */
    ui_busy_begin("Getting your achievements", NULL);
    code = achievements_fetch(prof.signed_in ? prof.name : NULL);
    ui_busy_end();
    if (code != 200) {
        sfx(SFX_ERROR);
        ui_message("Couldn't get the achievements", cloud_error_text(NULL, code));
        return;
    }
    ui_fade_in();
    for (;;) {
        int top = HEADER_H + 16, gap = 20, w = (SCR_W - 2 * M - gap) / 2, h = (SCR_H - top - 70 - gap) / 2;
        ui_poll();
        if (PRESSED(b)) { sfx(SFX_BACK); return; }
        if (PRESSED(y)) {
            sfx(SFX_SELECT);
            achievements_fetch(prof.signed_in ? prof.name : NULL);
        }
        ui_bg();
        snprintf(sub, sizeof sub, "%d of %d earned, in VOLT, DOOM, Tube and MineNest", achievements_earned(), nach);
        ui_header("Achievements", sub);
        draw_app(M, top, w, h, "volt");
        draw_app(M + w + gap, top, w, h, "doom");
        draw_app(M, top + h + gap, w, h, "tube");
        draw_app(M + w + gap, top + h + gap, w, h, "hostmc");
        ui_hints("Y=Refresh;B=Back");
        ui_end_frame();
    }
}
