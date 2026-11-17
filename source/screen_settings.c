/* SETTINGS: sound, network, system info, controller test, about. */
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "avatar.h"
#include "config.h"
#include "gfx.h"
#include "icons.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "startlog.h"
#include "theme.h"
#include "ui.h"

/* The switches first (S_VOLUME to S_CRASH), then the rows that open something. */
enum { S_VOLUME, S_UI_SOUNDS, S_BOOT, S_CONNECT, S_SHARING, S_CLOUD_SAVES, S_CRASH, S_NETWORK, S_SYSTEM,
       S_CONTROLLER, S_ABOUT, S_SIGN_OUT, S_COUNT };

static const char *const label[S_COUNT] = {
    "Volume", "Interface sounds", "Start-up chime", "Connect at start-up", "Show friends what I'm playing",
    "Cloud saves on this machine", "Send crash reports", "Network", "System info", "Controller test",
    "About XenithOS", "Sign out",
};

/* The rows shown: the ones about a profile only when someone's signed in. */
static int visible_rows(int *rows)
{
    int i, n = 0;
    for (i = 0; i < S_COUNT; i++)
        if (prof.signed_in || (i != S_SHARING && i != S_CLOUD_SAVES && i != S_SIGN_OUT)) rows[n++] = i;
    return n;
}

static void value_of(int i, char *out, int n)
{
    out[0] = 0;
    switch (i) {
    case S_VOLUME: snprintf(out, n, "%d%%", st.volume); break;
    case S_UI_SOUNDS: snprintf(out, n, st.ui_sounds ? "On" : "Off"); break;
    case S_BOOT: snprintf(out, n, st.boot_sound ? "On" : "Off"); break;
    case S_CONNECT: snprintf(out, n, st.auto_connect ? "On" : "Off"); break;
    case S_CRASH: snprintf(out, n, st.crash_reports ? "Always" : "Ask first"); break;
    case S_SHARING: snprintf(out, n, prof.sharing ? "On" : "Off"); break;
    case S_CLOUD_SAVES: snprintf(out, n, st.cloud_saves ? "On" : "Off"); break;
    case S_NETWORK: snprintf(out, n, "%s", net_is_up() ? net_ip() : "Not connected"); break;
    }
}

/* Friends see what you're playing and your HostMC server, or only that you're on. It's kept
 * on XenithOS Cloud, so every machine you sign in on does the same. */
static void toggle_sharing(void)
{
    struct http_resp r;
    char form[120];
    int code, want = !prof.sharing;
    if (!cloud_ready("Settings")) return;
    snprintf(form, sizeof form, "token=%s&on=%d", prof.token, want);
    ui_busy_begin(want ? "Showing friends what you play" : "Keeping what you play to yourself", NULL);
    code = cloud_post("/v1/friends/sharing", form, &r);
    ui_busy_end();
    if (code == 200) {
        prof.sharing = want;
        profile_save();
        sfx(SFX_SELECT);
        ui_toast(want ? "Friends see what you're playing" : "Friends only see that you're on",
                 want ? "And your HostMC server, when people can reach it." : "Not what you're playing, or your HostMC server.");
    } else {
        sfx(SFX_ERROR);
        ui_message("Couldn't change that", cloud_error_text(&r, code));
    }
    http_free(&r);
}

/* ------------------------------------------------------------ system info */

