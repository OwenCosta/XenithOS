/* XenithOS: a homebrew shell for the Xbox 360, built on libxenon.
 *
 * Boot: the wordmark and start-up chime, while storage and settings load.
 * Home: profile at the top, five tiles (Apps, Store, Cloud, Profile, Settings),
 *       and a "Jump back in" row of installed apps with their thumbnails.
 * Guide or Back on the home screen opens the power menu.
 */
#include <stdio.h>
#include <string.h>

#include "apps.h"
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

/* ------------------------------------------------------------------ boot */

static void boot(void)
{
    int s = SCR_W / 170, bw, bx, by;
    uint64_t start;
    if (s < 4) s = 4;
    bw = GFX_BRAND_W(s);
    bx = (SCR_W - bw) / 2;
    by = SCR_H / 2 - GFX_TEXT_H(s) / 2 - 20;

    ui_bg();
    gfx_present();

    plat_storage_init();
    startlog_mark("storage");
    config_load();
    settings_load();
    profile_load();
    startlog_mark("settings");
    audio_init();
    audio_set_volume(st.volume);
    audio_set_ui_sounds(st.ui_sounds);
    startlog_mark("sounds");
    if (st.boot_sound) sfx(SFX_BOOT);

    start = ui_ms();
    for (;;) {
        uint64_t t = ui_ms() - start;
        float f;
        int a, drift;
        ui_poll();
        if (t > 400 && (PRESSED(a) || PRESSED(start))) break;
        if (t > 2500) break;

        ui_bg();
        f = t < 700 ? t / 700.0f : 1.0f;
        a = (int)(256 * f);
        drift = (int)(14 * (1 - f) * (1 - f));
        gfx_brand(bx, by + drift, s, gfx_mix(C_BG, C_TEXT, a), gfx_mix(C_BG, C_ACCENT, a));
        if (t > 450) {
            float g = t < 1100 ? (t - 450) / 650.0f : 1.0f;
            g = 1 - (1 - g) * (1 - g);
            gfx_fill_rrect(SCR_W / 2.0f - bw * g / 2, (float)(by + GFX_TEXT_H(s) + s * 4), bw * g, 4, 2, C_ACCENT, 255);
        }
        if (t > 900) {
            int va = t < 1400 ? (int)(255 * (t - 900) / 500) : 255;
            gfx_str_a(FONT_SMALL, SCR_W / 2 - gfx_str_w(FONT_SMALL, "Version " XENITH_VERSION) / 2,
                      by + GFX_TEXT_H(s) + s * 4 + 24, C_SUB, va, "Version " XENITH_VERSION);
        }
        ui_end_frame();
    }
}

/* ------------------------------------------------------------------ home */

enum { T_APPS, T_STORE, T_CLOUD, T_PROFILE, T_SETTINGS, T_COUNT };
static const char *const tile_name[T_COUNT] = {"Apps", "Store", "Cloud", "Profile", "Settings"};
static const int tile_icon[T_COUNT] = {ICON_APPS, ICON_STORE, ICON_CLOUD, ICON_PROFILE, ICON_SETTINGS};

static int row, tile, card;
static float lift_t[T_COUNT], lift_c[MAX_APPS];
static const char *status_line = NULL;

static void tile_sub(int t, char *out, int n)
{
    switch (t) {
    case T_APPS: snprintf(out, n, napps == 1 ? "1 installed" : "%d installed", napps); break;
    case T_STORE: snprintf(out, n, "Apps and games"); break;
    case T_CLOUD: snprintf(out, n, net_is_up() ? "Online" : "Offline"); break;
    case T_PROFILE: snprintf(out, n, prof.signed_in ? "Your profile" : "Create or sign in"); break;
    default: snprintf(out, n, "Sound and system"); break;
    }
}

