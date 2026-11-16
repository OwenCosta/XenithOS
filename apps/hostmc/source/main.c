/* HOSTMC: a Minecraft: Java Edition server on a XenithOS PC.
 *
 * Start it, and friends join at this PC's address on the home network, Minecraft on this same
 * PC (Prism Launcher) joins at localhost, and with playit.gg turned on, anyone can join from
 * anywhere. The pages, down the left:
 *   Server    its state, where to join, who's on, start and stop
 *   Console   what the server says, and commands typed to it
 *   Players   who's on (operator, kick, ban), the whitelist and the operators
 *   Settings  server.properties (message, game mode, difficulty...) and its memory
 *   Version   Minecraft or Paper, the latest release or an earlier one
 *   World     backups, putting one back, a new world
 *   Internet  playit.gg
 *
 * The server keeps running after HostMC closes (server.c), so the user can go and play on it,
 * and HostMC picks it up again next time. Before the first start the user agrees to Minecraft's
 * EULA themselves; HostMC never agrees to it for them. Everything lives in hostmc/ in XenithOS's
 * data folder. PCs only: an Xbox can't run the server.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "config.h"
#include "gfx.h"
#include "handoff.h"
#include "hostmc.h"
#include "icons.h"
#include "keyboard.h"
#include "platform.h"
#include "startlog.h"
#include "theme.h"
#include "ui.h"

static char root[700];

const char *hm_root(void)
{
    return root;
}

void hm_frame(void)
{
    sv_poll();
    pi_poll();
    vps_poll();
}

/* ------------------------------------------------------------------ rows */

/* Each page is a list of rows, built again every frame from the state. */
enum { R_STATUS, R_BUTTON, R_ITEM, R_HEAD, R_INFO, R_TEXT, R_NAMES, R_QR, R_GAP };

struct row {
    int kind, id, cycle, primary;
    char label[64];
    char value[220];
    uint32_t color;
};

#define MAX_ROWS 64
static struct row rows[MAX_ROWS];
static int nrows;

static struct row *add(int kind, int id, const char *label, const char *value)
{
    static struct row spare;
    struct row *r = nrows < MAX_ROWS ? &rows[nrows++] : &spare;
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->id = id;
    snprintf(r->label, sizeof r->label, "%s", label ? label : "");
    snprintf(r->value, sizeof r->value, "%s", value ? value : "");
    return r;
}

static int selectable(const struct row *r)
{
    return r->kind == R_BUTTON || r->kind == R_ITEM;
}

/* Row ids: what A (or left and right) does. */
enum {
    A_START = 1, A_STOP, A_RESTART, A_FORCE,
    A_WL_ADD, S_MOTD, S_MODE, S_DIFF, S_MAX, S_WL, S_ONLINE, S_SEED, S_RAM, S_PORT,
    V_TYPE, V_VERSION, V_UPDATE, V_CHECK, V_INSTALL,
    W_BACKUP, W_RESTORE, W_RESET,
    N_ON, N_OFF, N_FORGET,
    N_VPS, N_VPS_CHECK, N_VPS_STEPS, N_VPS_EDIT, N_VPS_FORGET, N_VPS_OFF,
    ID_PLAYER = 100, ID_WL = 200, ID_OP = 300,
};

/* ----------------------------------------------------------------- state */

enum { P_SERVER, P_CONSOLE, P_PLAYERS, P_SETTINGS, P_VERSION, P_WORLD, P_INTERNET, P_COUNT };
static const char *const page_names[P_COUNT] = {"Server", "Console", "Players", "Settings", "Version", "World", "Internet"};
static const int page_icons[P_COUNT] = {ICON_SERVER, ICON_SYSTEM, ICON_PROFILE, ICON_SETTINGS, ICON_DOWNLOAD, ICON_HOME, ICON_NETWORK};

static int page, in_page, sel_id, scroll_y, console_back, needs_restart, force_update;
static char footer[260];               /* under a page's rows, not scrolling with them */
static float menu_hl, sel_y_hl = -1;
static struct version_list versions[2];
static int versions_ok[2];
static struct backup_list backups;
static char backup_notes[MAX_BACKUPS][80];
static uint64_t lists_at;
static char wl_names[32][17], op_names[32][17];
static int nwl, nop;

static const char *const modes[4] = {"survival", "creative", "adventure", "spectator"};
static const char *const mode_names[4] = {"Survival", "Creative", "Adventure", "Spectator"};
static const char *const diffs[4] = {"peaceful", "easy", "normal", "hard"};
static const char *const diff_names[4] = {"Peaceful", "Easy", "Normal", "Hard"};
static const int ram_steps[] = {1024, 2048, 3072, 4096, 5120, 6144, 8192, 10240, 12288, 16384, 24576, 32768};
#define NRAM ((int)(sizeof ram_steps / sizeof ram_steps[0]))

static int index_of(const char *const *list, int n, const char *v, int fallback)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(list[i], v)) return i;
    return fallback;
}

static int prop_bool(const char *key, int fallback)
{
    char v[16];
    if (!props_get(key, v, sizeof v) || !v[0]) return fallback;
    return !strcmp(v, "true");
}

static void ram_text(int mb, char *out, int n)
{
    if (mb % 1024 == 0) snprintf(out, n, "%d GB", mb / 1024);
    else snprintf(out, n, "%.1f GB", mb / 1024.0);
}

static const char *type_name(const char *type)
{
    return !strcmp(type, "paper") ? "Paper" : "Minecraft";
}

static void installed_text(char *out, int n)
{
    if (!hi.ok) snprintf(out, n, "Nothing yet");
    else if (hi.build[0]) snprintf(out, n, "%s %s, build %s", type_name(hi.type), hi.version, hi.build);
    else snprintf(out, n, "%s %s", type_name(hi.type), hi.version);
}

/* Names in whitelist.json or ops.json, which the server keeps. */
static int json_names(const char *path, char names[][17], int max)
{
    char *t = file_read(path, NULL);
    const char *e;
    int n = 0;
    if (!t) return 0;
    for (e = js_first(t); e && n < max; e = js_next(e))
        if (js_str(js_key(e, "name"), names[n], 17) && names[n][0]) n++;
    free(t);
    return n;
}

/* Lists that come from files: read again every couple of seconds. */
static void refresh_lists(int now)
{
    int i;
    if (!now && ui_ms() - lists_at < 2000) return;
    lists_at = ui_ms();
    nwl = json_names(HM_SERVER "/whitelist.json", wl_names, 32);
    nop = json_names(HM_SERVER "/ops.json", op_names, 32);
    world_backups(&backups);
    for (i = 0; i < backups.n; i++) {
        char path[120], *t;
        snprintf(path, sizeof path, HM_BACKUPS "/%s", backups.name[i]);
        snprintf(path + strlen(path) - 7, 8, ".txt");
        backup_notes[i][0] = 0;
        if ((t = file_read(path, NULL))) {
            t[strcspn(t, "\r\n")] = 0;
            snprintf(backup_notes[i], sizeof backup_notes[i], "%s", t);
            free(t);
        }
    }
}

static int is_op(const char *name)
{
    int i;
    for (i = 0; i < nop; i++)
        if (!strcasecmp(op_names[i], name)) return 1;
    return 0;
}

/* "2026-11-15_14-30-05.tar.gz" -> "15 Nov 2026 at 14:30" */
static void backup_date(const char *name, char *out, int n)
{
    static const char *const mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int y, m, d, h, mi, s;
    if (sscanf(name, "%d-%d-%d_%d-%d-%d", &y, &m, &d, &h, &mi, &s) == 6 && m >= 1 && m <= 12)
        snprintf(out, n, "%d %s %d at %02d:%02d", d, mon[m - 1], y, h, mi);
    else
        snprintf(out, n, "%s", name);
}

/* ------------------------------------------------------------ text tools */

/* How many characters of s fit in w on one line, broken at a space when there's one. */
static int fit_len(int font, const char *s, int w)
{
    const struct font *f = &font_table[font];
    int pen = 0, i, space = -1;
    for (i = 0; s[i] && s[i] != '\n'; i++) {
        int c = (unsigned char)s[i];
        if (c < FONT_FIRST || c > FONT_LAST) c = '?';
        pen += f->glyphs[c - FONT_FIRST].adv64;
        if (((pen + 32) >> 6) > w && i > 0) return space > 0 ? space : i;
        if (s[i] == ' ') space = i;
    }
    return i;
}

/* Draws s wrapped to w (or only counts the lines when draw is 0). */
static int wrap(int font, int x, int y, int w, int gap, uint32_t c, const char *s, int max_lines, int draw)
{
    char line[300];
    int lines = 0, lh = gfx_line_h(font) + gap;
    while (*s && lines < max_lines) {
        int n = fit_len(font, s, w);
        if (n <= 0) n = 1;
        if (draw) {
            snprintf(line, sizeof line, "%.*s", n < 299 ? n : 299, s);
            if (lines == max_lines - 1 && s[n] && s[n] != '\n') gfx_str_fit(font, x, y, c, s, w);
            else gfx_str(font, x, y, c, line);
        }
        s += n;
        while (*s == ' ' || *s == '\n') s++;
        y += lh;
        lines++;
    }
    return lines;
}

/* -------------------------------------------------------- full screens */

static void message(const char *title, const char *body)
{
    sfx(SFX_ERROR);
    ui_message(title, body);
}