static void system_info(void)
{
    static const char *const names[4] = {"CPU", "GPU", "Memory", "Board"};
    int temps[4] = {0, 0, 0, 0}, have = 0, i;
    uint64_t last = 0;
    ui_fade_in();
    for (;;) {
        int top = HEADER_H + 16, w = (SCR_W - 2 * M - 24) / 2, h = SCR_H - top - 70, y;
        char v[64];
        ui_poll();
        if (PRESSED(b)) { sfx(SFX_BACK); return; }
        if (!last || ui_ms() - last > 1000) { have = plat_temps(temps); last = ui_ms(); }

        ui_bg();
        ui_header("System info", plat_name());
        ui_panel(M, top, w, h);
        gfx_str(FONT_BODY_SB, M + 24, top + 22, C_TEXT, "Temperatures");
        for (i = 0, y = top + 80; i < 4 && have; i++) {
            int t = temps[i], bx = M + 130, bw = w - 130 - 90;
            uint32_t c = t >= 85 ? C_BAD : t >= 70 ? C_WARN : C_OK;
            if (t < 0) continue;            /* no sensor (PCs usually only have the CPU's) */
            gfx_str(FONT_BODY, M + 24, y, C_SUB, names[i]);
            gfx_fill_rrect((float)bx, y + 10.0f, (float)bw, 8, 4, C_SURFACE2, 255);
            gfx_fill_rrect((float)bx, y + 10.0f, bw * (t > 100 ? 100 : t < 0 ? 0 : t) / 100.0f, 8, 4, c, 255);
            snprintf(v, sizeof v, "%d C", t);
            gfx_str_r(FONT_BODY_SB, M + w - 24, y, C_TEXT, v);
            y += 58;
        }
        {
            /* Live numbers for diagnosing speed and sound on the real console. */
            struct audio_stats as;
            const char *k[4] = {"Frame time", "Sound mixer", "Sound buffer", "Dropouts"};
            char vals[4][48];
            int ms = ui_frame_ms();
            audio_get_stats(&as);
            snprintf(vals[0], 48, "%d ms (%d fps)", ms, ms > 16 ? 1000 / (ms ? ms : 1) : 60);
            snprintf(vals[1], 48, "%s", as.threaded ? "Own thread" : as.thread_failed ? "Main thread (helper stopped)" : "Main thread");
            snprintf(vals[2], 48, "%d ms, chip %d ms", as.queue_ms, as.chip_ms);
            snprintf(vals[3], 48, "%d", as.dropouts);
            y = top + 296;
            gfx_str(FONT_BODY_SB, M + 24, y, C_TEXT, "Sound and speed");
            for (i = 0; i < 4; i++) {
                y += 36;
                gfx_str(FONT_BODY, M + 24, y, C_SUB, k[i]);
                gfx_str_r(FONT_BODY_SB, M + w - 24, y, C_TEXT, vals[i]);
            }
        }
        ui_panel(M + w + 24, top, w, h);
        y = top + 22;
        gfx_str(FONT_BODY_SB, M + w + 48, y, C_TEXT, plat_kind() == PLAT_XBOX ? "This console" : "This machine");
        y += 56;
        {
            const char *k[6] = {"Software", "Video output", "Resolution", "Processor", "Network", "XenithOS Cloud"};
            char vals[6][64];
            snprintf(vals[0], 64, "XenithOS %s", XENITH_VERSION);
            snprintf(vals[1], 64, "%s", plat_video_out());
            snprintf(vals[2], 64, "%d x %d", SCR_W, SCR_H);
            snprintf(vals[3], 64, "%s", plat_cpu_name());
            snprintf(vals[4], 64, "%s", net_is_up() ? net_ip() : "Not connected");
            snprintf(vals[5], 64, "%s", !cfg.server[0] ? "Not set up" : !net_is_up() ? "Offline"
                                        : net_lan_route_on() ? "Connected at home"
                                        : !strncmp(cfg.server, "https://", 8) ? "Connected securely" : "Connected");
            for (i = 0; i < 6; i++, y += 44) {
                gfx_str(FONT_BODY, M + w + 48, y, C_SUB, k[i]);
                gfx_str_fit(FONT_BODY_SB, M + 2 * w - gfx_str_w(FONT_BODY_SB, vals[i]) < M + w + 230 ? M + w + 230 : M + 2 * w - gfx_str_w(FONT_BODY_SB, vals[i]), y, C_TEXT, vals[i], M + 2 * w - (M + w + 230));
            }
        }
        {
            /* How long starting up took, to find what makes starts slow. */
            char line[200];
            y += 8;
            gfx_str(FONT_BODY_SB, M + w + 48, y, C_TEXT, "Start-up times");
            y += 38;
            startlog_summary(line, sizeof line);
            gfx_str_fit(FONT_SMALL, M + w + 48, y, C_SUB, "XenithOS", w - 48);
            gfx_str_fit(FONT_SMALL, M + w + 150, y, C_TEXT, line, w - 150);
            y += 30;
            if (startlog_last_app(line, sizeof line)) {
                gfx_str_fit(FONT_SMALL, M + w + 48, y, C_SUB, "Last app", w - 48);
                gfx_str_fit(FONT_SMALL, M + w + 150, y, C_TEXT, line, w - 150);
            }
        }
        ui_hints("B=Back");
        ui_end_frame();
    }
}

