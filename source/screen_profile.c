/* PROFILE: create an account, sign in, change your avatar, sign out. */
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

static char best[16], rank[16];

static void pretty_date(const char *iso, char *out, int n)
{
    static const char *const mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int y, m, d;
    if (sscanf(iso, "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12) snprintf(out, n, "%s %d, %d", mon[m - 1], d, y);
    else snprintf(out, n, "%s", iso);
}

/* Gamertags: 3-15 characters, letters, digits and single spaces, starting with a letter. */
static const char *check_name(const char *s)
{
    int n = (int)strlen(s), i;
    if (n < 3) return "Gamertags need at least 3 characters.";
    if (n > 15) return "Gamertags can be 15 characters at most.";
    if (!isalpha((unsigned char)s[0])) return "Gamertags have to start with a letter.";
    if (s[n - 1] == ' ') return "Gamertags can't end with a space.";
    for (i = 0; i < n; i++) {
        if (!isalnum((unsigned char)s[i]) && s[i] != ' ') return "Use letters, numbers and spaces only.";
        if (s[i] == ' ' && i && s[i - 1] == ' ') return "Use one space at a time.";
    }
    return NULL;
}

static int ask_name(char *name)
{
    for (;;) {
        const char *err;
        if (!keyboard("Gamertag", "3 to 15 letters or numbers. This is how other players see you.", name, 15, KB_TEXT)) return 0;
        err = check_name(name);
        if (!err) return 1;
        sfx(SFX_ERROR);
        ui_message("Try a different gamertag", err);
    }
}

static int ask_pin(char *pin, const char *hint)
{
    for (;;) {
        if (!keyboard("Passcode", hint, pin, 8, KB_PIN)) return 0;
        if (strlen(pin) >= 4) return 1;
        sfx(SFX_ERROR);
        ui_message("Passcode too short", "Use 4 to 8 digits.");
    }
}

/* Save what the server sent back after creating an account or signing in. */
static void take_session(struct http_resp *r)
{
    char v[80];
    memset(&prof, 0, sizeof prof);
    kv_get(r->body, "name", prof.name, sizeof prof.name);
    kv_get(r->body, "token", v, sizeof v);
    snprintf(prof.token, sizeof prof.token, "%s", v);
    kv_get(r->body, "avatar", prof.avatar, sizeof prof.avatar);
    if (!avatar_valid(prof.avatar)) strcpy(prof.avatar, AVATAR_DEFAULT);
    kv_get(r->body, "joined", prof.joined, sizeof prof.joined);
    prof.signed_in = prof.name[0] && prof.token[0];
    profile_save();
}

static void create_profile(void)
{
    char name[20] = "", pin[12] = "", code[AVATAR_CODE_LEN + 1], form[256], en[64];
    int v[AV_PARTS];
    avatar_random(v, (uint32_t)plat_time_us());
    avatar_encode(v, code);

    for (;;) {
        struct http_resp r;
        int status;
        if (!ask_name(name)) return;
        if (!pin[0] && !ask_pin(pin, "4 to 8 digits. You'll use it to sign in on another console.")) return;
        if (!avatar_editor(code)) {
            if (!ui_confirm("Keep this avatar?", "You can change it later from your profile.", "Keep it", "Cancel sign-up")) return;
        }
        if (!cloud_ready("Profile")) return;
        net_escape(name, en, sizeof en);
        snprintf(form, sizeof form, "name=%s&pin=%s&avatar=%s", en, pin, code);
        ui_busy_begin("Creating your profile", name);
        status = cloud_post("/v1/accounts/create", form, &r);
        ui_busy_end();
        if (status == 200) {
            char hello[48];
            take_session(&r);
            http_free(&r);
            sfx(SFX_SUCCESS);
            snprintf(hello, sizeof hello, "Welcome, %s", prof.name);
            ui_toast(hello, "Your profile is saved on your server.");
            return;
        }
        sfx(SFX_ERROR);
        ui_message("Couldn't create the profile", cloud_error_text(&r, status));
        http_free(&r);
        if (status != 409) return;          /* gamertag taken: pick another */
    }
}

static void sign_in(void)
{
    char name[20] = "", pin[12] = "", form[256], en[64];
    struct http_resp r;
    int status;
    if (!ask_name(name)) return;
    if (!ask_pin(pin, "The passcode you chose when you made the profile.")) return;
    if (!cloud_ready("Profile")) return;
    net_escape(name, en, sizeof en);
    snprintf(form, sizeof form, "name=%s&pin=%s", en, pin);
    ui_busy_begin("Signing in", name);
    status = cloud_post("/v1/accounts/login", form, &r);
    ui_busy_end();
    if (status == 200) {
        char hello[48];
        take_session(&r);
        sfx(SFX_SUCCESS);
        snprintf(hello, sizeof hello, "Welcome back, %s", prof.name);
        ui_toast(hello, NULL);
    } else {
        sfx(SFX_ERROR);
        ui_message("Couldn't sign in", cloud_error_text(&r, status));
    }
    http_free(&r);
}

static void load_stats(void)
{
    struct http_resp r;
    char form[96];
    best[0] = rank[0] = 0;
    if (!prof.signed_in || !cfg.server[0] || !net_is_up()) return;
    snprintf(form, sizeof form, "token=%s", prof.token);
    if (cloud_post("/v1/accounts/me", form, &r) == 200) {
        kv_get(r.body, "volt_best", best, sizeof best);
        kv_get(r.body, "volt_rank", rank, sizeof rank);
        kv_get(r.body, "joined", prof.joined, sizeof prof.joined);
    }
    http_free(&r);
}

static void change_avatar(void)
{
    char code[AVATAR_CODE_LEN + 1], form[160];
    struct http_resp r;
    int status;
    snprintf(code, sizeof code, "%s", prof.avatar);
    if (!avatar_editor(code)) return;
    if (!cloud_ready("Profile")) return;
    snprintf(form, sizeof form, "token=%s&avatar=%s", prof.token, code);
    ui_busy_begin("Saving your avatar", NULL);
    status = cloud_post("/v1/accounts/avatar", form, &r);
    ui_busy_end();
    if (status == 200) {
        snprintf(prof.avatar, sizeof prof.avatar, "%s", code);
        profile_save();
        sfx(SFX_SUCCESS);
        ui_toast("Avatar saved", NULL);
    } else {
        sfx(SFX_ERROR);
        ui_message("Couldn't save your avatar", cloud_error_text(&r, status));
    }
    http_free(&r);
}

static void sign_out(void)
{
    struct http_resp r;
    char form[96];
    if (!ui_confirm("Sign out?", "Your profile stays on the server. Sign in again any time with your passcode.", "Sign out", "Cancel")) return;
    if (cfg.server[0] && net_is_up()) {
        snprintf(form, sizeof form, "token=%s", prof.token);
        cloud_post("/v1/accounts/logout", form, &r);
        http_free(&r);
    }
    profile_clear();
    sfx(SFX_BACK);
}

/* ------------------------------------------------------------------ screen */

struct action { int icon; const char *label; const char *sub; };

static int action_list(int x, int y, int w, const struct action *a, int n, int sel, float hl)
{
    int i, rowh = 84;
    gfx_fill_rrect((float)x, y + hl * rowh, (float)w, rowh - 10.0f, 14, C_ACCENT, 255);
    for (i = 0; i < n; i++) {
        int ry = y + i * rowh, s = i == sel;
        uint32_t bg = s ? C_ACCENT : C_SURFACE;
        if (!s) gfx_fill_rrect((float)x, (float)ry, (float)w, rowh - 10.0f, 14, C_SURFACE, 255);
        icon_draw(a[i].icon, x + 40.0f, ry + (rowh - 10) / 2.0f, 34, s ? 0xFFFFFF : C_ACCENT_HI, bg);
        gfx_str(FONT_BODY_SB, x + 76, ry + 12, s ? 0xFFFFFF : C_TEXT, a[i].label);
        gfx_str(FONT_SMALL, x + 76, ry + 42, s ? 0xDDE8FF : C_SUB, a[i].sub);
    }
    return n * rowh;
}

void screen_profile(void)
{
    int sel = 0;
    float hl = 0;
    if (prof.signed_in) {
        ui_busy_begin("Loading your profile", NULL);
        load_stats();
        ui_busy_end();
    }
    ui_fade_in();

    for (;;) {
        static const struct action guest[] = {
            {ICON_PERSON_ADD, "Create a profile", "Pick a gamertag and build your avatar"},
            {ICON_KEY, "Sign in", "Use a profile you already made"},
        };
        static const struct action member[] = {
            {ICON_PALETTE, "Change avatar", "Edit your look"},
            {ICON_SIGN_OUT, "Sign out", "Play as a guest on this console"},
        };
        const struct action *acts = prof.signed_in ? member : guest;
        int n = 2, top = HEADER_H + 16, size = SCR_H - top - 120, ax, d;
        char line[80], date[32];
        if (size > 360) size = 360;
        ax = M + size + 60;

        ui_poll();
        d = ui_nav_y();
        if (d && sel + d >= 0 && sel + d < n) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(b)) { sfx(SFX_BACK); return; }
        if (PRESSED(a)) {
            sfx(SFX_SELECT);
            if (!prof.signed_in) {
                if (sel == 0) create_profile(); else sign_in();
                if (prof.signed_in) load_stats();
            } else {
                if (sel == 0) change_avatar(); else sign_out();
            }
            sel = 0;
            ui_fade_in();
        }

        ui_bg();
        ui_header("Profile", prof.signed_in ? "Saved on your XenithOS Cloud server" : "You're playing as a guest");
        avatar_draw(prof.avatar, M, top + 10, size);
        hl = ui_ease(hl, (float)sel, 0.45f);
        if (prof.signed_in) {
            int y = top + 10;
            gfx_str(FONT_HEAD, ax, y, C_TEXT, prof.name);
            y += gfx_line_h(FONT_HEAD) + 4;
            if (prof.joined[0]) {
                pretty_date(prof.joined, date, sizeof date);
                snprintf(line, sizeof line, "Joined %s", date);
                gfx_str(FONT_BODY, ax, y, C_SUB, line);
            }
            y += 50;
            ui_panel(ax, y, 220, 92);
            gfx_str(FONT_SMALL, ax + 20, y + 16, C_SUB, "VOLT best");
            gfx_str(FONT_TITLE, ax + 20, y + 40, C_TEXT, best[0] && strcmp(best, "0") ? best : "-");
            ui_panel(ax + 236, y, 220, 92);
            gfx_str(FONT_SMALL, ax + 256, y + 16, C_SUB, "Leaderboard");
            snprintf(line, sizeof line, rank[0] && strcmp(rank, "0") ? "#%s" : "-", rank);
            gfx_str(FONT_TITLE, ax + 256, y + 40, C_TEXT, line);
            action_list(ax, y + 124, SCR_W - M - ax, acts, n, sel, hl);
        } else {
            int y = top + 10;
            gfx_str(FONT_HEAD, ax, y, C_TEXT, "Guest");
            gfx_wrap(FONT_BODY, ax, y + gfx_line_h(FONT_HEAD) + 4, SCR_W - M - ax, 4, C_SUB,
                     "A profile gives you a gamertag, an avatar, and your own spot on the leaderboards. "
                     "It's saved on your server, so it follows you to any console running XenithOS.", 4);
            action_list(ax, y + 190, SCR_W - M - ax, acts, n, sel, hl);
        }
        ui_hints("A=Select;B=Back");
        ui_end_frame();
    }
}