/* A list to choose from: the index, or -1 for B. */
static int pick(const char *title, const char *sub, char items[][64], int n, int sel)
{
    int first = 0, rowh = 56, top = HEADER_H + 16, w = SCR_W - 2 * M, visible = (SCR_H - top - 70) / rowh, i;
    float hl = (float)sel;
    if (n <= 0) return -1;
    if (sel < 0 || sel >= n) sel = 0;
    ui_fade_in();
    for (;;) {
        int d;
        ui_poll();
        hm_frame();
        d = ui_nav_y();
        if (d && sel + d >= 0 && sel + d < n) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(a)) { sfx(SFX_SELECT); ui_settle_input(); return sel; }
        if (PRESSED(b)) { sfx(SFX_BACK); ui_settle_input(); return -1; }
        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;
        hl = ui_ease(hl, (float)(sel - first), 0.45f);
        ui_bg();
        ui_header(title, sub);
        ui_select_box((float)M, top + hl * rowh, (float)w, rowh - 8.0f, 10);
        for (i = first; i < n && i < first + visible; i++)
            gfx_str_vc(FONT_BODY_SB, M + 22, top + (i - first) * rowh + (rowh - 8) / 2, C_TEXT, items[i]);
        if (n > visible) {
            char pos[24];
            snprintf(pos, sizeof pos, "%d of %d", sel + 1, n);
            gfx_str_r(FONT_SMALL, SCR_W - M, SCR_H - 46, C_DIM, pos);
        }
        ui_hints("A=Choose;B=Back");
        ui_end_frame();
    }
}

static void write_eula(void)
{
    char buf[300];
    int n = snprintf(buf, sizeof buf,
                     "#By changing the setting below to TRUE you are indicating your agreement to our EULA (" EULA_URL ").\n"
                     "#Agreed to in HostMC on %s\neula=true\n", hs.eula);
    mkdirs(HM_SERVER);
    file_write(HM_SERVER "/eula.txt", buf, n, 0644);
}

/* Minecraft's EULA, before the first start. 1 when the user agrees. */
static int eula_screen(void)
{
    int sel = 0;
    ui_fade_in();
    for (;;) {
        int d, top = HEADER_H + 10, ph = SCR_H - top - 80, qs = 230, tx = M + 32, tw, y, i;
        static const char *const opts[2] = {"I agree to the EULA", "Not now"};
        ui_poll();
        hm_frame();
        d = ui_nav_y();
        if (d && sel + d >= 0 && sel + d < 2) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(b)) { sfx(SFX_BACK); return 0; }
        if (PRESSED(a)) {
            sfx(SFX_SELECT);
            if (sel == 1) return 0;
            {
                time_t now = time(NULL);
                struct tm tm;
                localtime_r(&now, &tm);
                strftime(hs.eula, sizeof hs.eula, "%Y-%m-%d %H:%M", &tm);
            }
            hs_save();
            write_eula();
            return 1;
        }
        ui_bg();
        ui_header("Minecraft's EULA", "Before the server starts for the first time");
        ui_panel(M, top, SCR_W - 2 * M, ph);
        tw = SCR_W - 2 * M - 64 - qs - 40;
        y = top + 28;
        y += wrap(FONT_BODY, tx, y, tw, 6, C_TEXT,
                  "The Minecraft server is Mojang's software. To run it you have to agree to Minecraft's End User "
                  "License Agreement, the EULA. Please read it first.", 6, 1) * (gfx_line_h(FONT_BODY) + 6) + 14;
        gfx_str(FONT_LABEL, tx, y, C_TEXT, EULA_URL);
        y += gfx_line_h(FONT_LABEL) + 14;
        wrap(FONT_SMALL, tx, y, tw, 4, C_SUB,
             "Scan the code to read it on your phone. When you agree, HostMC writes your answer into the server's "
             "eula.txt, as the server asks, and doesn't ask again.", 4, 1);
        qr_draw(EULA_URL, SCR_W - M - 32 - qs, top + 28, qs);
        gfx_str_c(FONT_SMALL, SCR_W - M - 32 - qs / 2, top + 36 + qs, C_DIM, "Scan to read the EULA");
        y = top + ph - 2 * 58 - 18;
        for (i = 0; i < 2; i++) {
            int by = y + i * 58, bw = 360;
            gfx_fill_rrect((float)tx, (float)by, (float)bw, 48, 10, i == sel ? C_TEXT : C_SURFACE2, 255);
            gfx_str_vc(FONT_BODY_SB, tx + 20, by + 24, i == sel ? C_INK : C_TEXT, opts[i]);
        }
        ui_hints("A=Select;B=Back");
        ui_end_frame();
    }
}

static int wait_stopped(int seconds)
{
    uint64_t until = ui_ms() + (uint64_t)seconds * 1000;
    while (sv.state != SV_STOPPED && ui_ms() < until) {
        ui_poll();
        hm_frame();
        ui_loading_frame("Stopping the server", "It saves the world first");
    }
    ui_settle_input();
    return sv.state == SV_STOPPED;
}

/* For things that need the server stopped: asks, stops it and waits. 1 when it's stopped. */
static int make_stopped(const char *why)
{
    if (sv.state == SV_STOPPED) return 1;
    if (!ui_confirm("Stop the server?", why, "Stop it", "Cancel")) return 0;
    sv_stop();
    if (wait_stopped(120)) return 1;
    message("The server is still stopping", "Try again in a moment.");
    return 0;
}

/* ------------------------------------------------------------- starting */

static int need_install(void)
{
    if (!hi.ok || force_update) return 1;
    if (strcmp(hs.type, hi.type)) return 1;
    return strcmp(hs.version, "latest") && strcmp(hs.version, hi.version);
}

static void do_start(void)
{
    char why[256], body[700], a[24], line[200];
    struct server_plan sp;
    struct java_plan jp;
    int need_server, need_java, java;

    if (sv.state != SV_STOPPED) return;
    if (!hs.eula[0] && !eula_screen()) return;
    memset(&sp, 0, sizeof sp);
    memset(&jp, 0, sizeof jp);
    why[0] = 0;
    need_server = need_install();
    if (need_server && !plan_server(hs.type, force_update ? "latest" : hs.version, &sp, why, sizeof why)) {
        if (strcmp(why, "Cancelled.")) message("The server can't be downloaded", why);
        return;
    }
    if (need_server && hi.ok && !strcmp(hi.type, sp.paper ? "paper" : "vanilla") && !strcmp(hi.version, sp.version) &&
        !strcmp(hi.build, sp.build)) {
        need_server = 0;                       /* what's installed is already the newest */
        force_update = 0;
    }
    java = need_server ? sp.java : hi.java;
    need_java = java_path(java) == NULL;
    if (need_java && !plan_java(java, &jp, why, sizeof why)) {
        if (strcmp(why, "Cancelled.")) message("Java can't be downloaded", why);
        return;
    }
    if (need_server || need_java) {
        body[0] = 0;
        if (need_server) {
            human_size(sp.size, a, sizeof a);
            snprintf(line, sizeof line, "%s %s, the server (%s), from %s.\n", sp.paper ? "Paper" : "Minecraft", sp.version, a,
                     sp.paper ? "PaperMC" : "Mojang");
            strncat(body, line, sizeof body - strlen(body) - 1);
        }
        if (need_java) {
            human_size(jp.size, a, sizeof a);
            snprintf(line, sizeof line, "Java %d (%s): Eclipse Temurin's runtime, from Adoptium. %s %s needs it.\n", java, a,
                     need_server ? (sp.paper ? "Paper" : "Minecraft") : type_name(hi.type), need_server ? sp.version : hi.version);
            strncat(body, line, sizeof body - strlen(body) - 1);
        }
        strncat(body, "They stay in HostMC's folder for next time.", sizeof body - strlen(body) - 1);
        if (!ui_confirm("HostMC downloads these first", body, "Download and start", "Not now")) return;
    }
    if (need_server) {
        /* A world can't go back to an older version, and Paper keeps the Nether and the End in
         * folders of their own: a backup first, in case the switch isn't wanted after all. */
        if (hi.ok && world_exists() && (strcmp(hi.type, sp.paper ? "paper" : "vanilla") || version_newer(hi.version, sp.version))) {
            char note[96];
            snprintf(note, sizeof note, "Before switching to %s %s", sp.paper ? "Paper" : "Minecraft", sp.version);
            if (!world_backup(note, why, sizeof why) &&
                !ui_confirm("The world wasn't backed up", why, "Switch anyway", "Cancel")) return;
        }
        if (!get_server(&sp, why, sizeof why)) {
            if (strcmp(why, "Cancelled.")) message("The server didn't download", why);
            return;
        }
        force_update = 0;
    }
    if (need_java && !get_java(&jp, why, sizeof why)) {
        if (strcmp(why, "Cancelled.")) message("Java didn't download", why);
        return;
    }
    write_eula();                              /* the user's own answer, from the EULA screen */
    {
        /* Minecraft 26 turns the whitelist on in a new server.properties, and earlier versions
         * leave it off. HostMC makes it on for every version, so what Settings shows is so. */
        char v[16];
        if (!props_get("white-list", v, sizeof v)) {
            props_set("white-list", "true");
            props_set("enforce-whitelist", "true");
        }
    }
    if (!sv_start(why, sizeof why)) {
        message("The server didn't start", why);
        return;
    }
    needs_restart = 0;
    sfx(SFX_SELECT);
}

/* ------------------------------------------------------------ the pages */