/* -------------------------------------------------------- controller test */

static void stick(int cx, int cy, int r, int sx, int sy, int click)
{
    gfx_circle((float)cx, (float)cy, (float)r, C_SURFACE2, 255);
    gfx_ring((float)cx, (float)cy, (float)r, 2, C_LINE, 255);
    gfx_circle(cx + sx * (r - 18) / 32768.0f, cy - sy * (r - 18) / 32768.0f, 16, click ? C_ACCENT : C_TEXT, 255);
}

static void pill(int x, int y, int w, const char *name, int on, uint32_t color)
{
    gfx_fill_rrect((float)x, (float)y, (float)w, 40, 20, on ? color : C_SURFACE2, 255);
    gfx_str_c(FONT_BODY_SB, x + w / 2, y + 20 - gfx_line_h(FONT_BODY_SB) / 2, on ? 0xFFFFFF : C_SUB, name);
}

static void controller_test(void)
{
    uint64_t hold = 0;
    ui_fade_in();
    for (;;) {
        int top = HEADER_H + 30, cx = SCR_W / 2, r = 70;
        ui_poll();
        plat_rumble(cur.lt, cur.rt);
        if (cur.b) {
            if (!hold) hold = ui_ms();
            if (ui_ms() - hold > 1000) { plat_rumble(0, 0); sfx(SFX_BACK); while (cur.b) ui_poll(); return; }
        } else hold = 0;

        ui_bg();
        ui_header("Controller test", "Triggers run the rumble motors. Hold B to leave.");
        /* Triggers and bumpers */
        gfx_fill_rrect((float)M, (float)top, 60, 170, 14, C_SURFACE2, 255);
        if (cur.lt) gfx_fill_rrect((float)M, top + 170 - 170 * cur.lt / 255.0f, 60, 170 * cur.lt / 255.0f, 14, C_ACCENT, 255);
        gfx_str_c(FONT_SMALL, M + 30, top + 180, C_SUB, "LT");
        gfx_fill_rrect(SCR_W - M - 60.0f, (float)top, 60, 170, 14, C_SURFACE2, 255);
        if (cur.rt) gfx_fill_rrect(SCR_W - M - 60.0f, top + 170 - 170 * cur.rt / 255.0f, 60, 170 * cur.rt / 255.0f, 14, C_ACCENT, 255);
        gfx_str_c(FONT_SMALL, SCR_W - M - 30, top + 180, C_SUB, "RT");
        pill(M + 90, top, 110, "LB", cur.lb, C_ACCENT);
        pill(SCR_W - M - 200, top, 110, "RB", cur.rb, C_ACCENT);

        /* Sticks */
        stick(cx - 250, top + 160, r, cur.lx, cur.ly, cur.ls);
        stick(cx + 250, top + 160, r, cur.rx, cur.ry, cur.rs);

        /* D-pad on the left, face buttons on the right, both as diamonds like the pad */
        {
            int lx = cx - 250, rx = cx + 250, vy = top + 340;
            pill(lx - 45, vy - 75, 90, "Up", cur.up, C_ACCENT);
            pill(lx - 140, vy - 25, 90, "Left", cur.left, C_ACCENT);
            pill(lx + 50, vy - 25, 90, "Right", cur.right, C_ACCENT);
            pill(lx - 45, vy + 25, 90, "Down", cur.down, C_ACCENT);
            pill(rx - 35, vy - 75, 70, "Y", cur.y, C_BTN_Y);
            pill(rx - 125, vy - 25, 70, "X", cur.x, C_BTN_X);
            pill(rx + 55, vy - 25, 70, "B", cur.b, C_BTN_B);
            pill(rx - 35, vy + 25, 70, "A", cur.a, C_BTN_A);
        }
        pill(cx - 60, top + 60, 120, "Guide", cur.guide, C_ACCENT);
        pill(cx - 140, top + 110, 110, "Back", cur.back, C_ACCENT);
        pill(cx + 30, top + 110, 110, "Start", cur.start, C_ACCENT);
        if (hold) {
            float f = (ui_ms() - hold) / 1000.0f;
            gfx_arc(SCR_W - M - 30.0f, SCR_H - 34.0f, 12, 4, -1.5708f, -1.5708f + 6.2832f * f, C_BAD, 255);
        }
        ui_hints("B=Hold to leave");
        ui_end_frame();
    }
}

