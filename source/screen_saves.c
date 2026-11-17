/* PROFILE > CLOUD SAVES: what the profile keeps on XenithOS Cloud (DOOM's saves and settings,
 * XenithOS's own settings), how much room it takes, deleting it, and bringing this machine's
 * copies up to date (cloud_saves.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "cloudsave.h"
#include "config.h"
#include "gfx.h"
#include "icons.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

#define ROW_H 72

static int rows[CLOUD_LIST_MAX], nrows;     /* indexes into cloud_list: the files that are there */

static void size_text(long bytes, char *out, int n)
{
    if (bytes >= 1048576) snprintf(out, n, "%.1f MB", bytes / 1048576.0);
    else snprintf(out, n, "%ld KB", (bytes + 1023) / 1024);
}

/* The files in the order apps list them, then any this XenithOS doesn't know. */
static void make_rows(void)
{
    int i, j, k;
    nrows = 0;
    for (i = 0; i < ncloud_apps; i++)
        for (j = 0; j < cloud_apps[i].nfiles; j++)
            for (k = 0; k < ncloud_list; k++)
                if (!cloud_list[k].gone && !strcmp(cloud_list[k].app, cloud_apps[i].id)
                    && !strcmp(cloud_list[k].name, cloud_apps[i].files[j].name))
                    rows[nrows++] = k;
    for (k = 0; k < ncloud_list; k++)
        if (!cloud_list[k].gone && !cloud_app_find(cloud_list[k].app)) rows[nrows++] = k;
}

static void title_of(const struct cloud_entry *e, char *out, int n)
{
    const struct cloud_app *a = cloud_app_find(e->app);
    int j;
    for (j = 0; a && j < a->nfiles; j++)
        if (!strcmp(a->files[j].name, e->name)) {
            snprintf(out, n, "%s %s", a->title, a->files[j].title);     /* "DOOM save slot 1" */
            return;
        }
    snprintf(out, n, "%s, %s", e->app, e->name);
}

static int refresh(int busy)
{
    int code;
    if (busy) ui_busy_begin("Looking at your cloud saves", NULL);
    code = cloud_saves_list(NULL);
    if (busy) ui_busy_end();
    if (code == 200) make_rows();
    return code;
}

static void delete_one(const struct cloud_entry *e)
{
    char title[96], form[200];
    struct http_resp r;
    int code;
    title_of(e, title, sizeof title);
    if (!ui_confirm("Delete it from XenithOS Cloud?", "The copies on your machines stay, and only go up again if they change.",
                    "Delete it", "Cancel")) return;
    if (!cloud_ready("Cloud saves")) return;
    snprintf(form, sizeof form, "token=%s&app=%s&name=%s", prof.token, e->app, e->name);
    ui_busy_begin("Deleting", title);
    code = cloud_post("/v1/saves/delete", form, &r);
    ui_busy_end();
    if (code == 200) {
        sfx(SFX_SELECT);
        ui_toast_icon(ICON_CLOUD);
        ui_toast("Deleted from XenithOS Cloud", title);
    } else {
        sfx(SFX_ERROR);
        ui_message("Couldn't delete it", cloud_error_text(&r, code));
    }
    http_free(&r);
}

static void delete_all(void)
{
    char form[120];
    struct http_resp r;
    int code;
    if (!ui_confirm("Delete all your cloud saves?", "Everything is deleted from XenithOS Cloud. The copies on your machines "
                    "stay, and only go up again if they change.", "Delete them all", "Cancel")) return;
    if (!cloud_ready("Cloud saves")) return;
    snprintf(form, sizeof form, "token=%s&all=1", prof.token);
    ui_busy_begin("Deleting your cloud saves", NULL);
    code = cloud_post("/v1/saves/delete", form, &r);
    ui_busy_end();
    if (code == 200) {
        sfx(SFX_SELECT);
        ui_toast_icon(ICON_CLOUD);
        ui_toast("Your cloud saves are deleted", "The copies on your machines stay.");
    } else {
        sfx(SFX_ERROR);
        ui_message("Couldn't delete them", cloud_error_text(&r, code));
    }
    http_free(&r);
}