static void build_server(void)
{
    char ip[48], text[160], port[16] = "";
    struct row *r;
    add(R_STATUS, 0, NULL, NULL);
    if (sv.state == SV_STOPPED) {
        r = add(R_BUTTON, A_START, hi.ok ? "Start the server" : "Set up and start the server", NULL);
        r->primary = 1;
    } else if (sv.state == SV_STOPPING) {
        if (ui_ms() - sv.stop_at > 45000 && sv.stop_at) add(R_BUTTON, A_FORCE, "Make it stop now", NULL);
    } else {
        add(R_BUTTON, A_STOP, "Stop the server", NULL);
        if (sv.state == SV_RUNNING) add(R_BUTTON, A_RESTART, "Restart", NULL);
    }
    if (sv.why[0] && sv.state == SV_STOPPED) add(R_TEXT, 0, NULL, sv.why)->color = C_BAD;
    if (needs_restart && sv.state == SV_RUNNING) add(R_TEXT, 0, NULL, "Restart the server to use the new settings.")->color = C_WARN;
    add(R_HEAD, 0, "Join at", NULL);
    if (server_port() != DEFAULT_PORT) snprintf(port, sizeof port, ":%d", server_port());
    snprintf(text, sizeof text, "localhost%s", port);
    add(R_INFO, 0, "Minecraft on this PC", text);
    if (lan_address(ip, sizeof ip)) snprintf(text, sizeof text, "%s%s", ip, port);
    else snprintf(text, sizeof text, "Not on a network");
    add(R_INFO, 0, "Your home network", text);
    if (hs.vps) vps_address(text, sizeof text);
    else if (!hs.internet) snprintf(text, sizeof text, "Off. Turn it on in Internet");
    else if (pi.address[0]) snprintf(text, sizeof text, "%s", pi.address);
    else if (pi.state == PI_CLAIM) snprintf(text, sizeof text, "Waiting for playit.gg to be set up");
    else snprintf(text, sizeof text, "Waiting for playit.gg");
    r = add(R_INFO, 0, "The internet", text);
    if (!hs.internet && !hs.vps) r->color = C_DIM;
    else if (hs.vps && (!vps_up() || (vps.check[0] && !vps.check_ok))) r->color = C_SUB;   /* not yet working */
    if (prop_bool("white-list", 1) && !sv.nplayers)
        add(R_TEXT, 0, NULL, "The whitelist is on: add your friends' Minecraft names in Players first.")->color = C_SUB;
    snprintf(text, sizeof text, "%d of %d", sv.nplayers, props_get_int("max-players", 20));
    add(R_HEAD, 0, "Players online", sv.state == SV_RUNNING ? text : NULL);
    if (sv.nplayers) add(R_NAMES, 0, NULL, NULL);
    else add(R_TEXT, 0, NULL, sv.state == SV_RUNNING ? "Nobody's on yet." : "They show up here while the server runs.")->color = C_DIM;
}

static void build_players(void)
{
    char text[64];
    int i;
    struct row *r;
    refresh_lists(0);
    snprintf(text, sizeof text, "%d", sv.nplayers);
    add(R_HEAD, 0, "Online now", sv.state == SV_RUNNING ? text : NULL);
    for (i = 0; i < sv.nplayers; i++) add(R_ITEM, ID_PLAYER + i, sv.players[i], is_op(sv.players[i]) ? "Operator" : NULL);
    if (!sv.nplayers) add(R_TEXT, 0, NULL, sv.state == SV_RUNNING ? "Nobody's on right now." : "The server isn't running.")->color = C_DIM;
    add(R_HEAD, 0, "Whitelist", prop_bool("white-list", 1) ? "On" : "Off");
    add(R_BUTTON, A_WL_ADD, "Add someone to the whitelist", NULL);
    for (i = 0; i < nwl; i++) add(R_ITEM, ID_WL + i, wl_names[i], NULL);
    if (!prop_bool("white-list", 1)) {
        r = add(R_TEXT, 0, NULL, "The whitelist is off, so anyone with the address can join. Turn it on in Settings to let in only the people on it.");
        r->color = C_DIM;
    }
    add(R_HEAD, 0, "Operators", NULL);
    for (i = 0; i < nop; i++) add(R_ITEM, ID_OP + i, op_names[i], "Operator");
    if (!nop) add(R_TEXT, 0, NULL, "Operators can use commands in the game. Choose someone who's online to make them one.")->color = C_DIM;
}

static void build_settings(void)
{
    char v[200], help[220] = "";
    if (!props_get("motd", v, sizeof v) || !v[0]) snprintf(v, sizeof v, "A Minecraft Server");
    add(R_ITEM, S_MOTD, "Message of the day", v);
    props_get("gamemode", v, sizeof v);
    add(R_ITEM, S_MODE, "Game mode", mode_names[index_of(modes, 4, v, 0)])->cycle = 1;
    props_get("difficulty", v, sizeof v);
    add(R_ITEM, S_DIFF, "Difficulty", diff_names[index_of(diffs, 4, v, 1)])->cycle = 1;
    snprintf(v, sizeof v, "%d", props_get_int("max-players", 20));
    add(R_ITEM, S_MAX, "Max players", v)->cycle = 1;
    add(R_ITEM, S_WL, "Whitelist", prop_bool("white-list", 1) ? "On" : "Off")->cycle = 1;
    add(R_ITEM, S_ONLINE, "Online mode", prop_bool("online-mode", 1) ? "On" : "Off")->cycle = 1;
    if (!props_get("level-seed", v, sizeof v) || !v[0]) snprintf(v, sizeof v, "Random");
    add(R_ITEM, S_SEED, "World seed", v);
    ram_text(hs.ram_mb, v, sizeof v);
    add(R_ITEM, S_RAM, "Memory", v)->cycle = 1;
    snprintf(v, sizeof v, "%d", server_port());
    add(R_ITEM, S_PORT, "Port", v);
    switch (in_page ? sel_id : 0) {
    case S_MOTD: snprintf(help, sizeof help, "What players see under the server's name in their server list."); break;
    case S_MODE: snprintf(help, sizeof help, "The game mode new players start in."); break;
    case S_DIFF: snprintf(help, sizeof help, "How hard the mobs are."); break;
    case S_MAX: snprintf(help, sizeof help, "How many can be on at once."); break;
    case S_WL: snprintf(help, sizeof help, "On: only the people on the whitelist (in Players) can join."); break;
    case S_ONLINE: snprintf(help, sizeof help, "On: everyone needs a real Minecraft account, checked with Mojang. Off: anyone can join with any name, so use the whitelist."); break;
    case S_SEED: snprintf(help, sizeof help, "Used when a new world is made. Start a new world in World to use a new one."); break;
    case S_RAM: snprintf(help, sizeof help, "How much of this PC's memory the server may use. Leave enough for Minecraft itself if you play here too."); break;
    case S_PORT: snprintf(help, sizeof help, "Minecraft's usual port is 25565. Change it only to run a second server."); break;
    default: snprintf(help, sizeof help, "These are kept in the server's server.properties.");
    }
    snprintf(footer, sizeof footer, "%s%s", help,
             sv.state != SV_STOPPED ? " Game mode, difficulty and the whitelist change straight away; the rest when it restarts." : "");
}

static int update_available(void)
{
    int t = !strcmp(hs.type, "paper");
    return hi.ok && !strcmp(hs.version, "latest") && !strcmp(hs.type, hi.type) && versions_ok[t] &&
           versions[t].latest[0] && version_newer(versions[t].latest, hi.version);
}

static void build_version(void)
{
    char v[160], inst[96];
    int t = !strcmp(hs.type, "paper");
    add(R_ITEM, V_TYPE, "Server", type_name(hs.type))->cycle = 1;
    if (!strcmp(hs.version, "latest")) {
        if (versions_ok[t] && versions[t].latest[0]) snprintf(v, sizeof v, "Latest release (%s)", versions[t].latest);
        else snprintf(v, sizeof v, "Latest release");
    } else {
        snprintf(v, sizeof v, "%s", hs.version);
    }
    add(R_ITEM, V_VERSION, "Version", v);
    installed_text(inst, sizeof inst);
    add(R_INFO, 0, "Installed now", inst);
    if (hi.ok && java_path(hi.java)) snprintf(v, sizeof v, "Java %d, Eclipse Temurin", hi.java);
    else if (hi.ok) snprintf(v, sizeof v, "Java %d comes with the next start", hi.java);
    else snprintf(v, sizeof v, "Comes with the server");
    add(R_INFO, 0, "Java", v);
    add(R_GAP, 0, NULL, NULL);
    if (update_available()) {
        snprintf(v, sizeof v, "Update to %s %s", type_name(hs.type), versions[t].latest);
        add(R_BUTTON, V_UPDATE, v, NULL)->primary = 1;
    } else if (need_install() && hi.ok) {
        snprintf(v, sizeof v, "%s %s is downloaded the next time the server starts.%s", type_name(hs.type),
                 strcmp(hs.version, "latest") ? hs.version : "(the latest release)",
                 sv.state != SV_STOPPED ? " Restart it to switch now." : "");
        add(R_TEXT, 0, NULL, v)->color = C_WARN;
    }
    add(R_BUTTON, V_CHECK, "Check for a new version", NULL);
    add(R_TEXT, 0, NULL, "Minecraft is Mojang's own server. Paper is a faster one that also runs plugins. "
                         "Everyone's Minecraft has to be the same version as the server, so pick the one your Prism Launcher instance uses.")->color = C_DIM;
}

static void build_world(void)
{
    char a[64], b[200], size[24], level[64];
    int i;
    refresh_lists(0);
    if (!props_get("level-name", level, sizeof level) || !level[0]) snprintf(level, sizeof level, "world");
    add(R_BUTTON, W_BACKUP, "Back up the world now", NULL);
    if (backups.n) add(R_BUTTON, W_RESTORE, "Put a backup back", NULL);
    add(R_BUTTON, W_RESET, "Start a new world", NULL);
    snprintf(a, sizeof a, "%d", backups.n);
    add(R_HEAD, 0, "Backups", backups.n ? a : NULL);
    for (i = 0; i < backups.n && i < 6; i++) {
        backup_date(backups.name[i], a, sizeof a);
        human_size(backups.size[i], size, sizeof size);
        snprintf(b, sizeof b, "%s%s%s", size, backup_notes[i][0] ? ". " : "", backup_notes[i]);
        add(R_INFO, 0, a, b);
    }
    if (backups.n > 6) {
        snprintf(b, sizeof b, "And %d older ones. HostMC keeps the newest %d.", backups.n - 6, MAX_BACKUPS);
        add(R_TEXT, 0, NULL, b)->color = C_DIM;
    }
    if (!backups.n) add(R_TEXT, 0, NULL, "No backups yet. HostMC also makes one before it changes the world or switches the server.")->color = C_DIM;
    snprintf(b, sizeof b, "The world is in hostmc/server/%s in XenithOS's data folder.", level);
    add(R_TEXT, 0, NULL, b)->color = C_DIM;
}