/* ------------------------------------------------------------------ about */

static void about(void)
{
    ui_fade_in();
    for (;;) {
        int y = HEADER_H + 30;
        ui_poll();
        if (PRESSED(b) || PRESSED(a)) { sfx(SFX_BACK); return; }
        ui_bg();
        ui_header("About", NULL);
        ui_brand(M, y, FONT_HEAD, 0);
        y += gfx_line_h(FONT_HEAD) + 20;
        gfx_str(FONT_LABEL, M, y, C_TEXT, "Version " XENITH_VERSION);
        y += 50;
        y += 10 + 30 * gfx_wrap(FONT_BODY, M, y, SCR_W - 2 * M, 6, C_SUB,
            "A homebrew shell for the Xbox 360, made by OwenCosta. Free software under the GNU GPL v3. "
            "Source: github.com/OwenCosta/XenithOS", 3);
        y += 20;
        gfx_str(FONT_BODY_SB, M, y, C_TEXT, "Built with");
        y += 40;
        gfx_wrap(FONT_BODY, M, y, SCR_W - 2 * M, 6, C_SUB,
            "libxenon and the Free60 Project (BSD). The Inter typeface by Rasmus Andersson (SIL Open Font License). "
            "DOOM runs on doomgeneric (GPL v2 or later). Freedoom game data (BSD). Sounds are synthesized by XenithOS itself.", 4);
        ui_hints("B=Back");
        ui_end_frame();
    }
}

/* ----------------------------------------------------------------- screen */