static void draw(int sel, int first)
{
    int top = HEADER_H + 16, h = SCR_H - top - 70, lw = (SCR_W - 2 * M) * 60 / 100, rx = M + lw + 24;
    int rw = SCR_W - M - rx, visible = (h - 96) / ROW_H, i, pad = 24, y;
    char a[32], b[32], line[160], ago[40];
    float f = cloud_limit > 0 ? (float)cloud_used / cloud_limit : 0;

    ui_bg();
    ui_header("Cloud saves", "Saves and settings that follow you to every machine");

    /* How much room they take */
    size_text(cloud_used, a, sizeof a);
    size_text(cloud_limit, b, sizeof b);
    snprintf(line, sizeof line, "%s of %s", a, b);
    gfx_str(FONT_BODY_SB, M, top + 4, C_TEXT, "On XenithOS Cloud");
    gfx_str_r(FONT_BODY_SB, M + lw, top + 4, C_SUB, line);
    gfx_fill_rrect((float)M, top + 44.0f, (float)lw, 8, 4, C_SURFACE2, 255);
    if (f > 0) gfx_fill_rrect((float)M, top + 44.0f, lw * (f > 1 ? 1 : f < 0.01f ? 0.01f : f), 8, 4, f > 0.9f ? C_WARN : C_ACCENT, 255);

    y = top + 76;
    if (!nrows) {
        gfx_wrap(FONT_BODY, M, y + 10, lw, 4, C_SUB, "Nothing here yet. DOOM's saves and settings, and XenithOS's own "
                 "settings, come here as you play, so they're the same on your Xbox and your PCs.", 4);
    }
    for (i = 0; i < visible && first + i < nrows; i++) {
        const struct cloud_entry *e = &cloud_list[rows[first + i]];
        int ry = y + i * ROW_H, s = first + i == sel;
        char title[96];
        if (s) ui_select_box((float)M, (float)ry, (float)lw, ROW_H - 8.0f, 10);
        icon_draw(e->name[0] && strstr(e->name, ".dsg") ? ICON_PLAY : ICON_SETTINGS, M + 34.0f, ry + (ROW_H - 8) / 2.0f, 30,
                  s ? 0xFFFFFF : C_SUB, s ? C_SEL : C_BG);
        title_of(e, title, sizeof title);
        if (e->label[0]) {
            snprintf(line, sizeof line, "%s: %s", title, e->label);
            gfx_str_fit(FONT_BODY_SB, M + 68, ry + 8, C_TEXT, line, lw - 84);
        } else {
            gfx_str_fit(FONT_BODY_SB, M + 68, ry + 8, C_TEXT, title, lw - 84);
        }
        size_text(e->size, a, sizeof a);
        cloud_ago(e->age, ago, sizeof ago);
        ago[0] = (char)(ago[0] >= 'A' && ago[0] <= 'Z' ? ago[0] - 'A' + 'a' : ago[0]);
        snprintf(line, sizeof line, "%s, from %s, %s", a, e->from[0] ? e->from : "another machine", ago);
        gfx_str_fit(FONT_SMALL, M + 68, ry + 38, C_SUB, line, lw - 84);
    }
    if (first + visible < nrows) {
        snprintf(line, sizeof line, "%d more", nrows - first - visible);
        gfx_str_r(FONT_SMALL, M + lw, y + visible * ROW_H - 4, C_DIM, line);
    }

    /* What wins, and this machine */
    ui_panel(rx, top, rw, h);
    icon_draw(ICON_CLOUD, rx + pad + 16.0f, top + pad + 15.0f, 28, C_ACCENT_HI, C_SURFACE);
    gfx_str(FONT_BODY_SB, rx + pad + 40, top + pad + 2, C_TEXT, "How they stay in step");
    y = top + pad + 50;
    y += (gfx_line_h(FONT_SMALL) + 3) * gfx_wrap(FONT_SMALL, rx + pad, y, rw - 2 * pad, 3, C_SUB,
        "XenithOS brings them up to date when it starts, and before and after DOOM. What changed last, "
        "on whichever machine, is what you get.", 5) + 14;
    y += (gfx_line_h(FONT_SMALL) + 3) * gfx_wrap(FONT_SMALL, rx + pad, y, rw - 2 * pad, 3, C_SUB,
        "If a save changes on two machines before they match up, XenithOS asks which one to keep. Nothing is "
        "replaced without asking.", 5) + 14;
    {
        /* The last one only where it fits above this machine's line (with bigger text it doesn't) */
        static const char gone[] = "Deleting here leaves the copies on your machines.";
        if (y + gfx_wrap_lines(FONT_SMALL, rw - 2 * pad, gone, 3) * (gfx_line_h(FONT_SMALL) + 3) <= top + h - pad - 30)
            gfx_wrap(FONT_SMALL, rx + pad, y, rw - 2 * pad, 3, C_SUB, gone, 3);
    }
    snprintf(line, sizeof line, st.cloud_saves ? "On for this %s" : "Off for this %s, in Settings",
             plat_kind() == PLAT_XBOX ? "Xbox" : "PC");
    gfx_circle(rx + pad + 5.0f, top + h - pad - 11.0f, 4, st.cloud_saves ? C_OK : C_DIM, 255);
    gfx_str(FONT_SMALL, rx + pad + 18, top + h - pad - 22, C_TEXT, line);

    if (nrows) ui_hints("A=Delete;X=Bring up to date;Y=Delete all;B=Back");
    else ui_hints("X=Bring up to date;B=Back");
}

void screen_saves(void)
{
    int sel = 0, first = 0, code;
    if (!prof.signed_in) return;
    if (!cloud_ready("Cloud saves")) return;
    code = refresh(1);
    if (code != 200) {
        sfx(SFX_ERROR);
        ui_message("Couldn't look at your cloud saves", cloud_error_text(NULL, code));
        return;
    }
    ui_fade_in();
    for (;;) {
        int top = HEADER_H + 16, h = SCR_H - top - 70, visible = (h - 96) / ROW_H, d, again = 0;
        ui_poll();
        if (PRESSED(b)) { sfx(SFX_BACK); return; }
        d = ui_nav_y();
        if (d && sel + d >= 0 && sel + d < nrows) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(a) && nrows) { sfx(SFX_SELECT); delete_one(&cloud_list[rows[sel]]); again = 1; }
        else if (PRESSED(y) && nrows) { sfx(SFX_SELECT); delete_all(); again = 1; }
        else if (PRESSED(x)) {
            sfx(SFX_SELECT);
            if (!st.cloud_saves) {
                ui_message("Cloud saves are off here", "Turn them on in Settings to bring this machine's saves up to date.");
            } else if (cloud_ready("Cloud saves")) {
                int ok;
                ui_busy_begin("Bringing your saves up to date", NULL);
                ok = cloud_saves_sync(NULL, 1);
                ui_busy_end();
                if (ok) { ui_toast_icon(ICON_CLOUD); ui_toast("Up to date", "This machine and XenithOS Cloud match."); }
            }
            again = 1;
        }
        if (again) {
            if (net_is_up()) refresh(0);
            if (sel >= nrows) sel = nrows ? nrows - 1 : 0;
            ui_fade_in();
        }
        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;
        draw(sel, first);
        ui_end_frame();
    }
}