/* The player's own VPS, when that's the way in. */
static void build_vps(void)
{
    char addr[160], who[220];
    uint32_t color;
    const char *st = vps_status(&color);
    vps_address(addr, sizeof addr);
    add(R_INFO, 0, "Friends join at", addr);
    snprintf(who, sizeof who, "%s@%s, SSH port %d", vps.cfg.user, vps.cfg.host, vps.cfg.ssh_port);
    add(R_INFO, 0, "Your VPS", who);
    add(R_INFO, 0, "Tunnel", st)->color = color;
    if (vps.why[0] && !vps_up() && sv.state == SV_RUNNING) add(R_TEXT, 0, NULL, vps.why)->color = C_BAD;
    if (vps_checking()) add(R_TEXT, 0, NULL, "Checking the way in from the internet...")->color = C_SUB;
    else if (vps.check[0]) add(R_TEXT, 0, NULL, vps.check)->color = vps.check_ok ? C_OK : C_BAD;
    add(R_GAP, 0, NULL, NULL);
    if (vps_up()) add(R_BUTTON, N_VPS_CHECK, "Check from the internet", NULL);
    add(R_BUTTON, N_VPS_STEPS, "Show the setup steps", NULL);
    add(R_BUTTON, N_VPS_EDIT, "Change the VPS", NULL);
    if (strstr(vps.why, "identity changed")) add(R_BUTTON, N_VPS_FORGET, "Forget the VPS's identity", NULL);
    add(R_BUTTON, N_VPS_OFF, "Turn off the VPS route", NULL);
    add(R_TEXT, 0, NULL, "Through the VPS every player shows up as 127.0.0.1, so bans by address don't work for them; "
                         "bans by name do. It carries Java Edition, not Bedrock.")->color = C_DIM;
}

static void build_internet(void)
{
    struct row *r;
    if (hs.vps) {
        build_vps();
        return;
    }
    switch (pi.state) {
    case PI_OFF:
        add(R_TEXT, 0, NULL, "Friends who aren't on your home network can join through playit.gg, or through a server of your own "
                             "on the internet (a VPS). Your router stays as it is either way.");
        add(R_HEAD, 0, "playit.gg", NULL);
        add(R_TEXT, 0, NULL, "A free service: you approve HostMC with a playit.gg account, and its agent (about 9 MB, from playit's "
                             "releases on GitHub) runs while the server does.")->color = C_SUB;
        add(R_BUTTON, N_ON, "Turn on playit.gg", NULL)->primary = 1;
        add(R_HEAD, 0, "Your own server (VPS)", NULL);
        add(R_TEXT, 0, NULL, "HostMC logs in to your VPS over SSH with a key of its own and keeps a tunnel to it open while the "
                             "server runs. You add one line and one setting on the VPS. Java Edition only.")->color = C_SUB;
        add(R_BUTTON, N_VPS, "Use your own server (VPS)", NULL);
        break;
    case PI_CLAIM:
        add(R_QR, 0, NULL, pi.claim_url);
        add(R_BUTTON, N_OFF, "Cancel", NULL);
        break;
    case PI_STARTING:
    case PI_ON:
        add(R_INFO, 0, "Address", pi.address[0] ? pi.address : "Not made yet");
        add(R_INFO, 0, "Agent", pi_agent_running() ? "Running" : sv.state == SV_RUNNING ? "Starting" : "Runs while the server does");
        if (pi.note[0]) add(R_TEXT, 0, NULL, pi.note)->color = C_SUB;
        if (pi.address[0]) add(R_TEXT, 0, NULL, "Friends add this address in Minecraft's Multiplayer screen.")->color = C_DIM;
        add(R_GAP, 0, NULL, NULL);
        add(R_BUTTON, N_OFF, "Turn off playit.gg", NULL);
        add(R_BUTTON, N_FORGET, "Forget this PC's playit.gg agent", NULL);
        break;
    default:
        r = add(R_TEXT, 0, NULL, pi.note[0] ? pi.note : "playit.gg isn't working.");
        r->color = C_BAD;
        add(R_BUTTON, N_OFF, "Turn off playit.gg", NULL);
        add(R_BUTTON, N_FORGET, "Forget the agent and start again", NULL);
        break;
    }
}

static void build(void)
{
    nrows = 0;
    footer[0] = 0;
    switch (page) {
    case P_SERVER: build_server(); break;
    case P_PLAYERS: build_players(); break;
    case P_SETTINGS: build_settings(); break;
    case P_VERSION: build_version(); break;
    case P_WORLD: build_world(); break;
    case P_INTERNET: build_internet(); break;
    }
}

/* ------------------------------------------------------------- actions */

static int ask_text(const char *title, const char *hint, char *buf, int max)
{
    int ok = keyboard(title, hint, buf, max, KB_LINE);
    ui_settle_input();
    return ok;
}

static void player_menu(const char *name)
{
    char title[64], cmd[96];
    const char *opts[4];
    int op = is_op(name), c;
    opts[0] = op ? "Stop being an operator" : "Make them an operator";
    opts[1] = "Kick them off";
    opts[2] = "Ban them";
    opts[3] = "Cancel";
    snprintf(title, sizeof title, "%s", name);
    c = ui_dialog(title, op ? "An operator can use commands in the game." : "Operators can use commands in the game, like changing the time.", opts, 4);
    if (c == 0) snprintf(cmd, sizeof cmd, "%s %s", op ? "deop" : "op", name);
    else if (c == 1) snprintf(cmd, sizeof cmd, "kick %s", name);
    else if (c == 2) {
        snprintf(title, sizeof title, "Ban %s?", name);
        if (!ui_confirm(title, "They can't join again until they're pardoned (the pardon command, in Console).", "Ban them", "Cancel")) return;
        snprintf(cmd, sizeof cmd, "ban %s", name);
    } else return;
    if (!sv_command(cmd)) message("The server isn't running", "Start it first.");
    lists_at = 0;
}

static void change_setting(int id, int dir)
{
    char v[200];
    int i;
    switch (id) {
    case S_MOTD:
        if (!props_get("motd", v, sizeof v) || !v[0]) snprintf(v, sizeof v, "A Minecraft Server");
        if (ask_text("Message of the day", "What players see in their server list", v, 59)) {
            props_set("motd", v);
            if (sv.state != SV_STOPPED) needs_restart = 1;
        }
        break;
    case S_MODE:
        props_get("gamemode", v, sizeof v);
        i = (index_of(modes, 4, v, 0) + (dir ? dir : 1) + 4) % 4;
        props_set("gamemode", modes[i]);
        if (sv.state == SV_RUNNING) {
            char cmd[48];
            snprintf(cmd, sizeof cmd, "defaultgamemode %s", modes[i]);
            sv_command(cmd);
        }
        break;
    case S_DIFF:
        props_get("difficulty", v, sizeof v);
        i = (index_of(diffs, 4, v, 1) + (dir ? dir : 1) + 4) % 4;
        props_set("difficulty", diffs[i]);
        if (sv.state == SV_RUNNING) {
            char cmd[48];
            snprintf(cmd, sizeof cmd, "difficulty %s", diffs[i]);
            sv_command(cmd);
        }
        break;
    case S_MAX:
        i = props_get_int("max-players", 20) + (dir ? dir : 1);
        if (i < 1) i = 1;
        if (i > 200) i = 200;
        snprintf(v, sizeof v, "%d", i);
        props_set("max-players", v);
        if (sv.state != SV_STOPPED) needs_restart = 1;
        break;
    case S_WL:
        i = !prop_bool("white-list", 1);
        props_set("white-list", i ? "true" : "false");
        props_set("enforce-whitelist", i ? "true" : "false");
        if (sv.state == SV_RUNNING) sv_command(i ? "whitelist on" : "whitelist off");
        break;
    case S_ONLINE:
        i = !prop_bool("online-mode", 1);
        if (!i && !ui_confirm("Turn online mode off?",
                              "Then the server doesn't check accounts with Mojang: anyone can join with any name, even "
                              "someone else's. Only do it with the whitelist on, or for players without Minecraft accounts you trust.",
                              "Turn it off", "Keep it on")) break;
        props_set("online-mode", i ? "true" : "false");
        if (sv.state != SV_STOPPED) needs_restart = 1;
        break;
    case S_SEED:
        props_get("level-seed", v, sizeof v);
        if (ask_text("World seed", "Leave it empty for a random world", v, 40)) {
            props_set("level-seed", v);
            if (world_exists()) ui_message("Saved for the next new world", "This world keeps its own seed. Start a new world in World to use this one.");
        }
        break;
    case S_RAM: {
        int total = mem_total_mb(), k = 0;
        for (i = 0; i < NRAM; i++) if (ram_steps[i] <= hs.ram_mb) k = i;
        k += dir ? dir : 1;
        if (k < 0) k = 0;
        if (k >= NRAM || ram_steps[k] > total - 1024) {
            if (dir >= 0) { sfx(SFX_ERROR); return; }
            k = NRAM - 1;
            while (k > 0 && ram_steps[k] > total - 1024) k--;
        }
        hs.ram_mb = ram_steps[k];
        hs_save();
        if (sv.state != SV_STOPPED) needs_restart = 1;
        break;
    }
    case S_PORT:
        snprintf(v, sizeof v, "%d", server_port());
        if (ask_text("Port", "A number from 1024 to 65535", v, 5)) {
            int p = atoi(v);
            if (p < 1024 || p > 65535 || (int)strspn(v, "0123456789") != (int)strlen(v)) {
                message("That port can't be used", "Choose a number from 1024 to 65535. Minecraft's usual one is 25565.");
                break;
            }
            snprintf(v, sizeof v, "%d", p);
            props_set("server-port", v);
            if (sv.state != SV_STOPPED) needs_restart = 1;
            if (hs.internet) ui_message("Change the playit.gg tunnel too", "On playit.gg, point the tunnel at the new port, or turn playit.gg off and on.");
        }
        break;
    }
    sfx(SFX_MOVE);
}