static void draw_home(void)
{
    int asz = 72, tw, th = 210, gap = 16, ty = 150, i;
    int cw = 232, ch = cw * 9 / 16, cy, cgap = 18;
    char sub[48];

    ui_bg();

    /* Top: who's here */
    avatar_draw(prof.avatar, M, 34, asz);
    gfx_str(FONT_TITLE, M + asz + 18, 38, C_TEXT, prof.signed_in ? prof.name : "Guest");
    if (status_line) snprintf(sub, sizeof sub, "%s", status_line);
    else if (net_is_up()) snprintf(sub, sizeof sub, "Online");
    else if (!prof.signed_in) snprintf(sub, sizeof sub, "Press Y to create a profile");
    else snprintf(sub, sizeof sub, "Offline");
    gfx_circle(M + asz + 24.0f, 92, 4, net_is_up() ? C_OK : C_DIM, 255);
    gfx_str(FONT_SMALL, M + asz + 36, 81, C_SUB, sub);
    gfx_brand(SCR_W - M - GFX_BRAND_W(3), 58, 3, C_TEXT, C_ACCENT);

    /* Tiles */
    tw = (SCR_W - 2 * M - gap * (T_COUNT - 1)) / T_COUNT;
    for (i = 0; i < T_COUNT; i++) {
        int sel = row == 0 && tile == i;
        float l = lift_t[i] = ui_ease(lift_t[i], sel ? 1.0f : 0.0f, 0.35f);
        int g = (int)(l * 6), x = M + i * (tw + gap) - g, y = ty - g, w = tw + 2 * g, h = th + 2 * g;
        uint32_t fill = gfx_mix(C_SURFACE, C_ACCENT, (int)(256 * l)), fg = gfx_mix(C_SUB, 0xFFFFFF, (int)(256 * l));
        gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 18, fill, 255);
        if (i == T_PROFILE && prof.signed_in)
            avatar_draw(prof.avatar, x + w / 2 - 40, y + h * 42 / 100 - 40, 80);
        else
            icon_draw(tile_icon[i], x + w / 2.0f, y + h * 0.40f, 62, fg, fill);
        gfx_str(FONT_LABEL, x + 20, y + h - 70, gfx_mix(C_TEXT, 0xFFFFFF, (int)(256 * l)), tile_name[i]);
        tile_sub(i, sub, sizeof sub);
        gfx_str_fit(FONT_SMALL, x + 20, y + h - 38, gfx_mix(C_SUB, 0xE6EEFF, (int)(256 * l)), sub, w - 36);
    }

    /* Jump back in */
    cy = ty + th + 62;
    gfx_str(FONT_BODY_SB, M, cy - 40, C_SUB, napps ? "Jump back in" : "Nothing installed yet");
    if (!napps) {
        int sel = row == 1;
        float l = lift_c[0] = ui_ease(lift_c[0], sel ? 1.0f : 0.0f, 0.35f);
        if (sel) gfx_stroke_rrect(M - 5.0f, cy - 5.0f, cw + 10.0f, ch + 10.0f, 17, 3, 0xFFFFFF, (int)(255 * l));
        gfx_fill_rrect((float)M, (float)cy, (float)cw, (float)ch, 12, C_SURFACE, 255);
        icon_draw(ICON_STORE, M + cw / 2.0f, cy + ch / 2.0f - 14, 40, C_SUB, C_SURFACE);
        gfx_str_c(FONT_BODY_SB, M + cw / 2, cy + ch - 46, C_TEXT, "Open the Store");
    }
    for (i = 0; i < napps; i++) {
        int x = M + i * (cw + cgap), sel = row == 1 && card == i;
        float l = lift_c[i] = ui_ease(lift_c[i], sel ? 1.0f : 0.0f, 0.35f);
        if (x > SCR_W) break;
        app_card(&apps[i], x, cy, cw, ch, sel, l);
        gfx_str_fit(FONT_BODY_SB, x, cy + ch + 14, sel ? C_TEXT : C_SUB, apps[i].name, cw);
    }

    ui_hints(prof.signed_in ? "A=Select;Y=Profile;BACK=Power" : "A=Select;Y=Sign in;BACK=Power");
}