void screen_settings(void)
{
    int sel = 0, changed = 0, i;
    float hl = 0;
    ui_fade_in();
    for (;;) {
        int rows[S_COUNT], n = visible_rows(rows), top = HEADER_H + 16, rowh = 56, w = SCR_W - 2 * M;
        int visible = (SCR_H - top - 70) / rowh, first = 0, d, dx;
        char v[48];
        if (sel >= n) sel = n - 1;
        if (sel >= visible) first = sel - visible + 1;

        ui_poll();
        d = ui_nav_y();
        dx = ui_nav_x();
        if (d && sel + d >= 0 && sel + d < n) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(b)) {
            if (changed) {
                settings_save();
                if (cloud_saves_on() && net_is_up()) {      /* the ones that follow the profile go up */
                    ui_busy_begin("Saving your settings", NULL);
                    cloud_saves_sync("xenithos", 0);
                    ui_busy_end();
                }
            }
            sfx(SFX_BACK);
            return;
        }
        if (dx || PRESSED(a)) {
            switch (rows[sel]) {
            case S_VOLUME:
                if (dx) {
                    st.volume += dx * 10;
                    if (st.volume < 0) st.volume = 0;
                    if (st.volume > 100) st.volume = 100;
                    audio_set_volume(st.volume);
                    sfx(SFX_MOVE);
                    changed = 1;
                }
                break;
            case S_UI_SOUNDS:
                st.ui_sounds = !st.ui_sounds;
                audio_set_ui_sounds(st.ui_sounds);
                sfx(SFX_SELECT);
                changed = 1;
                break;
            case S_BOOT:
                st.boot_sound = !st.boot_sound;
                if (st.boot_sound) sfx(SFX_BOOT); else sfx(SFX_SELECT);
                changed = 1;
                break;
            case S_CONNECT:
                st.auto_connect = !st.auto_connect;
                sfx(SFX_SELECT);
                changed = 1;
                break;
            case S_CRASH:
                /* "Ask first" (as it starts out): XenithOS asks before sending each one
                 * (source/crash_report.c). Turning it to "Always" says what goes first. */
                sfx(SFX_SELECT);
                if (st.crash_reports) {
                    st.crash_reports = 0;
                    changed = 1;
                } else {
                    if (ui_confirm("Send crash reports without asking?",
                                   "When XenithOS or one of its apps crashes, a report of where it stopped goes to the "
                                   "people who make XenithOS, so it can be fixed. It has the version, the app and what "
                                   "it was doing. Never your gamertag, and nothing from your files.",
                                   "Always send them", "Keep asking")) {
                        st.crash_reports = 1;
                        changed = 1;
                    }
                    ui_fade_in();
                }
                break;
            case S_SHARING:
                toggle_sharing();
                ui_fade_in();
                break;
            case S_CLOUD_SAVES:
                st.cloud_saves = !st.cloud_saves;
                settings_save();
                sfx(SFX_SELECT);
                if (st.cloud_saves && cloud_ready("Settings")) {    /* catch up now */
                    ui_busy_begin("Bringing your saves up to date", NULL);
                    cloud_saves_sync(NULL, 1);
                    ui_busy_end();
                    ui_fade_in();
                }
                ui_toast_icon(ICON_CLOUD);
                ui_toast(st.cloud_saves ? "Cloud saves are on" : "Cloud saves are off",
                         st.cloud_saves ? "Your saves and settings follow you to this machine." :
                         "This machine keeps its own. What's on XenithOS Cloud stays there.");
                break;
            default:
                if (!PRESSED(a)) break;
                sfx(SFX_SELECT);
                if (rows[sel] == S_NETWORK) {
                    if (cloud_ready("Network")) {
                        cloud_sync(0);
                        ui_toast("Connected", net_ip());
                    }
                } else if (rows[sel] == S_SYSTEM) system_info();
                else if (rows[sel] == S_CONTROLLER) controller_test();
                else if (rows[sel] == S_ABOUT) about();
                else if (rows[sel] == S_SIGN_OUT) {
                    if (ui_confirm("Sign out?", "Your profile stays on XenithOS Cloud.", "Sign out", "Cancel")) {
                        profile_clear();
                        sel = 0;
                    }
                }
                ui_fade_in();
                break;
            }
        }

        ui_bg();
        ui_header("Settings", "Changes are saved to your USB stick");
        hl = ui_ease(hl, (float)(sel - first), 0.45f);
        ui_select_box((float)M, top + hl * rowh, (float)w, rowh - 6.0f, 10);
        for (i = 0; i < visible && first + i < n; i++) {
            int idx = rows[first + i], y = top + i * rowh, cy = y + (rowh - 6) / 2, s = first + i == sel;
            gfx_str_vc(FONT_BODY_SB, M + 22, cy, s ? 0xFFFFFF : C_TEXT, label[idx]);
            value_of(idx, v, sizeof v);
            if (idx == S_VOLUME) {
                int bw = 220, bx = M + w - 22 - bw - 70;
                gfx_fill_rrect((float)bx, cy - 4.0f, (float)bw, 8, 4, C_LINE, 255);
                gfx_fill_rrect((float)bx, cy - 4.0f, bw * st.volume / 100.0f, 8, 4, C_ACCENT, 255);
                gfx_str_r(FONT_BODY_SB, M + w - 22, cy - gfx_line_h(FONT_BODY_SB) / 2, s ? 0xFFFFFF : C_SUB, v);
            } else if (v[0]) {
                gfx_str_r(FONT_BODY_SB, M + w - 22, cy - gfx_line_h(FONT_BODY_SB) / 2, s ? 0xFFFFFF : C_SUB, v);
            } else {
                gfx_line(M + w - 30.0f, cy - 7.0f, M + w - 24.0f, (float)cy, 3, s ? 0xFFFFFF : C_DIM, 255);
                gfx_line(M + w - 30.0f, cy + 7.0f, M + w - 24.0f, (float)cy, 3, s ? 0xFFFFFF : C_DIM, 255);
            }
        }
        ui_hints(rows[sel] == S_VOLUME ? "LEFT/RIGHT=Change;B=Back" : rows[sel] <= S_CRASH ? "A=Switch;B=Back" : "A=Open;B=Back");
        ui_end_frame();
    }
}