static int load_versions(int paper, int refresh)
{
    char why[200] = "";
    if (versions_get(paper, refresh, &versions[paper], why, sizeof why)) {
        versions_ok[paper] = 1;
        return 1;
    }
    if (strcmp(why, "Cancelled.")) message("No list of versions", why);
    return 0;
}

static void choose_version(void)
{
    static char items[MAX_VERSIONS + 1][64];
    int paper = !strcmp(hs.type, "paper"), n, i, sel = 0, c;
    if (!versions_ok[paper] && !load_versions(paper, 0)) return;
    snprintf(items[0], sizeof items[0], "Latest release (%s)", versions[paper].latest);
    for (i = 0, n = 1; i < versions[paper].n && n <= MAX_VERSIONS; i++, n++) {
        snprintf(items[n], sizeof items[n], "%s", versions[paper].id[i]);
        if (!strcmp(hs.version, versions[paper].id[i])) sel = n;
    }
    c = pick(paper ? "Paper version" : "Minecraft version", "Everyone's Minecraft needs the same version", items, n, sel);
    if (c < 0) return;
    if (c > 0 && hi.ok && world_exists() && version_newer(hi.version, items[c]) &&
        !ui_confirm("An older version?", "Worlds can't go back to an older Minecraft: things in it may be lost. "
                                         "HostMC backs the world up first.", "Use it anyway", "Cancel")) return;
    snprintf(hs.version, sizeof hs.version, "%s", c == 0 ? "latest" : items[c]);
    hs_save();
}

static void act_version(int id, int dir)
{
    int t;
    (void)dir;
    switch (id) {
    case V_TYPE:
        t = strcmp(hs.type, "paper") != 0;                 /* 1: becoming Paper */
        if (hi.ok && world_exists() && !t && !strcmp(hi.type, "paper") &&
            !ui_confirm("Back to Minecraft from Paper?", "Paper keeps worlds a little differently: older Paper puts the Nether and "
                        "the End in folders Minecraft doesn't read, so they'd start again. HostMC backs the world up first.",
                        "Switch", "Cancel")) return;
        snprintf(hs.type, sizeof hs.type, "%s", t ? "paper" : "vanilla");
        snprintf(hs.version, sizeof hs.version, "latest");
        hs_save();
        if (!versions_ok[t]) {
            char why[200];
            if (versions_get(t, -1, &versions[t], why, sizeof why)) versions_ok[t] = 1;
        }
        sfx(SFX_MOVE);
        break;
    case V_VERSION:
        choose_version();
        break;
    case V_CHECK: {
        int paper = !strcmp(hs.type, "paper");
        if (!load_versions(paper, 1)) return;
        if (update_available()) {
            ui_toast_icon(ICON_DOWNLOAD);
            ui_toast("There's a new version", versions[paper].latest);
        } else {
            ui_toast_icon(ICON_CHECK);
            ui_toast("You're up to date", hi.ok ? hi.version : versions[paper].latest);
        }
        break;
    }
    case V_UPDATE:
        if (sv.state != SV_STOPPED) {
            if (!ui_confirm("Update now?", "The server restarts to update. Everyone on it is disconnected for a minute.", "Update", "Not now")) return;
            force_update = 1;
            sv.restart = 1;
            sv_stop();
        } else {
            force_update = 1;
            do_start();
        }
        break;
    }
}

static void act_world(int id)
{
    char why[220] = "", items[MAX_BACKUPS][64], body[220];
    int i, c;
    switch (id) {
    case W_BACKUP:
        if (world_backup("Made by you", why, sizeof why)) {
            ui_toast_icon(ICON_CHECK);
            ui_toast("The world is backed up", "It's at the top of the list");
        } else {
            message("No backup was made", why);
        }
        break;
    case W_RESTORE:
        refresh_lists(1);
        for (i = 0; i < backups.n; i++) backup_date(backups.name[i], items[i], sizeof items[i]);
        c = pick("Put a backup back", "The world as it is now is backed up first", items, backups.n, 0);
        if (c < 0) break;
        snprintf(body, sizeof body, "The world goes back to how it was on %s. The world as it is now is backed up first.", items[c]);
        if (!ui_confirm("Put this backup back?", body, "Put it back", "Cancel")) break;
        if (!make_stopped("The server has to stop while the world is swapped.")) break;
        if (world_restore(backups.name[c], why, sizeof why)) {
            ui_toast_icon(ICON_CHECK);
            ui_toast("The backup is back", "Start the server to play it");
        } else {
            message("The backup wasn't put back", why);
        }
        break;
    case W_RESET:
        if (!world_exists()) {
            ui_message("There's no world yet", "A new one is made the first time the server starts.");
            break;
        }
        if (!ui_confirm("Start a new world?", "The world as it is now is backed up first. A new one is made the next time the server starts, "
                                              "from the seed in Settings, or a random one.", "Start a new world", "Cancel")) break;
        if (!make_stopped("The server has to stop to start a new world.")) break;
        if (world_reset(why, sizeof why)) {
            ui_toast_icon(ICON_CHECK);
            ui_toast("Ready for a new world", "It's made when the server starts");
        } else {
            message("The world wasn't changed", why);
        }
        break;
    }
    lists_at = 0;
}

/* What to do on the VPS, once: HostMC's key in authorized_keys, the sshd setting, the firewall. */
static void vps_steps(void)
{
    static const char *const opts[2] = {"Test the login", "Done"};
    char pub[240], cmd[420], sub[200], result[260] = "", why[220];
    uint32_t result_color = C_SUB;
    int sel = 0;
    tunnel_pubkey(&vps.t, pub, sizeof pub);
    snprintf(cmd, sizeof cmd, "mkdir -p ~/.ssh && echo '%s' >> ~/.ssh/authorized_keys", pub);
    snprintf(sub, sizeof sub, "For %s@%s, once", vps.cfg.user, vps.cfg.host);
    ui_fade_in();
    for (;;) {
        int top = HEADER_H + 4, ph = SCR_H - top - 70, qs = 236, x = M + 28, w = SCR_W - 2 * M - 56 - qs - 28, y, i, d;
        int lh = gfx_line_h(FONT_SMALL) + 3;
        char text[400];
        ui_poll();
        hm_frame();
        d = ui_nav_x();
        if (d && sel + d >= 0 && sel + d < 2) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(b)) { sfx(SFX_BACK); ui_settle_input(); return; }
        if (PRESSED(a)) {
            sfx(SFX_SELECT);
            if (sel == 1) { ui_settle_input(); return; }
            why[0] = 0;
            if (tunnel_test(&vps.t, &vps.cfg, why, sizeof why)) {
                snprintf(result, sizeof result, "HostMC logged in to the VPS. With steps 2 and 3 done too, start the server.");
                result_color = C_OK;
                sfx(SFX_SUCCESS);
            } else if (strcmp(why, "Cancelled.")) {
                snprintf(result, sizeof result, "%s", why);
                result_color = C_BAD;
                sfx(SFX_ERROR);
            }
            ui_settle_input();
        }
        ui_bg();
        ui_header("Set up your VPS", sub);
        ui_panel(M, top, SCR_W - 2 * M, ph);
        y = top + 20;
        gfx_str(FONT_BODY_SB, x, y, C_TEXT, "1   Let HostMC log in");
        y += gfx_line_h(FONT_BODY_SB) + 4;
        snprintf(text, sizeof text, "Add this line to ~/.ssh/authorized_keys for %s on the VPS. The code holds a command that adds it, "
                                    "to paste into the VPS from your phone.", vps.cfg.user);
        y += wrap(FONT_SMALL, x + 28, y, w - 28, 3, C_SUB, text, 3, 1) * lh + 4;
        y += wrap(FONT_SMALL, x + 28, y, w - 28, 3, C_TEXT, pub, 3, 1) * lh + 14;
        gfx_str(FONT_BODY_SB, x, y, C_TEXT, "2   Let it take players");
        y += gfx_line_h(FONT_BODY_SB) + 4;
        y += wrap(FONT_SMALL, x + 28, y, w - 28, 3, C_SUB, "In /etc/ssh/sshd_config, add the line GatewayPorts clientspecified, "
                                                            "then run sudo systemctl restart ssh.", 3, 1) * lh + 14;
        gfx_str(FONT_BODY_SB, x, y, C_TEXT, "3   Open the port");
        y += gfx_line_h(FONT_BODY_SB) + 4;
        snprintf(text, sizeof text, "Let port %d in through the VPS's firewall (sudo ufw allow %d/tcp), and through your "
                                    "provider's firewall if it has one.", vps.cfg.public_port, vps.cfg.public_port);
        y += wrap(FONT_SMALL, x + 28, y, w - 28, 3, C_SUB, text, 3, 1) * lh + 10;
        if (result[0]) wrap(FONT_SMALL, x, y, w, 3, result_color, result, 2, 1);
        qr_draw(cmd, SCR_W - M - 28 - qs, top + 20, qs);
        gfx_str_c(FONT_SMALL, SCR_W - M - 28 - qs / 2, top + 28 + qs, C_DIM, "Step 1 as a command");
        for (i = 0; i < 2; i++) {
            int bw = 260, bx = x + i * (bw + 16), by = top + ph - 66;
            gfx_fill_rrect((float)bx, (float)by, (float)bw, 46, 10, i == sel ? C_TEXT : C_SURFACE2, 255);
            gfx_str_vc(FONT_BODY_SB, bx + 20, by + 23, i == sel ? C_INK : C_TEXT, opts[i]);
        }
        ui_hints("A=Select;Left=Previous;Right=Next;B=Back");
        ui_end_frame();
    }
}

