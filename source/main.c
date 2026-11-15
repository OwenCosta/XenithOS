/* XenithOS: a homebrew shell for the Xbox 360, built on libxenon.
 *
 * Boot: the wordmark and start-up chime, while storage and settings load.
 * Home: profile at the top, five tiles (Apps, Store, Cloud, Profile, Settings),
 *       and a "Jump back in" row of installed apps with their thumbnails.
 * Guide or Back on the home screen opens the power menu.
 */
#include <stdio.h>
#include <string.h>

#include <strings.h>

#include "apps.h"
#include "audio.h"
#include "avatar.h"
#include "config.h"
#include "gfx.h"
#include "handoff.h"
#include "icons.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "startlog.h"
#include "theme.h"
#include "ui.h"

/* ------------------------------------------------------------------ boot */

/* 0..1 over [from, from + len) milliseconds */
static float span(float t, float from, float len)
{
    float f = (t - from) / len;
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

static float ease_out(float f) { return 1 - (1 - f) * (1 - f) * (1 - f); }
static float ease_in(float f) { return f * f; }

/* One frame of the start-up, t milliseconds in. Four streaks of light spiral in from the edges
 * and become the XenithOS mark: its arms shoot out of the middle, the red point lands with the
 * chime and a ring rolls out from it, then the mark moves aside for the name. Everything is
 * flat (solid shapes, no glows), in the theme's colours. */
static void boot_frame(float t)
{
    float size = SCR_H * 0.24f, cy = SCR_H / 2.0f - 16, mark_w = size * 0.84f;
    int name_w = gfx_str_w(FONT_BIG, "XenithOS"), gap = (int)(size * 0.28f), i;
    float group = mark_w + gap + name_w, final_cx = (SCR_W - group) / 2 + mark_w / 2;
    float slide = ease_out(span(t, 1500, 650)), cx = SCR_W / 2.0f + (final_cx - SCR_W / 2.0f) * slide;
    float arms = ease_out(span(t, 900, 320)), dot = 0;

    /* 1. the streaks: each spins in on its own spiral, shrinking and thinning as it closes */
    for (i = 0; i < 4 && t < 1150; i++) {
        float p = span(t, 60.0f + i * 70.0f, 980.0f - i * 70.0f), e = ease_in(p);
        float r = size * (2.4f - 2.3f * e), a0 = i * 1.5708f + 0.4f + p * 6.6f, len = 0.5f + 1.1f * p;
        int alpha = (int)(255 * (p < 0.15f ? p / 0.15f : t > 1000 ? (1150 - t) / 150.0f : 1));
        if (p <= 0) continue;
        gfx_arc(cx, cy, r, size * (0.07f - 0.045f * e), a0, a0 + len, i == 2 ? C_ACCENT : C_TEXT, alpha);
    }
    /* 2. the mark's arms come out of the middle */
    if (arms > 0) {
        /* 3. the red point lands with a little overshoot */
        if (t >= 1080) {
            float q = span(t, 1080, 380);
            dot = q < 0.6f ? 1.35f * ease_out(q / 0.6f) : 1.35f - 0.35f * ease_out((q - 0.6f) / 0.4f);
        }
        ui_logo(cx, cy, size, arms, dot, 255);
    }
    /* and a ring rolls out from it */
    if (t >= 1080 && t < 1900) {
        float q = span(t, 1080, 820);
        gfx_ring(cx, cy, size * (0.12f + 1.25f * ease_out(q)), 2 + size * 0.03f * (1 - q), C_TEXT, (int)(150 * (1 - q)));
    }
    /* 4. the name comes in beside it, then the version */
    if (slide > 0) {
        int x = (int)(cx + mark_w / 2 + gap + 24 * (1 - slide)), ty = (int)cy - font_table[FONT_BIG].ascent + gfx_cap_h(FONT_BIG) / 2;
        gfx_str_a(FONT_BIG, x, ty, C_TEXT, (int)(255 * slide), "XenithOS");
    }
    if (t > 2100) {
        int va = (int)(255 * span(t, 2100, 400));
        gfx_str_a(FONT_SMALL, SCR_W / 2 - gfx_str_w(FONT_SMALL, "Version " XENITH_VERSION) / 2,
                  SCR_H - 64, C_DIM, va, "Version " XENITH_VERSION);
    }
}

static void boot(void)
{
    uint64_t start;
    int chimed = 0;
    /* Coming back from an app: it left "XenithOS / Going back" on screen, so carry on from
     * there and go straight home, without the logo and the chime. */
    int back = handoff_get("from") != NULL;

    if (back) {
        ui_loading_frame("XenithOS", "Loading");
    } else {
        ui_bg();
        gfx_present();
    }

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
    if (back) return;

    /* The start-up animation (boot_frame): the XenithOS logo forms out of four streaks and the
     * name comes in beside it. A or Start skips the rest. */
    start = ui_ms();
    for (;;) {
        uint64_t t = ui_ms() - start;
        ui_poll();
        if (t > 400 && (PRESSED(a) || PRESSED(start))) break;
        if (t > 3200) break;
        if (!chimed && t >= 1080) {                  /* the chime lands with the red point */
            if (st.boot_sound) sfx(SFX_BOOT);
            chimed = 1;
        }
        ui_bg();
        boot_frame((float)t);
        ui_end_frame();
    }
}

/* ------------------------------------------------------------------ home */

enum { T_APPS, T_STORE, T_CLOUD, T_PROFILE, T_SETTINGS, T_COUNT };
static const char *const tile_name[T_COUNT] = {"Apps", "Store", "Cloud", "Profile", "Settings"};
static const int tile_icon[T_COUNT] = {ICON_APPS, ICON_STORE, ICON_CLOUD, ICON_PROFILE, ICON_SETTINGS};

static int row, tile, card;
static float lift_t[T_COUNT], lift_c[MAX_APPS];
static int card_first;                  /* the first card fully in view in "Jump back in" */
static float card_scroll = -1;          /* how far that row has slid, in pixels (eased); -1: not drawn yet */
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
    ui_brand(SCR_W - M - ui_brand(0, 0, FONT_LABEL, 1), 46, FONT_LABEL, 0);

    /* Tiles */
    tw = (SCR_W - 2 * M - gap * (T_COUNT - 1)) / T_COUNT;
    for (i = 0; i < T_COUNT; i++) {
        int sel = row == 0 && tile == i;
        float l = lift_t[i] = ui_ease(lift_t[i], sel ? 1.0f : 0.0f, 0.35f);
        int x = M + i * (tw + gap), y = ty - (int)(l * 8), w = tw, h = th;
        uint32_t fill = gfx_mix(C_SURFACE, C_SEL, (int)(256 * l)), fg = gfx_mix(C_SUB, 0xFFFFFF, (int)(256 * l));
        gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 12, fill, 255);
        gfx_stroke_rrect((float)x, (float)y, (float)w, (float)h, 12, 1 + l, gfx_mix(C_LINE, 0xFFFFFF, (int)(235 * l)), 255);
        if (i == T_PROFILE && prof.signed_in)
            avatar_draw(prof.avatar, x + w / 2 - 40, y + h * 42 / 100 - 40, 80);
        else
            icon_draw(tile_icon[i], x + w / 2.0f, y + h * 0.40f, 62, fg, fill);
        gfx_str(FONT_LABEL, x + 20, y + h - 70, C_TEXT, tile_name[i]);
        tile_sub(i, sub, sizeof sub);
        gfx_str_fit(FONT_SMALL, x + 20, y + h - 38, C_SUB, sub, w - 36);
    }

    /* Jump back in */
    cy = ty + th + 62;
    gfx_str(FONT_BODY_SB, M, cy - 40, C_TEXT, napps ? "Jump back in" : "Nothing installed yet");
    if (napps) gfx_str(FONT_SMALL, M + gfx_str_w(FONT_BODY_SB, "Jump back in") + 14, cy - 37, C_SUB, "See all in Apps");
    if (!napps) {
        int sel = row == 1;
        float l = lift_c[0] = ui_ease(lift_c[0], sel ? 1.0f : 0.0f, 0.35f);
        if (sel) gfx_stroke_rrect(M - 5.0f, cy - 5.0f, cw + 10.0f, ch + 10.0f, 17, 3, 0xFFFFFF, (int)(255 * l));
        gfx_fill_rrect((float)M, (float)cy, (float)cw, (float)ch, 12, C_SURFACE, 255);
        icon_draw(ICON_STORE, M + cw / 2.0f, cy + ch / 2.0f - 14, 40, C_SUB, C_SURFACE);
        gfx_str_c(FONT_BODY_SB, M + cw / 2, cy + ch - 46, C_TEXT, "Open the Store");
    }
    /* The row slides to keep the selected card in view, and is cut at the side margins. Each
     * card is like the streaming site's posters: the picture, then the name and kind inside it. */
    if (napps) {
        int fit = (SCR_W - 2 * M + cgap) / (cw + cgap), clip[4];
        const char *running = plat_running();
        if (fit < 1) fit = 1;
        if (card < card_first) card_first = card;
        if (card >= card_first + fit) card_first = card - fit + 1;
        if (card_first > napps - fit) card_first = napps - fit > 0 ? napps - fit : 0;
        card_scroll = card_scroll < 0 ? (float)(card_first * (cw + cgap)) : ui_ease(card_scroll, (float)(card_first * (cw + cgap)), 0.3f);
        gfx_get_clip(clip);
        gfx_clip_intersect(M - 14, cy - 14, SCR_W - 2 * M + 28, ch + 80);
        for (i = 0; i < napps; i++) {
            int x = M + i * (cw + cgap) - (int)(card_scroll + 0.5f), sel = row == 1 && card == i, ly;
            float l = lift_c[i] = ui_ease(lift_c[i], sel ? 1.0f : 0.0f, 0.35f);
            if (x + cw + 14 < M - 14) continue;
            if (x > SCR_W - M + 14) break;
            ly = cy - (int)(l * 8);
            gfx_fill_rrect((float)x, (float)ly, (float)cw, ch + 58.0f, 10, C_SURFACE, 255);
            gfx_stroke_rrect((float)x, (float)ly, (float)cw, ch + 58.0f, 10, 1 + l, gfx_mix(C_LINE, 0xFFFFFF, (int)(235 * l)), 255);
            app_card(&apps[i], x, ly, cw, ch, 0, 0);
            gfx_str_fit(FONT_BODY_SB, x + 12, ly + ch + 8, C_TEXT, apps[i].name, cw - 24);
            if (running && !strcmp(running, apps[i].path)) {     /* left running from the guide */
                gfx_circle(x + 16.0f, ly + ch + 41.0f, 4, C_OK, 255);
                gfx_str(FONT_SMALL, x + 26, ly + ch + 32, C_TEXT, "Running");
            } else {
                gfx_str(FONT_SMALL, x + 12, ly + ch + 32, C_SUB, apps[i].version[0] ? apps[i].version : "App");
            }
        }
        gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
        if (napps > fit) {               /* where in the row you are */
            char where[24];
            snprintf(where, sizeof where, "%d of %d", card + 1, napps);
            if (row == 1) gfx_str_r(FONT_SMALL, SCR_W - M, cy - 37, C_DIM, where);
        }
    }

    ui_hints(prof.signed_in ? "A=Select;Y=Profile;BACK=Power" : "A=Select;Y=Sign in;BACK=Power");
}