/* Bring the network up after the first home frame, then sync the profile and inbox. */
static void go_online(void)
{
    if (!cfg.server[0] || net_is_up()) return;
    status_line = "Connecting...";
    draw_home();
    ui_end_frame();
    audio_prepare_stall();
    if (net_up()) {
        status_line = NULL;
        cloud_sync(1);
    } else {
        status_line = NULL;
    }
}

/* Returns 1 to leave XenithOS (back to XeLL on the Xbox). */
static int power_menu(void)
{
    draw_home();
    if (plat_kind() == PLAT_XBOX) {
        static const char *const opts[] = {"Restart", "Turn off", "Exit to XeLL", "Cancel"};
        int c = ui_dialog("Power", "XenithOS stays on your USB stick. Starting it again needs the exploit unless you have RGH.", opts, 4);
        if (c == 0) plat_reboot();
        if (c == 1) plat_power_off();
        return c == 2;
    } else {
        const char *opts[4];
        int n = 0, desk = -1, c;
        if (plat_has_desktop()) { desk = n; opts[n++] = "Switch to desktop mode"; }
        opts[n++] = "Restart";
        opts[n++] = "Turn off";
        opts[n++] = "Cancel";
        c = ui_dialog("Power", plat_has_desktop()
                      ? "Desktop mode opens the full Linux desktop. Return to XenithOS from its menu or desktop icon."
                      : "Restart or turn off this machine.", opts, n);
        if (c >= 0 && c == desk) {
            ui_loading_frame("Switching to desktop mode", "One moment");
            plat_switch_to_desktop();
            sfx(SFX_ERROR);
            ui_message("Couldn't switch", "Desktop mode isn't set up on this machine.");
        } else if (c >= 0 && !strcmp(opts[c], "Restart")) {
            plat_reboot();
        } else if (c >= 0 && !strcmp(opts[c], "Turn off")) {
            plat_power_off();
        }
        return 0;
    }
}

static void open_tile(int t)
{
    switch (t) {
    case T_APPS: screen_apps(); break;
    case T_STORE: screen_store(); break;
    case T_CLOUD: screen_cloud(); break;
    case T_PROFILE: screen_profile(); break;
    case T_SETTINGS: screen_settings(); break;
    }
    apps_scan();
    if (card >= napps) card = napps ? napps - 1 : 0;
    ui_fade_in();
}

int main(void)
{
    startlog_mark("start");
    plat_init();
    startlog_mark("video");
    gfx_init();
    ui_init();
    boot();

    apps_scan();
    ui_fade_in();
    draw_home();
    ui_end_frame();
    startlog_mark("boot screen");
    if (st.auto_connect) go_online();

    for (;;) {
        int dx, dy;
        ui_poll();
        dx = ui_nav_x();
        dy = ui_nav_y();

        if (dy > 0 && row == 0) { row = 1; if (card >= napps) card = napps ? napps - 1 : 0; sfx(SFX_MOVE); }
        else if (dy < 0 && row == 1) { row = 0; sfx(SFX_MOVE); }
        if (dx) {
            if (row == 0 && tile + dx >= 0 && tile + dx < T_COUNT) { tile += dx; sfx(SFX_MOVE); }
            else if (row == 1 && card + dx >= 0 && card + dx < napps) { card += dx; sfx(SFX_MOVE); }
        }

        if (PRESSED(a)) {
            sfx(SFX_SELECT);
            if (row == 0) open_tile(tile);
            else if (napps) app_launch(&apps[card]);
            else open_tile(T_STORE);
        } else if (PRESSED(y)) {
            sfx(SFX_SELECT);
            open_tile(T_PROFILE);
        } else if (PRESSED(guide) || PRESSED(back)) {
            sfx(SFX_SELECT);
            if (power_menu()) break;
        }

        draw_home();
        ui_end_frame();
        cloud_poll_inbox();
    }

    plat_rumble(0, 0);
    audio_shutdown();
    plat_audio_stop();
    return 0;   /* back to XeLL */
}