static int ask_port(const char *title, const char *hint, int *port)
{
    char v[16];
    snprintf(v, sizeof v, "%d", *port);
    for (;;) {
        if (!ask_text(title, hint, v, 5) || !v[0]) return 0;
        if (strspn(v, "0123456789") == strlen(v) && atoi(v) >= 1 && atoi(v) <= 65535) {
            *port = atoi(v);
            return 1;
        }
        message("That isn't a port", "Ports are numbers from 1 to 65535.");
    }
}

/* The own-VPS route: its address, user and ports, typed in (with the usual ones ready), HostMC's
 * key made, then the steps on the VPS. */
static void vps_setup(void)
{
    struct tunnel_cfg c = vps.cfg;
    char why[220] = "";
    int other_vps, changed;
    if (!ask_text("Your VPS's address", "Its IP address or its name, like 203.0.113.5", c.host, 120) || !c.host[0]) return;
    if (!ask_text("User name on the VPS", "The account HostMC logs in as, often root", c.user, 32) || !c.user[0]) return;
    if (!ask_port("SSH port on the VPS", "Usually 22", &c.ssh_port)) return;
    if (!ask_port("Port for players", "What friends connect to on the VPS. 25565 is Minecraft's own", &c.public_port)) return;
    c.local_port = server_port();
    if (!tunnel_cfg_ok(&c, why, sizeof why)) {
        message("That won't work", why);
        return;
    }
    other_vps = strcmp(c.host, vps.cfg.host) || c.ssh_port != vps.cfg.ssh_port;
    changed = other_vps || strcmp(c.user, vps.cfg.user) || c.public_port != vps.cfg.public_port;
    if (other_vps) tunnel_forget_host(&vps.t);                 /* another machine: its own identity */
    if (changed) tunnel_stop(&vps.t);                          /* it starts again with these */
    vps.cfg = c;
    vps_save();
    if (!vps_turn_on(why, sizeof why)) {
        message("Your VPS can't be used yet", why);
        return;
    }
    vps_steps();
}

static void act_internet(int id)
{
    char why[220] = "";
    switch (id) {
    case N_VPS:
    case N_VPS_EDIT:
        vps_setup();
        if (hs.vps) sel_id = N_VPS_STEPS;
        break;
    case N_VPS_STEPS:
        vps_steps();
        break;
    case N_VPS_CHECK:
        if (vps_up()) vps_check();
        break;
    case N_VPS_FORGET:
        if (!ui_confirm("Forget the VPS's identity?", "Only if you reinstalled or replaced the VPS: HostMC then trusts the one it "
                                                      "finds next time.", "Forget it", "Cancel")) break;
        tunnel_forget_host(&vps.t);
        vps.why[0] = 0;
        break;
    case N_VPS_OFF:
        vps_turn_off();
        sel_id = N_ON;
        break;
    case N_ON:
        if (pi_turn_on(why, sizeof why)) {
            sfx(SFX_SELECT);
            if (pi.state == PI_CLAIM) sel_id = N_OFF;
        } else if (strcmp(why, "Cancelled.")) {
            message("playit.gg can't be turned on", why);
        }
        break;
    case N_OFF:
        pi_turn_off();
        sel_id = N_ON;
        break;
    case N_FORGET:
        if (!ui_confirm("Forget the agent?", "HostMC deletes this PC's playit.gg key. Turning playit.gg on again makes a new one, which "
                                             "you approve again. Remove the old agent on playit.gg too.", "Forget it", "Cancel")) break;
        pi_forget();
        sel_id = N_ON;
        break;
    }
}

static void act(int id, int dir)
{
    if (id >= ID_OP) {
        char title[64];
        const char *name = op_names[id - ID_OP];
        snprintf(title, sizeof title, "%s stops being an operator?", name);
        if (dir == 0 && ui_confirm(title, "They can still play, without commands.", "Yes", "Cancel")) {
            char cmd[64];
            snprintf(cmd, sizeof cmd, "deop %s", name);
            if (!sv_command(cmd)) message("The server isn't running", "Start it first: it keeps the list.");
            lists_at = 0;
        }
        return;
    }
    if (id >= ID_WL) {
        char title[64];
        const char *name = wl_names[id - ID_WL];
        snprintf(title, sizeof title, "Take %s off the whitelist?", name);
        if (dir == 0 && ui_confirm(title, "With the whitelist on, they can't join any more.", "Take them off", "Cancel")) {
            char cmd[64];
            snprintf(cmd, sizeof cmd, "whitelist remove %s", name);
            if (!sv_command(cmd)) message("The server isn't running", "Start it first: it keeps the list.");
            lists_at = 0;
        }
        return;
    }
    if (id >= ID_PLAYER) {
        if (dir == 0 && id - ID_PLAYER < sv.nplayers) player_menu(sv.players[id - ID_PLAYER]);
        return;
    }
    if (id >= S_MOTD && id <= S_PORT) { change_setting(id, dir); return; }
    if (id >= V_TYPE && id <= V_INSTALL) { act_version(id, dir); return; }
    if (dir) return;
    switch (id) {
    case A_START: do_start(); break;
    case A_STOP: sv_stop(); break;
    case A_RESTART: sv.restart = 1; sv_stop(); break;
    case A_FORCE: sv_force_stop(); break;
    case A_WL_ADD: {
        char name[20] = "", why[200] = "", title[64];
        if (ask_text("Add to the whitelist", "Their Minecraft name, spelled exactly", name, 16) && name[0]) {
            if (whitelist_add(name, why, sizeof why)) {
                snprintf(title, sizeof title, "%s is on the whitelist", name);
                ui_toast_icon(ICON_PERSON_ADD);
                ui_toast(title, "They can join now");
            } else if (strcmp(why, "Cancelled.")) {
                message("They weren't added", why);
            }
            lists_at = 0;
        }
        break;
    }
    case W_BACKUP: case W_RESTORE: case W_RESET: act_world(id); break;
    case N_ON: case N_OFF: case N_FORGET:
    case N_VPS: case N_VPS_CHECK: case N_VPS_STEPS: case N_VPS_EDIT: case N_VPS_FORGET: case N_VPS_OFF:
        act_internet(id);
        break;
    }
}

/* ------------------------------------------------------------- console */

static uint32_t line_color(int kind)
{
    switch (kind) {
    case LN_WARN: return C_WARN;
    case LN_ERROR: return C_BAD;
    case LN_JOIN: return C_OK;
    case LN_CHAT: return C_TEXT;
    case LN_SENT: return C_TEXT;
    case LN_HOSTMC: return RGB(150, 186, 240);
    default: return RGB(178, 185, 196);
    }
}

/* What's shown of a line: the time, then the message without "[Server thread/INFO]: ". */
static const char *console_text(const char *line, char *stamp)
{
    const char *m;
    stamp[0] = 0;
    if (line[0] == '[' && line[3] == ':' && line[6] == ':' && (m = strstr(line, "]: "))) {
        memcpy(stamp, line + 1, 8);
        stamp[8] = 0;
        return m + 3;
    }
    return line;
}

static void draw_console(int px, int py, int pw, int ph)
{
    int x = px + 24, w = pw - 48 - 86, lh = gfx_line_h(FONT_SMALL) + 3, bottom = py + ph - 18, y = bottom, i, clip[4];
    int skip = console_back;
    ui_panel(px, py, pw, ph);
    gfx_get_clip(clip);
    gfx_clip_intersect(px + 1, py + 10, pw - 2, ph - 20);
    if (!sv.nlines) gfx_str(FONT_BODY, x, py + 24, C_DIM, "The server's messages show up here.");
    for (i = sv.nlines - 1; i >= 0 && y > py; i--) {
        const struct hm_line *l = sv_line(i);
        char stamp[12];
        const char *t = console_text(l->text, stamp);
        int lines;
        if (skip > 0) { skip--; continue; }
        lines = wrap(FONT_SMALL, x + 86, 0, w, 3, 0, t, 6, 0);
        y -= lines * lh;
        if (stamp[0]) gfx_str(FONT_SMALL, x, y, C_DIM, stamp);
        wrap(FONT_SMALL, stamp[0] ? x + 86 : x, y, stamp[0] ? w : w + 86, 3, line_color(l->kind), t, 6, 1);
    }
    gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
    if (console_back > 0) {
        char more[48];
        snprintf(more, sizeof more, "%d newer lines below", console_back);
        gfx_fill_rrect((float)(px + pw - 220), (float)(py + ph - 44), 200, 30, 15, C_SURFACE2, 255);
        gfx_str_c(FONT_SMALL, px + pw - 120, py + ph - 40, C_SUB, more);
    }
}