/* Bring the network up after the first home frame, then sync the profile and inbox. */
static void go_online(void)
{
    if (!cfg.server[0]) return;
    if (!net_is_up()) {                     /* the Xbox asks for an address; a PC already has one */
        status_line = "Connecting...";
        draw_home();
        ui_end_frame();
        audio_prepare_stall();
        if (!net_up()) { status_line = NULL; return; }
        status_line = NULL;
    }
    cloud_route_check();
    cloud_sync(1);
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

int main(int argc, char **argv)
{
    const char *from;
    int i;
    /* "xenithos --version" (PCs): one line and out, for scripts such as XenithOS Desktop's
     * updater. It comes before anything that opens a window or starts X. */
    if (argc > 1 && argv && argv[1] && !strcmp(argv[1], "--version")) {
        printf("XenithOS %s\n", XENITH_VERSION);
        return 0;
    }
    handoff_args(argc, argv);
    startlog_mark("start");
    plat_init();
    startlog_mark("video");
    gfx_init();
    ui_init();
    plat_set_guide(guide_run);
    boot();

    apps_scan();
    from = handoff_get("from");
    for (i = 0; from && i < napps; i++)
        if (!strcasecmp(apps[i].id, from)) {     /* back from this app: it's the one selected */
            row = 1;
            card = i;
        }
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
            else if (napps) {
                app_launch(&apps[card]);
                apps_scan();             /* a PC's app may have changed what's installed (Steam games) */
                card = 0;                /* what was just played is first now */
            } else open_tile(T_STORE);
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
