/* Feedback: "Got a recommendation or a bug? Let us know here!" A bug report or an idea, typed on
 * the on-screen keyboard (or a PC's own keyboard), goes to XenithOS Cloud, which passes it on to
 * the people who make XenithOS (server/xenith_cloud.py, POST /v1/feedback). */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "config.h"
#include "gfx.h"
#include "icons.h"
#include "keyboard.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

#define TEXT_MAX 600

static const struct {
    const char *kind, *title, *sub, *ask, *hint, *what;
    int icon;
} choice[2] = {
    {"bug", "Report a bug", "Something broke, froze or didn't work the way it should.",
     "What went wrong?", "What you were doing, and what happened", "bug report", ICON_INFO},
    {"idea", "Suggest something", "An idea, an app or a game you'd like, or a change to XenithOS.",
     "What's your idea?", "Anything that would make XenithOS better", "idea", ICON_MAIL},
};

/* key=value, percent-encoded, after what's there already. */
static void form_add(char *out, int n, const char *key, const char *val)
{
    static const char hex[] = "0123456789ABCDEF";
    int len = (int)strlen(out);
    len += snprintf(out + len, n - len, "%s%s=", len ? "&" : "", key);
    for (; *val && len < n - 4; val++) {
        unsigned char c = (unsigned char)*val;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[len++] = (char)c;
        } else {
            out[len++] = '%';
            out[len++] = hex[c >> 4];
            out[len++] = hex[c & 15];
        }
    }
    out[len] = 0;
}

/* 1 once it's sent. */
static int send(int k, const char *text)
{
    struct http_resp r;
    char form[TEXT_MAX * 3 + 400], platform[40];
    int code;
    if (plat_kind() == PLAT_XBOX) snprintf(platform, sizeof platform, "Xbox 360");
    else snprintf(platform, sizeof platform, "PC, %s", plat_arch()[0] ? plat_arch() : "Linux");
    form[0] = 0;
    form_add(form, sizeof form, "kind", choice[k].kind);
    form_add(form, sizeof form, "text", text);
    form_add(form, sizeof form, "version", XENITH_VERSION);
    form_add(form, sizeof form, "platform", platform);
    if (prof.signed_in) form_add(form, sizeof form, "token", prof.token);
    ui_busy_begin("Sending", NULL);
    code = cloud_post("/v1/feedback", form, &r);
    ui_busy_end();
    if (code == 200) {
        http_free(&r);
        sfx(SFX_SUCCESS);
        ui_toast_icon(ICON_CHECK);
        ui_toast("Sent. Thank you!", k == 0 ? "Your bug report is on its way." : "Your idea is on its way.");
        return 1;
    }
    sfx(SFX_ERROR);
    ui_message("It didn't send", cloud_error_text(&r, code));
    http_free(&r);
    return 0;
}

static void trim(char *s)
{
    char *p = s;
    size_t n;
    while (*p == ' ') p++;
    memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while (n && s[n - 1] == ' ') s[--n] = 0;
}

static char draft[2][TEXT_MAX + 1];            /* kept while XenithOS runs, in case you back out */
static float lift[2];

/* The screen, into the frame; a dialog opened after it shows over it. */
static void draw(int sel)
{
    int i, gap = 24, w = (SCR_W - 2 * M - gap) / 2, h = 230, y0 = HEADER_H + 60;
    ui_bg();
    ui_header("Feedback", "Got a recommendation or a bug? Let us know here!");
    for (i = 0; i < 2; i++) {
        int x = M + i * (w + gap), y;
        float l = lift[i] = ui_ease(lift[i], sel == i ? 1.0f : 0.0f, 0.35f);
        uint32_t fill = gfx_mix(C_SURFACE, C_SEL, (int)(256 * l));
        y = y0 - (int)(l * 6);
        gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 14, fill, 255);
        gfx_stroke_rrect((float)x, (float)y, (float)w, (float)h, 14, 1 + l, gfx_mix(C_LINE, 0xFFFFFF, (int)(235 * l)), 255);
        gfx_circle(x + 72.0f, y + h / 2.0f, 40, i == 0 ? C_ACCENT : C_SURFACE2, 255);
        icon_draw(choice[i].icon, x + 72.0f, y + h / 2.0f, 40, 0xFFFFFF, i == 0 ? C_ACCENT : C_SURFACE2);
        gfx_str_fit(FONT_TITLE, x + 136, y + 46, C_TEXT, choice[i].title, w - 160);
        gfx_wrap(FONT_BODY, x + 136, y + 46 + gfx_line_h(FONT_TITLE) + 8, w - 160, 4, C_SUB, choice[i].sub, 3);
        if (draft[i][0]) gfx_str_fit(FONT_SMALL, x + 136, y + h - 44, C_DIM, "Your draft is kept for you", w - 160);
    }
    gfx_wrap(FONT_SMALL, M, y0 + h + 36, SCR_W - 2 * M, 4, C_SUB,
             "It goes to the people who make XenithOS, with XenithOS's version, what you're playing on, and your "
             "gamertag if you're signed in. Nothing else about you or this machine.", 3);
}

void screen_feedback(void)
{
    int sel = 0;
    ui_fade_in();
    for (;;) {
        int d;
        ui_poll();
        d = ui_nav_x();
        if (d && sel + d >= 0 && sel + d < 2) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(b)) { sfx(SFX_BACK); return; }
        if (PRESSED(a)) {
            sfx(SFX_SELECT);
            if (cloud_ready("Feedback") && keyboard(choice[sel].ask, choice[sel].hint, draft[sel], TEXT_MAX, KB_LINE)) {
                trim(draft[sel]);
                draw(sel);                         /* behind what comes next, not the keyboard */
                if (strlen(draft[sel]) < 3) {
                    if (draft[sel][0]) ui_message("A little more", "Write a few words, so it makes sense to whoever reads it.");
                } else {
                    char q[48];
                    snprintf(q, sizeof q, "Send this %s?", choice[sel].what);
                    if (ui_confirm(q, draft[sel], "Send", "Not yet") && send(sel, draft[sel])) draft[sel][0] = 0;
                }
            }
        }
        draw(sel);
        ui_hints("A=Write it;B=Back");
        ui_end_frame();
    }
}