static void console_input(void)
{
    char buf[220] = "", cmd[240];
    int d = ui_nav_y();
    if (d < 0 && console_back < sv.nlines - 1) { console_back += 3; if (console_back > sv.nlines - 1) console_back = sv.nlines - 1; }
    if (d > 0 && console_back > 0) { console_back -= 3; if (console_back < 0) console_back = 0; }
    if (PRESSED(lb)) { console_back += 15; if (console_back > sv.nlines - 1) console_back = sv.nlines > 0 ? sv.nlines - 1 : 0; }
    if (PRESSED(rb)) { console_back -= 15; if (console_back < 0) console_back = 0; }
    if (PRESSED(a) || PRESSED(y)) {
        int say = PRESSED(y);
        sfx(SFX_SELECT);
        if (sv.state == SV_STOPPED) { message("The server isn't running", "Start it on the Server page first."); return; }
        if (!ask_text(say ? "Say something" : "Send a command", say ? "Everyone on the server sees it" : "Like time set day, without the slash", buf, 200) || !buf[0]) return;
        snprintf(cmd, sizeof cmd, "%s%s", say ? "say " : "", buf[0] == '/' ? buf + 1 : buf);
        if (!sv_command(cmd)) message("The command didn't reach the server", "It may still be starting. Try again in a moment.");
        console_back = 0;
    }
    if (PRESSED(x)) {
        static const char *const opts[] = {"Make it day", "Clear the weather", "Save the world", "Who's on", "Cancel"};
        static const char *const cmds[] = {"time set day", "weather clear", "save-all", "list"};
        int c;
        sfx(SFX_SELECT);
        if (sv.state != SV_RUNNING) { message("The server isn't running", "Start it on the Server page first."); return; }
        c = ui_dialog("Quick commands", NULL, opts, 5);
        if (c >= 0 && c < 4) { sv_command(cmds[c]); console_back = 0; }
    }
}

/* ---------------------------------------------------------------- drawing */

static int row_height(const struct row *r, int w)
{
    switch (r->kind) {
    case R_STATUS: return 110;
    case R_BUTTON: return 58;
    case R_ITEM: return page == P_SETTINGS ? 48 : 54;
    case R_HEAD: return 46;
    case R_INFO: return 35;
    case R_TEXT: return wrap(FONT_BODY, 0, 0, w, 4, 0, r->value, 8, 0) * (gfx_line_h(FONT_BODY) + 4) + 8;
    case R_NAMES: {
        int i, x = 0, lines = 1;
        for (i = 0; i < sv.nplayers; i++) {
            int cw = gfx_str_w(FONT_BODY_SB, sv.players[i]) + 32;
            if (x + cw > w && x > 0) { lines++; x = 0; }
            x += cw + 10;
        }
        return lines * 46 + 4;
    }
    case R_QR: return 250;
    default: return 14;
    }
}

static void draw_status(int x, int y, int w)
{
    const char *word, *line2;
    char a[64], b[160], inst[96];
    uint32_t dot;
    switch (sv.state) {
    case SV_RUNNING: word = "Running"; dot = C_OK; break;
    case SV_STARTING: word = "Starting"; dot = C_WARN; break;
    case SV_STOPPING: word = "Stopping"; dot = C_WARN; break;
    default: word = hi.ok ? "Stopped" : "Not set up yet"; dot = sv.why[0] ? C_BAD : C_DIM;
    }
    gfx_circle(x + 12.0f, y + 26.0f, 9, dot, 255);
    if (sv.state == SV_STARTING || sv.state == SV_STOPPING) gfx_ring(x + 12.0f, y + 26.0f, 15, 2, dot, (int)(128 + 100 * ((ui_ms() / 500) % 2)));
    gfx_str(FONT_HEAD, x + 38, y - 2, C_TEXT, word);
    installed_text(inst, sizeof inst);
    if (hi.ok) gfx_str_r(FONT_BODY_SB, x + w, y + 14, C_SUB, inst);
    switch (sv.state) {
    case SV_RUNNING:
        human_time((long long)time(NULL) - sv.started, a, sizeof a);
        ram_text(hs.ram_mb, inst, sizeof inst);
        snprintf(b, sizeof b, "Up for %s, with up to %s of memory.", a, inst);
        line2 = b;
        break;
    case SV_STARTING: line2 = "Getting the world ready. The first start takes a minute or two."; break;
    case SV_STOPPING: line2 = "Saving the world, then it stops."; break;
    default: line2 = hi.ok ? "Ready to start." : "Starting it the first time downloads the server and Java.";
    }
    gfx_str_fit(FONT_BODY, x + 2, y + 60, C_SUB, line2, w);
}

static void draw_rows(int px, int py, int pw, int ph)
{
    int x = px + 28, w = pw - 56, y, i, clip[4], sel_i = -1, sel_top = 0, sel_h = 0, total = 0, btn_w = 280;
    int ys[MAX_ROWS], hs_[MAX_ROWS];
    ui_panel(px, py, pw, ph);
    if (footer[0]) {                                /* the footer takes the bottom of the panel */
        int lines = wrap(FONT_SMALL, 0, 0, w, 4, 0, footer, 3, 0), fh = lines * (gfx_line_h(FONT_SMALL) + 4) + 26;
        ph -= fh;
        gfx_rect(px + 1, py + ph, pw - 2, 1, C_LINE);
        wrap(FONT_SMALL, x, py + ph + 14, w, 4, C_SUB, footer, 3, 1);
    }
    for (i = 0; i < nrows; i++) {
        hs_[i] = row_height(&rows[i], w);
        ys[i] = total;
        total += hs_[i];
        if (rows[i].id == sel_id && selectable(&rows[i]) && sel_i < 0) { sel_i = i; sel_top = ys[i]; sel_h = hs_[i]; }
        if (rows[i].kind == R_BUTTON && gfx_str_w(FONT_BODY_SB, rows[i].label) + 56 > btn_w) btn_w = gfx_str_w(FONT_BODY_SB, rows[i].label) + 56;
    }
    /* Keep the selected row in view (and the top of the page when nothing is selected). */
    if (in_page && sel_i >= 0) {
        if (sel_top - scroll_y < 0) scroll_y = sel_top - (sel_i > 0 && !selectable(&rows[sel_i - 1]) ? hs_[sel_i - 1] : 0);
        if (sel_top + sel_h - scroll_y > ph - 44) scroll_y = sel_top + sel_h - (ph - 44);
    }
    if (scroll_y > total - (ph - 44)) scroll_y = total - (ph - 44);
    if (scroll_y < 0) scroll_y = 0;
    gfx_get_clip(clip);
    gfx_clip_intersect(px + 1, py + 1, pw - 2, ph - 2);
    y = py + 22 - scroll_y;
    if (in_page && sel_i >= 0 && rows[sel_i].kind == R_ITEM) {
        float target = (float)(y + sel_top);
        sel_y_hl = sel_y_hl < 0 ? target : ui_ease(sel_y_hl, target, 0.45f);
        ui_select_box((float)(x - 14), sel_y_hl, (float)(w + 28), sel_h - 6.0f, 10);
    } else {
        sel_y_hl = -1;
    }
    for (i = 0; i < nrows; i++) {
        struct row *r = &rows[i];
        int ry = y + ys[i], h = hs_[i], on = in_page && i == sel_i;
        if (ry > py + ph || ry + h < py) continue;
        switch (r->kind) {
        case R_STATUS:
            draw_status(x, ry, w);
            break;
        case R_BUTTON: {
            int bw = btn_w;                        /* one width for a page's buttons */
            uint32_t fill = on ? C_TEXT : r->primary ? C_ACCENT : C_SURFACE2;
            gfx_fill_rrect((float)x, (float)ry, (float)bw, 46, 10, fill, 255);
            gfx_str_vc(FONT_BODY_SB, x + 22, ry + 23, on ? C_INK : C_TEXT, r->label);
            break;
        }
        case R_ITEM: {
            int cy = ry + (h - 6) / 2;
            gfx_str_vc(FONT_BODY_SB, x, cy, C_TEXT, r->label);
            if (r->value[0]) {
                char shown[200];
                int vw, maxw = w - gfx_str_w(FONT_BODY_SB, r->label) - 60;
                snprintf(shown, sizeof shown, "%s%s%s", on && r->cycle ? "<  " : "", r->value, on && r->cycle ? "  >" : "");
                vw = gfx_str_w(FONT_SMALL, shown) + 28;
                if (vw > maxw) vw = maxw;
                gfx_fill_rrect((float)(x + w - vw), cy - 15.0f, (float)vw, 30, 15, on ? C_TEXT : C_SURFACE2, 255);
                gfx_str_fit(FONT_SMALL, x + w - vw + 14, cy - gfx_line_h(FONT_SMALL) / 2, on ? C_INK : C_TEXT, shown, vw - 28);
            }
            break;
        }
        case R_HEAD:
            gfx_str(FONT_LABEL, x, ry + 12, C_TEXT, r->label);
            if (r->value[0]) gfx_str_r(FONT_BODY_SB, x + w, ry + 16, C_SUB, r->value);
            break;
        case R_INFO: {
            int lw = gfx_str_w(FONT_BODY, r->label) + 24;
            gfx_str(FONT_BODY, x, ry + 4, C_SUB, r->label);
            if (lw < w / 3) lw = w / 3;
            gfx_str_fit(FONT_BODY_SB, x + lw, ry + 4, r->color ? r->color : C_TEXT, r->value, w - lw);
            break;
        }
        case R_TEXT:
            wrap(FONT_BODY, x, ry + 2, w, 4, r->color ? r->color : C_TEXT, r->value, 8, 1);
            break;
        case R_NAMES: {
            int k, cx = x, cyy = ry + 2;
            for (k = 0; k < sv.nplayers; k++) {
                int cw = gfx_str_w(FONT_BODY_SB, sv.players[k]) + 32;
                if (cx + cw > x + w && cx > x) { cx = x; cyy += 46; }
                gfx_fill_rrect((float)cx, (float)cyy, (float)cw, 36, 18, C_SURFACE2, 255);
                gfx_circle(cx + 14.0f, cyy + 18.0f, 4, C_OK, 255);
                gfx_str_vc(FONT_BODY_SB, cx + 24, cyy + 18, C_TEXT, sv.players[k]);
                cx += cw + 10;
            }
            break;
        }
        case R_QR: {
            int qs = 226, tx = x + qs + 32, tw = w - qs - 32, ty = ry + 4;
            qr_draw(r->value, x, ry, qs);
            ty += wrap(FONT_BODY, tx, ty, tw, 4, C_TEXT, "Scan this with your phone, or open the link on any device. Sign in to playit.gg there and approve this PC.", 5, 1)
                  * (gfx_line_h(FONT_BODY) + 4) + 12;
            gfx_str_fit(FONT_LABEL, tx, ty, C_TEXT, r->value + 8, tw);       /* without "https://" */
            ty += gfx_line_h(FONT_LABEL) + 14;
            gfx_circle(tx + 6.0f, ty + 11.0f, 5, pi.claim_seen ? C_OK : C_WARN, 255);
            wrap(FONT_BODY, tx + 20, ty, tw - 20, 4, C_SUB, pi.note[0] ? pi.note : "Waiting for the link to be opened.", 3, 1);
            break;
        }
        }
    }
    gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
    if (total > ph - 30) {                          /* a scroll bar */
        float vis = (float)(ph - 44) / total, at = (float)scroll_y / total;
        gfx_fill_rrect((float)(px + pw - 10), py + 12 + at * (ph - 24), 4, vis * (ph - 24), 2, C_LINE, 255);
    }
}

static void draw_menu(int x, int y, int w)
{
    int i, rowh = 56;
    menu_hl = ui_ease(menu_hl, (float)page, 0.45f);
    if (!in_page) ui_select_box((float)x, y + menu_hl * rowh, (float)w, rowh - 8.0f, 10);
    else gfx_fill_rrect((float)x, y + page * rowh + 0.0f, (float)w, rowh - 8.0f, 10, C_SURFACE, 255);
    for (i = 0; i < P_COUNT; i++) {
        int cy = y + i * rowh + (rowh - 8) / 2, on = i == page;
        uint32_t bg = on ? (in_page ? C_SURFACE : C_SEL) : C_BG;
        icon_draw(page_icons[i], x + 26.0f, (float)cy, 24, on ? C_TEXT : C_SUB, bg);
        gfx_str_vc(FONT_BODY_SB, x + 52, cy, on ? C_TEXT : C_SUB, page_names[i]);
        if (i == P_SERVER) {
            uint32_t c = sv.state == SV_RUNNING ? C_OK : sv.state == SV_STOPPED ? C_DIM : C_WARN;
            gfx_circle(x + w - 22.0f, (float)cy, 5, c, 255);
        } else if (i == P_INTERNET && hs.internet) {
            gfx_circle(x + w - 22.0f, (float)cy, 5, pi.state == PI_ON ? C_OK : pi.state == PI_PROBLEM ? C_BAD : C_WARN, 255);
        } else if (i == P_INTERNET && hs.vps) {
            uint32_t c = vps_up() && (vps.check_ok || !vps.check[0]) ? C_OK : sv.state != SV_RUNNING ? C_DIM
                       : vps.why[0] || vps.check[0] ? C_BAD : C_WARN;
            gfx_circle(x + w - 22.0f, (float)cy, 5, c, 255);
        } else if (i == P_PLAYERS && sv.nplayers) {
            char n[8];
            snprintf(n, sizeof n, "%d", sv.nplayers);
            gfx_str_r(FONT_SMALL, x + w - 14, cy - gfx_line_h(FONT_SMALL) / 2, C_SUB, n);
        }
    }
}

static const char *subtitle(void)
{
    switch (sv.state) {
    case SV_RUNNING: return "Your Minecraft server is running";
    case SV_STARTING: return "Your Minecraft server is starting";
    case SV_STOPPING: return "Your Minecraft server is stopping";
    default: return "A Minecraft: Java Edition server on this PC";
    }
}

/* ------------------------------------------------------------------ main */

static void leave(void)
{
    if (sv.state != SV_STOPPED && !hs.told_detach) {
        ui_message("The server keeps running", "It stays on after you leave HostMC, so you and your friends can play on it. "
                                               "Come back to HostMC to stop it.");
        hs.told_detach = 1;
        hs_save();
    }
    ui_back_to_xenithos("hostmc");
}

static void move_sel(int d)
{
    int i, cur = -1;
    for (i = 0; i < nrows; i++)
        if (rows[i].id == sel_id && selectable(&rows[i])) { cur = i; break; }
    for (i = cur + d; i >= 0 && i < nrows; i += d)
        if (selectable(&rows[i])) { sel_id = rows[i].id; sfx(SFX_MOVE); return; }
    if (d < 0) {                                   /* above the first row: show the top of the page */
        if (scroll_y > 0) scroll_y -= 60;
    } else if (cur >= 0) {
        scroll_y += 60;
    }
}

static int first_selectable(void)
{
    int i;
    for (i = 0; i < nrows; i++)
        if (selectable(&rows[i])) return rows[i].id;
    return 0;
}

int main(int argc, char **argv)
{
    int started = 0;
    handoff_args(argc, argv);
    startlog_mark("start");
    plat_init();
    gfx_init();
    ui_init();
    ui_loading_frame("HostMC", "Loading");
    plat_storage_init();
    config_load();
    settings_load();
    profile_load();
    audio_init();
    audio_set_volume(st.volume);
    audio_set_ui_sounds(st.ui_sounds);
    startlog_mark("sounds");

    if (plat_kind() != PLAT_PC) {
        ui_message("HostMC needs a PC", "A Minecraft server runs on a PC or Raspberry Pi with XenithOS. The Xbox can't run one.");
        ui_back_to_xenithos("hostmc");
        return 0;
    }
    if (!getcwd(root, sizeof root - 16)) snprintf(root, sizeof root, ".");
    strcat(root, "/" HM_DIR);
    mkdirs(HM_DIR);
    {
        /* PATH_MAX, not less: Ubuntu builds with _FORTIFY_SOURCE, and its glibc stops a program
         * that gives realpath a smaller buffer, however short the path is. */
        char real[PATH_MAX];
        if (realpath(root, real) && strlen(real) < sizeof root) snprintf(root, sizeof root, "%s", real);   /* as /proc shows it */
    }
    hs_load();
    hi_load();
    sv_attach();
    pi_init();
    vps_init();
    ui_fade_in();

    for (;;) {
        int px = M + 252, py = HEADER_H + 4, pw = SCR_W - M - px, ph = SCR_H - py - 66;
        ui_poll();
        hm_frame();
        if (sv.restart && sv.state == SV_STOPPED) {
            sv.restart = 0;
            do_start();
        }
        if (page != P_CONSOLE) build();

        if (!in_page) {
            int d = ui_nav_y();
            if (d && page + d >= 0 && page + d < P_COUNT) {
                page += d;
                sfx(SFX_MOVE);
                scroll_y = 0;
                console_back = 0;
                if (page != P_CONSOLE) build();
                sel_id = first_selectable();
            }
            if (PRESSED(a) || PRESSED(right) || (cur.lx > STICK_TH && prev.lx <= STICK_TH)) {
                if (page == P_CONSOLE || first_selectable()) {
                    in_page = 1;
                    sfx(SFX_SELECT);
                    sel_id = first_selectable();
                    sel_y_hl = -1;
                    if (page == P_VERSION && !versions_ok[!strcmp(hs.type, "paper")]) {
                        char why[200];
                        int paper = !strcmp(hs.type, "paper");
                        /* The list from last time is enough to show what's newest; Check asks again. */
                        if (versions_get(paper, -1, &versions[paper], why, sizeof why)) versions_ok[paper] = 1;
                    }
                }
            }
            if (PRESSED(b)) { sfx(SFX_BACK); leave(); return 0; }
        } else if (page == P_CONSOLE) {
            if (PRESSED(b) || (PRESSED(left))) { sfx(SFX_BACK); in_page = 0; }
            else console_input();
        } else {
            int dy = ui_nav_y(), dx = ui_nav_x(), i;
            struct row *r = NULL;
            for (i = 0; i < nrows; i++)
                if (rows[i].id == sel_id && selectable(&rows[i])) { r = &rows[i]; break; }
            if (!r && first_selectable()) { sel_id = first_selectable(); }
            if (dy) move_sel(dy);
            if (PRESSED(b)) { sfx(SFX_BACK); in_page = 0; }
            else if (dx && r && r->cycle) act(r->id, dx);
            else if (dx < 0) { sfx(SFX_BACK); in_page = 0; }
            else if (PRESSED(a) && r) { sfx(SFX_SELECT); act(r->id, 0); }
        }

        ui_bg();
        ui_header("HostMC", subtitle());
        draw_menu(M, py, 230);
        if (page == P_CONSOLE) draw_console(px, py, pw, ph);
        else { build(); draw_rows(px, py, pw, ph); }
        if (!in_page) ui_hints("A=Open;B=Leave HostMC");
        else if (page == P_CONSOLE) ui_hints("A=Command;Y=Say;X=Quick commands;LB=Older;RB=Newer;B=Back");
        else {
            const char *hint = "A=Select;B=Back";
            int i;
            for (i = 0; i < nrows; i++)
                if (rows[i].id == sel_id && selectable(&rows[i]) && rows[i].cycle) hint = "A=Change;Left=Less;Right=More;B=Back";
            ui_hints(hint);
        }
        ui_end_frame();
        if (!started) { started = 1; startlog_ready("HOSTMC"); }
    }
}
