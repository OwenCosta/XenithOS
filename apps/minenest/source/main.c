/* MINENEST: Minecraft servers on a XenithOS PC (it replaced HostMC).
 *
 * The servers themselves are run by MineNest (github.com/OwenCosta/minenest), its own project for
 * Windows and Linux: a background service that downloads the server and Java, runs it, installs
 * mods and plugins from Modrinth, keeps backups, and connects playit.gg, a VPS or the supporter
 * relay. This app is its screen on XenithOS, in XenithOS's own look, for a controller or a mouse
 * (api.c talks to the service). The pages, down the left:
 *   Server    which server, its state, where to join, start and stop, a new one
 *   Console   what the server says, and commands typed to it
 *   Mods      the mods, plugins or data packs, and Modrinth's to add
 *   Players   who's on, the whitelist and the operators
 *   Settings  the game's settings, memory, starting with MineNest
 *   Version   the server type, Minecraft version and loader
 *   World     backups, putting one back, a new world
 *   Internet  playit.gg, the supporter relay, Bedrock players
 *
 * Servers keep running after the app closes: the service runs them. Before a server's first start
 * the user agrees to Minecraft's EULA themselves; MineNest never agrees for them. The XenithOS
 * profile that's signed in is handed to MineNest for the supporter relay. A server HostMC made is
 * offered for bringing in (MineNest copies it). On XenithOS Desktop, MineNest's own window can be
 * opened from the Server page. PCs only: an Xbox can't run the server.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "config.h"
#include "gfx.h"
#include "handoff.h"
#include "icons.h"
#include "keyboard.h"
#include "mn.h"
#include "platform.h"
#include "social.h"
#include "startlog.h"
#include "theme.h"
#include "ui.h"

/* ------------------------------------------------------------------ the service's state */

#define MAX_SRV 16
#define MAX_PLAYERS 32

struct srv {
    char id[40], name[48], state[12], flavor[12], flavor_name[16], mc[24], build[48], busy[96], why[240];
    char modpack[80], addon_kind[12], here[64], home[64], pub[128];
    int installed, eula_needed, max_players, port, ram, bedrock_on, autostart, auto_restart, whitelist_on, whitelist_count;
    long long uptime;
    char players[MAX_PLAYERS][17];
    int nplayers;
    struct { char kind[10], state[16], address[128], note[240], check[300], claim[96], bedrock[96]; int up, check_ok; } way;
};
static struct srv srvs[MAX_SRV];
static int nsrv, ssel;                     /* ssel: the server the pages are about */
static char cur_id[40];
static uint64_t srv_at;
static int connected;
static char upd_latest[24];                /* a newer MineNest, when there is one */

static void str_of(const char *obj, const char *key, char *out, int n)
{
    out[0] = 0;
    js_str(js_key(obj, key), out, n);
}

static void parse_srv(const char *o, struct srv *s)
{
    const char *a, *w, *p;
    memset(s, 0, sizeof *s);
    str_of(o, "id", s->id, sizeof s->id);
    str_of(o, "name", s->name, sizeof s->name);
    str_of(o, "state", s->state, sizeof s->state);
    str_of(o, "flavor", s->flavor, sizeof s->flavor);
    str_of(o, "flavor_name", s->flavor_name, sizeof s->flavor_name);
    str_of(o, "minecraft", s->mc, sizeof s->mc);
    str_of(o, "build", s->build, sizeof s->build);
    str_of(o, "busy", s->busy, sizeof s->busy);
    str_of(o, "why", s->why, sizeof s->why);
    str_of(o, "modpack", s->modpack, sizeof s->modpack);
    str_of(o, "addon_kind", s->addon_kind, sizeof s->addon_kind);
    s->installed = js_bool(js_key(o, "installed"));
    s->eula_needed = js_bool(js_key(o, "eula_needed"));
    s->max_players = (int)js_num(js_key(o, "max_players"));
    s->port = (int)js_num(js_key(o, "port"));
    s->ram = (int)js_num(js_key(o, "ram_mb"));
    s->bedrock_on = js_bool(js_key(o, "bedrock_on"));
    s->autostart = js_bool(js_key(o, "autostart"));
    s->auto_restart = js_bool(js_key(o, "auto_restart"));
    s->whitelist_on = js_bool(js_key(o, "whitelist_on"));
    s->whitelist_count = (int)js_num(js_key(o, "whitelist_count"));
    s->uptime = js_num(js_key(o, "uptime"));
    for (p = js_first(js_key(o, "players")); p && s->nplayers < MAX_PLAYERS; p = js_next(p))
        js_str(p, s->players[s->nplayers++], 17);
    a = js_key(o, "addresses");
    str_of(a, "here", s->here, sizeof s->here);
    str_of(a, "home", s->home, sizeof s->home);
    str_of(a, "public", s->pub, sizeof s->pub);
    w = js_key(o, "way");
    str_of(w, "kind", s->way.kind, sizeof s->way.kind);
    str_of(w, "state", s->way.state, sizeof s->way.state);
    str_of(w, "address", s->way.address, sizeof s->way.address);
    str_of(w, "note", s->way.note, sizeof s->way.note);
    str_of(w, "check", s->way.check, sizeof s->way.check);
    str_of(w, "claim_url", s->way.claim, sizeof s->way.claim);
    str_of(w, "bedrock", s->way.bedrock, sizeof s->way.bedrock);
    s->way.up = js_bool(js_key(w, "up"));
    s->way.check_ok = js_bool(js_key(w, "check_ok"));
}

/* The list of servers, every second (the service is on this PC: it answers in a moment). */
static void refresh(int now)
{
    struct mn_resp r;
    const char *o;
    int i;
    if (!now && ui_ms() - srv_at < 1000) return;
    srv_at = ui_ms();
    if (mn_call("GET", "/api/servers", NULL, &r) != 200) {
        connected = 0;
        mn_free(&r);
        return;
    }
    connected = 1;
    nsrv = 0;
    for (o = js_first(r.body); o && nsrv < MAX_SRV; o = js_next(o)) parse_srv(o, &srvs[nsrv++]);
    mn_free(&r);
    ssel = 0;
    for (i = 0; i < nsrv; i++)
        if (!strcmp(srvs[i].id, cur_id)) ssel = i;
    if (nsrv) snprintf(cur_id, sizeof cur_id, "%s", srvs[ssel].id);
}

#define S (&srvs[ssel])
static int running(void) { return nsrv && !strcmp(S->state, "running"); }
static int stopped(void) { return !nsrv || !strcmp(S->state, "stopped"); }

/* Whether there's a newer MineNest (MineNest asks GitHub at most every few hours). */
static void check_update(void)
{
    struct mn_resp r;
    upd_latest[0] = 0;
    if (mn_call("GET", "/api/update", NULL, &r) == 200 && js_bool(js_key(r.body, "newer"))) str_of(r.body, "latest", upd_latest, sizeof upd_latest);
    mn_free(&r);
}

/* Updates MineNest: it puts the new version in place and restarts, starting the servers that
 * were running; this waits for it to answer again. */
static void update_minenest(void)
{
    char why[300], body[200];
    int i;
    snprintf(body, sizeof body, "MineNest %s is out. Servers that are running stop for a moment and start again by themselves.", upd_latest);
    if (!ui_confirm("Update MineNest?", body, "Update", "Not now")) return;
    if (!mn_do("POST", "/api/update", NULL, why, sizeof why)) { message("MineNest wasn't updated", why); return; }
    for (i = 0; i < 120; i++) {                    /* it restarts: up to a minute */
        struct mn_resp r;
        ui_poll();
        ui_loading_frame("MineNest", "Restarting MineNest");
        usleep(500000);
        if (mn_call("GET", "/api/info", NULL, &r) == 200) {
            char v[24] = "";
            str_of(r.body, "version", v, sizeof v);
            mn_free(&r);
            if (!strcmp(v, upd_latest)) break;
        } else {
            mn_free(&r);
        }
    }
    check_update();
    refresh(1);
    ui_toast_icon(ICON_DOWNLOAD);
    ui_toast("MineNest is up to date", NULL);
}

/* "/api/servers/<id><rest>" for the current server. */
static const char *sp(const char *rest)
{
    static char p[200];
    char id[90];
    mn_urlenc(cur_id, id, sizeof id);
    snprintf(p, sizeof p, "/api/servers/%s%s", id, rest);
    return p;
}

/* ------------------------------------------------------------------ rows */

/* Each page is a list of rows, built again every frame from the state. */
enum { R_STATUS, R_BUTTON, R_ITEM, R_HEAD, R_INFO, R_TEXT, R_NAMES, R_QR, R_GAP };

struct row {
    int kind, id, cycle, primary;
    char label[64];
    char value[240];
    uint32_t color;
};

#define MAX_ROWS 96
static struct row rows[MAX_ROWS];
static int nrows;

struct hit { int id, x, y, w, h, pill_x, pill_w; };
static struct hit hits[MAX_ROWS];
static int nhits;
static int reveal;

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

static int selectable(const struct row *r) { return r->kind == R_BUTTON || r->kind == R_ITEM; }

/* Row ids: what A (or left and right) does. */
enum {
    A_START = 1, A_STOP, A_RESTART, A_FORCE, A_SERVER, A_NEW, A_HOSTMC, A_WINDOW, A_EULA, A_CONNECT, A_UPDATE,
    M_SEARCH, M_CHECK, M_UPDATE, M_FILE_INFO,
    P_ADD_WL, P_ADD_OP, P_WL_ON,
    S_MODE, S_DIFF, S_MAX, S_MOTD, S_ONLINE, S_RAM, S_AUTOSTART, S_RESTART, S_PORT, S_RENAME, S_DELETE,
    V_TYPE, V_VERSION, V_BUILD, V_CHANGE, V_REPAIR,
    W_BACKUP, W_RESET,
    N_WAY, N_CHECK, N_FORGET, N_RELAY_NAME, N_RELAY_STATUS, N_BEDROCK, N_GEYSER, N_REALIP,
    ID_ADDON = 100, ID_PLAYER = 200, ID_WL = 300, ID_OP = 400, ID_BACKUP = 500,
};

enum { P_SERVER, P_CONSOLE, P_MODS, P_PLAYERS, P_SETTINGS, P_VERSION, P_WORLD, P_INTERNET, P_COUNT };
static const char *const page_names[P_COUNT] = {"Server", "Console", "Mods", "Players", "Settings", "Version", "World", "Internet"};
static const int page_icons[P_COUNT] = {ICON_SERVER, ICON_SYSTEM, ICON_DOWNLOAD, ICON_PROFILE, ICON_SETTINGS, ICON_REFRESH, ICON_HOME, ICON_NETWORK};

static int page, in_page, sel_id, scroll_y, console_back;
static char footer[300];
static float menu_hl, sel_y_hl = -1;

static const char *const modes[4] = {"survival", "creative", "adventure", "spectator"};
static const char *const mode_names[4] = {"Survival", "Creative", "Adventure", "Spectator"};
static const char *const diffs[4] = {"peaceful", "easy", "normal", "hard"};
static const char *const diff_names[4] = {"Peaceful", "Easy", "Normal", "Hard"};
static const int ram_steps[] = {1024, 1536, 2048, 3072, 4096, 5120, 6144, 8192, 10240, 12288, 16384};
#define NRAM ((int)(sizeof ram_steps / sizeof ram_steps[0]))
static const char *const flavor_ids[6] = {"paper", "vanilla", "fabric", "neoforge", "forge", "purpur"};
static const char *const flavor_names[6] = {"Paper", "Vanilla", "Fabric", "NeoForge", "Forge", "Purpur"};
static const char *const ways[3] = {"", "playit", "relay"};
static const char *const way_names[3] = {"Only at home", "playit.gg", "Supporter relay"};

static int index_of(const char *const *list, int n, const char *v, int fallback)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(list[i], v)) return i;
    return fallback;
}

/* ------------------------------------------------------------------ page data */

/* What a page shows besides the server list, fetched when the page opens and every few seconds. */
static char *props;                        /* server.properties as JSON */
static char *addons, *players_json, *backups_json, *relay_json;
static uint64_t data_at;
static char data_for[40];
static int data_page = -1;

static char *fetch(const char *path)
{
    struct mn_resp r;
    if (mn_call("GET", path, NULL, &r) != 200) { mn_free(&r); return NULL; }
    return r.body;
}

static void replace(char **slot, char *v)
{
    free(*slot);
    *slot = v;
}

static void load_page_data(int force)
{
    if (!nsrv) return;
    if (!force && data_page == page && !strcmp(data_for, cur_id) && ui_ms() - data_at < 3000) return;
    data_page = page;
    data_at = ui_ms();
    snprintf(data_for, sizeof data_for, "%s", cur_id);
    switch (page) {
    case P_SETTINGS: replace(&props, fetch(sp("/properties"))); break;
    case P_MODS: replace(&addons, fetch(sp("/addons"))); break;
    case P_PLAYERS: replace(&players_json, fetch(sp("/players"))); break;
    case P_WORLD: replace(&backups_json, fetch(sp("/backups"))); break;
    case P_INTERNET: replace(&relay_json, fetch("/api/account")); break;
    }
}

static void prop(const char *key, char *out, int n, const char *fallback)
{
    out[0] = 0;
    if (!props || !js_str(js_key(props, key), out, n)) snprintf(out, n, "%s", fallback);
}

static int has_prop(const char *key) { return props && js_key(props, key) != NULL; }

/* ------------------------------------------------------------------ text helpers */

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

static void message(const char *title, const char *body)
{
    sfx(SFX_ERROR);
    ui_message(title, body);
}

static int ask_text(const char *title, const char *hint, char *buf, int max)
{
    int ok = keyboard(title, hint, buf, max, KB_LINE);
    ui_settle_input();
    return ok;
}

/* A call to MineNest that should work; a message says why when it doesn't. */
static int call(const char *method, const char *path, const char *json, const char *fail_title)
{
    char why[400];
    if (mn_do(method, path, json, why, sizeof why)) { refresh(1); load_page_data(1); return 1; }
    message(fail_title, why);
    refresh(1);
    return 0;
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
        d = ui_nav_y();
        if (d && sel + d >= 0 && sel + d < n) { sel += d; sfx(SFX_MOVE); }
        if (ui_desktop) {
            if (mouse.wheel) {
                first -= mouse.wheel;
                if (first > n - visible) first = n - visible;
                if (first < 0) first = 0;
                if (sel < first) sel = first;
                if (sel >= first + visible) sel = first + visible - 1;
            }
            for (i = first; i < n && i < first + visible; i++)
                if (ui_mouse_in(M, top + (i - first) * rowh, w, rowh - 8)) {
                    if ((mouse.dx || mouse.dy) && sel != i) { sel = i; sfx(SFX_MOVE); }
                    if (mouse.clicks & 1) { sfx(SFX_SELECT); ui_settle_input(); return i; }
                }
        }
        if (PRESSED(a)) { sfx(SFX_SELECT); ui_settle_input(); return sel; }
        if (PRESSED(b)) { sfx(SFX_BACK); ui_settle_input(); return -1; }
        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;
        hl = ui_ease(hl, (float)(sel - first), 0.45f);
        ui_bg();
        ui_header(title, sub);
        ui_select_box((float)M, top + hl * rowh, (float)w, rowh - 8.0f, 10);
        for (i = first; i < n && i < first + visible; i++)
            gfx_str_fit(FONT_BODY_SB, M + 22, top + (i - first) * rowh + (rowh - 8) / 2 - gfx_line_h(FONT_BODY_SB) / 2, C_TEXT, items[i], w - 44);
        if (n > visible) {
            char pos[24];
            snprintf(pos, sizeof pos, "%d of %d", sel + 1, n);
            gfx_str_r(FONT_SMALL, SCR_W - M, SCR_H - 46, C_DIM, pos);
        }
        ui_hints(ui_desktop ? "Click=Choose;Wheel=Scroll;A=Choose;B=Back" : "A=Choose;B=Back");
        ui_end_frame();
    }
}

/* Minecraft's EULA: the user's own yes, with the address to read it at (and its QR code). */
static int eula_screen(void)
{
    static const char *const opts[] = {"I've read it and I agree", "Not now"};
    char body[400];
    snprintf(body, sizeof body, "To run a Minecraft server you need to agree to Minecraft's End User License Agreement. "
                                "Read it at %s (on your phone or another device). MineNest never agrees for you.", EULA_URL + 8);
    if (ui_dialog("Minecraft's EULA", body, opts, 2) != 0) return 0;
    return call("POST", sp("/eula"), "{\"agree\":true}", "The EULA couldn't be saved");
}

/* ------------------------------------------------------------------ actions */

static void do_start(void)
{
    if (!nsrv || !S->installed) return;
    if (S->eula_needed && !eula_screen()) return;
    if (call("POST", sp("/start"), NULL, "The server didn't start")) {
        ui_toast_icon(ICON_SERVER);
        ui_toast("Starting the server", "The first start takes a minute or two.");
    }
}

static int pick_version(const char *flavor, char *out, int n)
{
    static char items[200][64];
    char path[80];
    struct mn_resp r;
    const char *v;
    int k = 0, c;
    snprintf(path, sizeof path, "/api/versions/%s", flavor);
    ui_busy_begin("Looking for Minecraft versions", NULL);
    mn_call("GET", path, NULL, &r);
    ui_busy_end();
    if (r.status != 200) { mn_free(&r); message("No list of versions", "MineNest couldn't get it. Is this PC online?"); return 0; }
    snprintf(items[k++], 64, "The newest");
    for (v = js_first(r.body); v && k < 200; v = js_next(v)) js_str(v, items[k++], 64);
    mn_free(&r);
    c = pick("Minecraft version", "Older ones too, for older mods and worlds", items, k, 0);
    if (c < 0) return 0;
    snprintf(out, n, "%s", c == 0 ? "latest" : items[c]);
    return 1;
}

/* A new server: its type (or a modpack), its Minecraft version, its name, then the EULA. */
static void new_server(void)
{
    static char items[8][64];
    char version[24] = "latest", name[48] = "", json[600], ename[100], pack[64] = "", why[400];
    struct mn_resp r;
    int t, i;
    for (i = 0; i < 6; i++) snprintf(items[i], 64, "%s", flavor_names[i]);
    snprintf(items[6], 64, "A modpack from Modrinth");
    t = pick("New server", "Paper takes plugins; Fabric, NeoForge and Forge take mods", items, 7, 0);
    if (t < 0) return;
    if (t == 6) {
        static char packs[20][64], ids[20][40];
        char q[64] = "", eq[200], path[300];
        const char *h;
        int k = 0, c;
        if (!ask_text("Find a modpack", "Like Cobblemon, or leave it empty for the most popular", q, 60)) return;
        mn_urlenc(q, eq, sizeof eq);
        snprintf(path, sizeof path, "/api/modpacks?q=%s", eq);
        ui_busy_begin("Searching Modrinth", q);
        mn_call("GET", path, NULL, &r);
        ui_busy_end();
        for (h = js_first(js_key(r.body, "hits")); h && k < 20; h = js_next(h)) {
            str_of(h, "title", packs[k], 64);
            str_of(h, "id", ids[k], 40);
            k++;
        }
        mn_free(&r);
        if (!k) { message("No modpacks found", "Try other words."); return; }
        c = pick("Choose a modpack", "From Modrinth", packs, k, 0);
        if (c < 0) return;
        snprintf(pack, sizeof pack, "%s", ids[c]);
        snprintf(name, sizeof name, "%.40s", packs[c]);
    } else if (!pick_version(flavor_ids[t], version, sizeof version)) {
        return;
    }
    if (!name[0]) snprintf(name, sizeof name, "My Server");
    if (!ask_text("Name the server", "Up to 40 letters", name, 40) || !name[0]) return;
    mn_escape(name, ename, sizeof ename);
    if (pack[0]) snprintf(json, sizeof json, "{\"name\":\"%s\",\"modpack\":\"%s\"}", ename, pack);
    else snprintf(json, sizeof json, "{\"name\":\"%s\",\"flavor\":\"%s\",\"version\":\"%s\"}", ename, flavor_ids[t], version);
    if (!mn_do("POST", "/api/servers", json, why, sizeof why)) { message("The server couldn't be made", why); refresh(1); return; }
    refresh(1);
    for (i = 0; i < nsrv; i++)
        if (!strcmp(srvs[i].name, name)) { ssel = i; snprintf(cur_id, sizeof cur_id, "%s", srvs[i].id); }
    ui_toast_icon(ICON_SERVER);
    ui_toast("The server is ready", "Start it on the Server page.");
}

/* HostMC's server, in XenithOS's data folder: offered for bringing in until it has been. */
static int hostmc_offer(void)
{
    struct stat st;
    return stat(HOSTMC_SERVER "/server.properties", &st) == 0 && stat("hostmc/brought-in.txt", &st) != 0;
}

static void bring_in_hostmc(void)
{
    char real[PATH_MAX], esc[PATH_MAX * 2], json[PATH_MAX * 2 + 80], why[400];
    FILE *f;
    if (!realpath(HOSTMC_SERVER, real)) return;
    mn_escape(real, esc, sizeof esc);
    snprintf(json, sizeof json, "{\"path\":\"%s\",\"name\":\"HostMC server\"}", esc);
    if (!mn_do("POST", "/api/import", json, why, sizeof why)) { message("HostMC's server couldn't be brought in", why); return; }
    if ((f = fopen("hostmc/brought-in.txt", "w"))) { fputs("MineNest has a copy of this server.\n", f); fclose(f); }
    refresh(1);
    ui_toast_icon(ICON_SERVER);
    ui_toast("HostMC's server is in MineNest", "Its world, plugins and settings came with it.");
}

static void open_window(void)
{
    const char *e = mn_engine();
    char *argv[3];
    if (!e) return;
    argv[0] = (char *)e;
    argv[1] = "gui";
    argv[2] = NULL;
    mn_spawn(argv);
    ui_toast_icon(ICON_SERVER);
    ui_toast("Opening MineNest's window", "Everything is there too, with a mouse and keyboard.");
}

static void set_prop(const char *key, const char *value)
{
    char json[400], ev[300];
    mn_escape(value, ev, sizeof ev);
    snprintf(json, sizeof json, "{\"%s\":\"%s\"}", key, ev);
    call("PUT", sp("/properties"), json, "The setting couldn't be saved");
}

static void patch(const char *json) { call("PATCH", sp(""), json, "The setting couldn't be saved"); }

/* ------------------------------------------------------------------ the pages */

static char want_flavor[12], want_version[24] = "latest", want_build[48];

static void build_server(void)
{
    char v[260];
    if (!connected) {
        add(R_TEXT, 0, NULL, mn_engine() ? "MineNest isn't answering right now." : "MineNest isn't installed on this PC. Update XenithOS in the Store to get it.");
        if (mn_engine()) add(R_BUTTON, A_CONNECT, "Start MineNest", NULL)->primary = 1;
        return;
    }
    if (!nsrv) {
        add(R_TEXT, 0, NULL, "No servers yet. Make one and you can be playing on it in a minute or two.");
        add(R_BUTTON, A_NEW, "Make a server", NULL)->primary = 1;
        if (hostmc_offer()) add(R_BUTTON, A_HOSTMC, "Bring in HostMC's server", NULL);
        if (ui_desktop && mn_engine()) add(R_BUTTON, A_WINDOW, "Open MineNest's window", NULL);
        return;
    }
    if (nsrv > 1) add(R_ITEM, A_SERVER, "Server", S->name)->cycle = 1;
    add(R_STATUS, 0, NULL, NULL);
    if (S->busy[0]) {
        snprintf(v, sizeof v, "MineNest is busy with %s.", S->busy);
        add(R_TEXT, 0, NULL, v)->color = C_WARN;
    }
    if (S->why[0] && stopped()) {
        snprintf(v, sizeof v, "It stopped by itself: %s", S->why);
        add(R_TEXT, 0, NULL, v)->color = C_BAD;
    }
    if (stopped()) {
        if (S->eula_needed) add(R_BUTTON, A_EULA, "Agree to Minecraft's EULA", NULL)->primary = 1;
        add(R_BUTTON, A_START, "Start", NULL)->primary = !S->eula_needed;
    } else if (!strcmp(S->state, "stopping")) {
        add(R_BUTTON, A_FORCE, "End it now, without saving", NULL);
    } else {
        add(R_BUTTON, A_STOP, "Stop", NULL);
        if (running()) add(R_BUTTON, A_RESTART, "Restart", NULL);
    }
    add(R_HEAD, 0, "Where to join", NULL);
    add(R_INFO, 0, "This PC", S->here);
    if (S->home[0]) add(R_INFO, 0, "Your home network", S->home);
    if (S->pub[0]) add(R_INFO, 0, "The internet", S->pub)->color = C_OK;
    else add(R_INFO, 0, "The internet", S->way.kind[0] ? (running() ? (S->way.note[0] ? S->way.note : "Connecting") : "Once the server is on") : "Not set up (Internet page)")->color = C_DIM;
    if (S->nplayers) {
        snprintf(v, sizeof v, "%d of %d", S->nplayers, S->max_players);
        add(R_HEAD, 0, "Players", v);
        add(R_NAMES, 0, NULL, NULL);
    }
    add(R_HEAD, 0, "More", NULL);
    if (upd_latest[0]) {
        char u[64];
        snprintf(u, sizeof u, "Update MineNest to %s", upd_latest);
        add(R_BUTTON, A_UPDATE, u, NULL)->primary = 1;
    }
    add(R_BUTTON, A_NEW, "Make another server", NULL);
    if (hostmc_offer()) add(R_BUTTON, A_HOSTMC, "Bring in HostMC's server", NULL);
    if (ui_desktop && mn_engine()) add(R_BUTTON, A_WINDOW, "Open MineNest's window", NULL);
}

static const char *kind_word(void)
{
    return !strcmp(S->addon_kind, "plugin") ? "plugins" : !strcmp(S->addon_kind, "datapack") ? "data packs" : "mods";
}

static void build_mods(void)
{
    char head[64], v[160], t[80];
    const char *a;
    int i = 0;
    if (!S->installed) { add(R_TEXT, 0, NULL, "The server software isn't installed yet."); return; }
    snprintf(head, sizeof head, "Find %s on Modrinth", kind_word());
    add(R_BUTTON, M_SEARCH, head, NULL)->primary = 1;
    add(R_BUTTON, M_CHECK, "Check for updates", NULL);
    add(R_BUTTON, M_UPDATE, "Update them all", NULL);
    snprintf(head, sizeof head, "Installed %s", kind_word());
    add(R_HEAD, 0, head, NULL);
    for (a = js_first(js_key(addons, "addons")); a && i < 60; a = js_next(a), i++) {
        char up[40] = "";
        str_of(a, "title", t, sizeof t);
        str_of(a, "update", up, sizeof up);
        str_of(a, "version", v, sizeof v);
        if (up[0]) snprintf(v, sizeof v, "Update: %s", up);
        add(R_ITEM, ID_ADDON + i, t, v);
    }
    for (a = js_first(js_key(addons, "other")); a && i < 60; a = js_next(a), i++) {
        js_str(a, t, sizeof t);
        add(R_ITEM, ID_ADDON + i, t, "added by hand");
    }
    if (!i) add(R_TEXT, 0, NULL, "None yet.")->color = C_DIM;
    snprintf(footer, sizeof footer, "Only %s made for %s %s show up, and what they need comes with them. Changes count after a restart.",
             kind_word(), S->flavor_name, S->mc);
}

static int names_of(const char *list, char out[][17], int max)
{
    const char *p;
    int n = 0;
    for (p = js_first(js_key(players_json, list)); p && n < max; p = js_next(p)) js_str(p, out[n++], 17);
    return n;
}

static char wl[40][17], ops[40][17];
static int nwl, nop;

static void build_players(void)
{
    char v[64];
    int i;
    snprintf(v, sizeof v, "%d of %d", S->nplayers, S->max_players);
    add(R_HEAD, 0, "On now", v);
    for (i = 0; i < S->nplayers; i++) add(R_ITEM, ID_PLAYER + i, S->players[i], "Choose")->color = 0;
    if (!S->nplayers) add(R_TEXT, 0, NULL, running() ? "Nobody is on." : "The server isn't running.")->color = C_DIM;
    add(R_HEAD, 0, "Whitelist", NULL);
    add(R_ITEM, P_WL_ON, "Only these players can join", js_bool(js_key(players_json, "whitelist_on")) ? "On" : "Off")->cycle = 1;
    nwl = names_of("whitelist", wl, 40);
    for (i = 0; i < nwl; i++) add(R_ITEM, ID_WL + i, wl[i], "Remove");
    add(R_BUTTON, P_ADD_WL, "Add a player", NULL);
    add(R_HEAD, 0, "Operators", "can use every command");
    nop = names_of("ops", ops, 40);
    for (i = 0; i < nop; i++) add(R_ITEM, ID_OP + i, ops[i], "Remove");
    add(R_BUTTON, P_ADD_OP, "Make a player an operator", NULL);
}

static void build_settings(void)
{
    char v[200];
    int i;
    add(R_HEAD, 0, "Game", NULL);
    prop("gamemode", v, sizeof v, "survival");
    add(R_ITEM, S_MODE, "Game mode", mode_names[index_of(modes, 4, v, 0)])->cycle = 1;
    prop("difficulty", v, sizeof v, "easy");
    add(R_ITEM, S_DIFF, "Difficulty", diff_names[index_of(diffs, 4, v, 1)])->cycle = 1;
    prop("max-players", v, sizeof v, "20");
    add(R_ITEM, S_MAX, "Most players at once", v)->cycle = 1;
    prop("motd", v, sizeof v, "");
    add(R_ITEM, S_MOTD, "Message in the server list", v);
    if (!props || has_prop("online-mode")) {
        prop("online-mode", v, sizeof v, "true");
        add(R_ITEM, S_ONLINE, "Check accounts with Mojang", strcmp(v, "false") ? "On" : "Off")->cycle = 1;
    }
    snprintf(v, sizeof v, "%d", S->port);
    add(R_ITEM, S_PORT, "Port", v);
    add(R_HEAD, 0, "MineNest", NULL);
    for (i = 0; i < NRAM - 1 && ram_steps[i] < S->ram; i++) { }
    snprintf(v, sizeof v, "%.1f GB", S->ram / 1024.0);
    add(R_ITEM, S_RAM, "Memory", v)->cycle = 1;
    add(R_ITEM, S_AUTOSTART, "Start it with MineNest", S->autostart ? "On" : "Off")->cycle = 1;
    add(R_ITEM, S_RESTART, "Start it again if it crashes", S->auto_restart ? "On" : "Off")->cycle = 1;
    add(R_BUTTON, S_RENAME, "Rename", NULL);
    add(R_BUTTON, S_DELETE, "Delete this server", NULL);
    snprintf(footer, sizeof footer, "%s", stopped() ? "Changes count from the next start." : "Changes count after a restart.");
}

static void build_version(void)
{
    char v[160];
    if (!want_flavor[0] || strcmp(data_for, cur_id)) {
        snprintf(want_flavor, sizeof want_flavor, "%s", S->flavor[0] ? S->flavor : "paper");
        snprintf(want_version, sizeof want_version, "%s", S->mc[0] ? S->mc : "latest");
        want_build[0] = 0;
    }
    add(R_HEAD, 0, "Now", NULL);
    snprintf(v, sizeof v, "%s %s%s%s", S->flavor_name, S->mc, S->build[0] ? ", " : "", S->build);
    add(R_INFO, 0, "Running", S->installed ? v : "Not installed");
    if (S->modpack[0]) add(R_INFO, 0, "Modpack", S->modpack);
    add(R_HEAD, 0, "Change it", "the world is backed up first");
    add(R_ITEM, V_TYPE, "Server type", flavor_names[index_of(flavor_ids, 6, want_flavor, 0)])->cycle = 1;
    add(R_ITEM, V_VERSION, "Minecraft version", !strcmp(want_version, "latest") ? "The newest" : want_version);
    if (strcmp(want_flavor, "vanilla")) add(R_ITEM, V_BUILD, "Loader version", want_build[0] ? want_build : "Recommended");
    add(R_BUTTON, V_CHANGE, "Change to this", NULL)->primary = 1;
    add(R_BUTTON, V_REPAIR, "Download it again", NULL);
    if (!stopped()) snprintf(footer, sizeof footer, "Stop the server to change it.");
}

static void build_world(void)
{
    const char *b;
    char when[64], v[160], why[100], name[64];
    int i = 0;
    add(R_BUTTON, W_BACKUP, "Back up now", NULL)->primary = 1;
    add(R_HEAD, 0, "Backups", NULL);
    for (b = js_first(js_key(backups_json, "backups")); b && i < 30; b = js_next(b), i++) {
        time_t t = (time_t)js_num(js_key(b, "time"));
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(when, sizeof when, "%d %b %Y, %H:%M", &tm);
        str_of(b, "why", why, sizeof why);
        str_of(b, "name", name, sizeof name);
        snprintf(v, sizeof v, "%.1f MB", js_num(js_key(b, "size")) / 1048576.0);
        add(R_ITEM, ID_BACKUP + i, when, v);
    }
    if (!i) add(R_TEXT, 0, NULL, "No backups yet.")->color = C_DIM;
    add(R_BUTTON, W_RESET, "Make a new world", NULL);
    snprintf(footer, sizeof footer, "Putting a backup back, or a new world, backs the world up first, so nothing is lost.");
}

static void build_internet(void)
{
    char v[300], name[40] = "", msgtxt[260] = "", address[128] = "", step[16] = "", patreon[160] = "";
    int w = index_of(ways, 3, S->way.kind, 0);
    if (!strcmp(S->way.kind, "vps")) {
        add(R_INFO, 0, "Way in", "Your own VPS");
        add(R_TEXT, 0, NULL, "Set up in MineNest's window (Open MineNest's window on the Server page).")->color = C_DIM;
    }
    add(R_ITEM, N_WAY, "Friends elsewhere join through", way_names[w])->cycle = 1;
    if (S->way.kind[0]) {
        add(R_HEAD, 0, "How it's going", NULL);
        if (S->way.address[0]) add(R_INFO, 0, "Address", S->way.address)->color = S->way.up ? C_OK : C_TEXT;
        snprintf(v, sizeof v, "%s", S->way.up ? (S->way.check[0] ? S->way.check : "Connected.")
                                    : running() ? (S->way.note[0] ? S->way.note : S->way.state) : "It connects when the server starts.");
        add(R_TEXT, 0, NULL, v)->color = S->way.up ? C_OK : !strcmp(S->way.state, "problem") ? C_BAD : C_SUB;
        if (S->way.claim[0] && running()) {
            struct row *r = add(R_QR, 0, "Approve this PC on playit.gg: scan this with your phone, or open the link on any device.", S->way.claim);
            (void)r;
        }
        if (S->way.bedrock[0]) add(R_INFO, 0, "Bedrock players", S->way.bedrock);
        if (strcmp(S->way.kind, "playit") && running()) add(R_BUTTON, N_CHECK, "Check from the internet", NULL);
        if (!strcmp(S->way.kind, "playit")) add(R_BUTTON, N_FORGET, "Forget playit.gg on this PC", NULL);
    }
    if (!strcmp(S->way.kind, "relay") || w == 2) {
        str_of(relay_json, "name", name, sizeof name);
        str_of(relay_json, "message", msgtxt, sizeof msgtxt);
        str_of(relay_json, "address", address, sizeof address);
        str_of(relay_json, "step", step, sizeof step);
        str_of(relay_json, "patreon", patreon, sizeof patreon);
        add(R_HEAD, 0, "Supporter relay", "for supporters on Patreon");
        if (msgtxt[0]) add(R_TEXT, 0, NULL, msgtxt);
        if (name[0]) add(R_INFO, 0, "Your address", address)->color = C_OK;
        if ((!strcmp(step, "patreon") || !strcmp(step, "tier")) && patreon[0]) add(R_QR, 0, "Become a supporter on Patreon: scan this with your phone.", patreon);
        if (!strcmp(step, "ok")) add(R_BUTTON, N_RELAY_NAME, name[0] ? "Change the name" : "Pick a name", NULL);
        add(R_BUTTON, N_RELAY_STATUS, "Check my membership", NULL);
        if (!strcmp(S->flavor, "paper") || !strcmp(S->flavor, "purpur"))
            add(R_ITEM, N_REALIP, "Players' real addresses", js_bool(js_key(relay_json, "proxy_able")) ? (S->bedrock_on ? "Off with Bedrock" : "On") : "Not yet")->cycle = 0;
    }
    if (!strcmp(S->flavor, "paper") || !strcmp(S->flavor, "purpur")) {
        add(R_HEAD, 0, "Bedrock players", "phones, consoles, Windows");
        add(R_ITEM, N_BEDROCK, "Let Bedrock players join", S->bedrock_on ? "On" : "Off")->cycle = 1;
        if (S->bedrock_on) add(R_BUTTON, N_GEYSER, "Update Geyser", NULL);
    }
    snprintf(footer, sizeof footer, "Players at home and on this PC always join directly. Your OCAccount's XenithOS profile is the one the relay knows.");
}

static void build(void)
{
    nrows = 0;
    footer[0] = 0;
    if (page != P_SERVER && (!connected || !nsrv)) {
        add(R_TEXT, 0, NULL, connected ? "Make a server first, on the Server page." : "MineNest isn't answering. See the Server page.");
        return;
    }
    switch (page) {
    case P_SERVER: build_server(); break;
    case P_MODS: build_mods(); break;
    case P_PLAYERS: build_players(); break;
    case P_SETTINGS: build_settings(); break;
    case P_VERSION: build_version(); break;
    case P_WORLD: build_world(); break;
    case P_INTERNET: build_internet(); break;
    }
}

/* ------------------------------------------------------------------ acting on rows */

static void search_addons(void)
{
    static char items[20][64], ids[20][40];
    char q[64] = "", eq[200], path[300], title[64], json[120];
    struct mn_resp r;
    const char *h;
    int k = 0, c;
    snprintf(title, sizeof title, "Find %s", kind_word());
    if (!ask_text(title, "Words to look for, or leave it empty for the most popular", q, 60)) return;
    mn_urlenc(q, eq, sizeof eq);
    snprintf(path, sizeof path, "%s?q=%s", sp("/addons/search"), eq);
    ui_busy_begin("Searching Modrinth", q);
    mn_call("GET", path, NULL, &r);
    ui_busy_end();
    if (r.status != 200) { message("Modrinth couldn't be searched", "Is this PC online?"); mn_free(&r); return; }
    for (h = js_first(js_key(r.body, "hits")); h && k < 20; h = js_next(h)) {
        char t[60];
        str_of(h, "title", t, sizeof t);
        snprintf(items[k], 64, "%s%s", t, js_bool(js_key(h, "installed")) ? "  (installed)" : "");
        str_of(h, "id", ids[k], 40);
        k++;
    }
    mn_free(&r);
    if (!k) { message("Nothing found", "Nothing on Modrinth fits that for this server. Try other words."); return; }
    c = pick(title, "From Modrinth, for this server's type and version", items, k, 0);
    if (c < 0) return;
    snprintf(json, sizeof json, "{\"project\":\"%s\"}", ids[c]);
    if (call("POST", sp("/addons"), json, "It couldn't be installed")) {
        ui_toast_icon(ICON_DOWNLOAD);
        ui_toast("Installed", running() ? "Restart the server to load it." : items[c]);
    }
}

static void addon_menu(int i)
{
    static const char *const opts[] = {"Remove it", "Update it", "Cancel"};
    const char *a = js_first(js_key(addons, "addons"));
    char project[48] = "", title[80] = "", path[200], json[120], enc[100];
    int k, n = 0, c;
    for (k = 0; a && k < i; k++) a = js_next(a);
    if (a) { str_of(a, "project", project, sizeof project); str_of(a, "title", title, sizeof title); }
    else {
        for (a = js_first(js_key(addons, "addons")); a; a = js_next(a)) n++;
        a = js_first(js_key(addons, "other"));
        for (k = n; a && k < i; k++) a = js_next(a);
        if (!a) return;
        js_str(a, project, sizeof project);
        snprintf(title, sizeof title, "%s", project);
    }
    c = ui_dialog(title, NULL, opts, 3);
    mn_urlenc(project, enc, sizeof enc);
    if (c == 0) {
        snprintf(path, sizeof path, "%s%s", sp("/addons/"), enc);
        call("DELETE", path, NULL, "It couldn't be removed");
    } else if (c == 1) {
        snprintf(json, sizeof json, "{\"project\":\"%s\"}", project);
        call("POST", sp("/addons/update"), json, "It couldn't be updated");
    }
}

static void player_act(const char *action, const char *name)
{
    char json[160];
    snprintf(json, sizeof json, "{\"action\":\"%s\",\"name\":\"%s\"}", action, name);
    call("POST", sp("/players"), json, "That didn't work");
}

static void player_menu(const char *name)
{
    static const char *const opts[] = {"Make operator", "Kick", "Ban", "Cancel"};
    static const char *const acts[] = {"op", "kick", "ban"};
    int c = ui_dialog(name, NULL, opts, 4);
    if (c >= 0 && c < 3) player_act(acts[c], name);
}

static void ask_player(const char *action, const char *title)
{
    char name[20] = "";
    if (!ask_text(title, "Their Minecraft name", name, 16) || !name[0]) return;
    player_act(action, name);
}

static void relay_name(void)
{
    char name[24] = "", suggest[24] = "", json[100];
    str_of(relay_json, "name", name, sizeof name);
    str_of(relay_json, "suggest", suggest, sizeof suggest);
    if (!name[0]) snprintf(name, sizeof name, "%s", suggest);
    if (!ask_text("Your server's name", "3 to 20 small letters, numbers and hyphens", name, 20) || !name[0]) return;
    snprintf(json, sizeof json, "{\"name\":\"%s\"}", name);
    if (call("POST", "/api/relay/claim", json, "That name didn't work")) {
        ui_toast_icon(ICON_NETWORK);
        ui_toast("It's yours", "Friends can use it once the server is on.");
    }
}

static void choose_build(void)
{
    static char items[80][64], ids[80][48];
    char path[120];
    struct mn_resp r;
    const char *b;
    int k = 0, c;
    snprintf(path, sizeof path, "/api/builds/%s/%s", want_flavor, want_version);
    ui_busy_begin("Looking for loader versions", NULL);
    mn_call("GET", path, NULL, &r);
    ui_busy_end();
    snprintf(items[k], 64, "Recommended");
    ids[k++][0] = 0;
    for (b = js_first(r.body); b && k < 80; b = js_next(b)) {
        str_of(b, "label", items[k], 64);
        str_of(b, "id", ids[k], 48);
        k++;
    }
    mn_free(&r);
    c = pick("Loader version", "Recommended is MineNest's pick", items, k, 0);
    if (c >= 0) snprintf(want_build, sizeof want_build, "%s", ids[c]);
}

static int step_of(int v, const int *steps, int n, int dir)
{
    int i;
    for (i = 0; i < n - 1 && steps[i] < v; i++) { }
    i += dir ? dir : 1;
    if (i < 0) i = 0;
    if (i >= n) i = dir ? n - 1 : 0;
    return steps[i];
}

static void act(int id, int dir)
{
    char v[200], json[300];
    int d = dir ? dir : 1, i;
    switch (id) {
    case A_CONNECT: if (!mn_connect(1)) message("MineNest didn't start", "Try again, or update XenithOS in the Store."); refresh(1); return;
    case A_SERVER:
        if (nsrv > 1) {
            ssel = (ssel + d + nsrv) % nsrv;
            snprintf(cur_id, sizeof cur_id, "%s", srvs[ssel].id);
            sfx(SFX_MOVE);
            load_page_data(1);
        }
        return;
    case A_START: do_start(); return;
    case A_EULA: eula_screen(); return;
    case A_STOP: call("POST", sp("/stop"), NULL, "The server didn't stop"); return;
    case A_FORCE:
        if (ui_confirm("End it now?", "Anything since the last save is lost.", "End it", "Wait")) call("POST", sp("/kill"), NULL, "It couldn't be ended");
        return;
    case A_RESTART: call("POST", sp("/restart"), NULL, "The server didn't restart"); return;
    case A_NEW: new_server(); return;
    case A_HOSTMC: bring_in_hostmc(); return;
    case A_WINDOW: open_window(); return;
    case A_UPDATE: update_minenest(); return;
    case M_SEARCH: search_addons(); return;
    case M_CHECK: call("POST", sp("/addons/check"), NULL, "Couldn't check for updates"); return;
    case M_UPDATE: call("POST", sp("/addons/update"), "{}", "They couldn't be updated"); return;
    case P_WL_ON:
        set_prop("white-list", js_bool(js_key(players_json, "whitelist_on")) ? "false" : "true");
        return;
    case P_ADD_WL: ask_player("allow", "Add to the whitelist"); return;
    case P_ADD_OP: ask_player("op", "Make an operator"); return;
    case S_MODE:
        prop("gamemode", v, sizeof v, "survival");
        set_prop("gamemode", modes[(index_of(modes, 4, v, 0) + d + 4) % 4]);
        return;
    case S_DIFF:
        prop("difficulty", v, sizeof v, "easy");
        set_prop("difficulty", diffs[(index_of(diffs, 4, v, 1) + d + 4) % 4]);
        return;
    case S_MAX: {
        static const int steps[] = {2, 4, 5, 8, 10, 12, 16, 20, 25, 30, 40, 50, 75, 100};
        prop("max-players", v, sizeof v, "20");
        snprintf(v, sizeof v, "%d", step_of(atoi(v), steps, 14, dir));
        set_prop("max-players", v);
        return;
    }
    case S_MOTD:
        prop("motd", v, sizeof v, "");
        if (ask_text("Message in the server list", "What players see under the name", v, 59)) set_prop("motd", v);
        return;
    case S_ONLINE:
        prop("online-mode", v, sizeof v, "true");
        set_prop("online-mode", strcmp(v, "false") ? "false" : "true");
        return;
    case S_PORT:
        snprintf(v, sizeof v, "%d", S->port);
        if (ask_text("Port", "1024 to 65535; 25565 is Minecraft's usual one", v, 5)) set_prop("server-port", v);
        return;
    case S_RAM:
        snprintf(json, sizeof json, "{\"ram_mb\":%d}", step_of(S->ram, ram_steps, NRAM, dir));
        patch(json);
        return;
    case S_AUTOSTART: patch(S->autostart ? "{\"autostart\":false}" : "{\"autostart\":true}"); return;
    case S_RESTART: patch(S->auto_restart ? "{\"auto_restart\":false}" : "{\"auto_restart\":true}"); return;
    case S_RENAME: {
        char name[48], en[100];
        snprintf(name, sizeof name, "%s", S->name);
        if (!ask_text("Rename", "Up to 40 letters", name, 40) || !name[0]) return;
        mn_escape(name, en, sizeof en);
        snprintf(json, sizeof json, "{\"name\":\"%s\"}", en);
        patch(json);
        return;
    }
    case S_DELETE:
        if (!stopped()) { message("Stop it first", "A running server can't be deleted."); return; }
        if (ui_confirm("Delete this server?", "Its world and backups are deleted too. This can't be undone.", "Delete", "Keep it")) {
            call("DELETE", sp(""), NULL, "It couldn't be deleted");
            cur_id[0] = 0;
            refresh(1);
        }
        return;
    case V_TYPE:
        i = (index_of(flavor_ids, 6, want_flavor, 0) + d + 6) % 6;
        snprintf(want_flavor, sizeof want_flavor, "%s", flavor_ids[i]);
        snprintf(want_version, sizeof want_version, "latest");
        want_build[0] = 0;
        return;
    case V_VERSION: if (pick_version(want_flavor, want_version, sizeof want_version)) want_build[0] = 0; return;
    case V_BUILD: choose_build(); return;
    case V_CHANGE:
        if (!stopped()) { message("Stop it first", "The server software can't change while it runs."); return; }
        snprintf(json, sizeof json, "{\"flavor\":\"%s\",\"version\":\"%s\",\"build\":\"%s\"}", want_flavor, want_version, want_build);
        if (ui_confirm("Change the server?", "The world is backed up first. Going back to an older Minecraft can damage a world, so keep that backup.", "Change it", "Cancel"))
            call("POST", sp("/version"), json, "It couldn't be changed");
        return;
    case V_REPAIR:
        if (!stopped()) { message("Stop it first", "The server software can't change while it runs."); return; }
        call("POST", sp("/repair"), NULL, "It couldn't be downloaded");
        return;
    case W_BACKUP:
        if (call("POST", sp("/backups"), NULL, "The backup didn't work") && prof.signed_in && social_earn("hostmc-backup")) social_flush();
        return;
    case W_RESET:
        if (!stopped()) { message("Stop it first", "The world can't change while the server runs."); return; }
        if (ui_confirm("Make a new world?", "The world now is backed up first. The next start makes the new one.", "New world", "Cancel"))
            call("POST", sp("/world/reset"), "{}", "It didn't work");
        return;
    case N_WAY:
        i = (index_of(ways, 3, S->way.kind, 0) + d + 3) % 3;
        snprintf(json, sizeof json, "{\"kind\":\"%s\"}", ways[i]);
        call("POST", sp("/way"), json, i == 2 ? "The supporter relay isn't ready" : "That didn't work");
        return;
    case N_CHECK: call("POST", sp("/recheck"), NULL, "The check didn't start"); return;
    case N_FORGET:
        if (ui_confirm("Forget playit.gg?", "This PC is disconnected from your playit.gg account. You can connect it again.", "Forget", "Cancel"))
            call("POST", "/api/playit/forget", NULL, "It couldn't be forgotten");
        return;
    case N_RELAY_NAME: relay_name(); return;
    case N_RELAY_STATUS: call("POST", "/api/relay/status", "{}", "XenithOS Cloud didn't answer"); return;
    case N_BEDROCK:
        call("POST", sp("/bedrock"), S->bedrock_on ? "{\"on\":false}" : "{\"on\":true}", "Bedrock players couldn't be turned on");
        return;
    case N_GEYSER: call("POST", sp("/bedrock"), "{\"update\":true}", "Geyser couldn't be updated"); return;
    }
    if (id >= ID_ADDON && id < ID_PLAYER) { addon_menu(id - ID_ADDON); return; }
    if (id >= ID_PLAYER && id < ID_WL && id - ID_PLAYER < S->nplayers) { player_menu(S->players[id - ID_PLAYER]); return; }
    if (id >= ID_WL && id < ID_OP && id - ID_WL < nwl) { player_act("disallow", wl[id - ID_WL]); return; }
    if (id >= ID_OP && id < ID_BACKUP && id - ID_OP < nop) { player_act("deop", ops[id - ID_OP]); return; }
    if (id >= ID_BACKUP) {
        static const char *const opts[] = {"Put it back", "Delete it", "Cancel"};
        const char *b = js_first(js_key(backups_json, "backups"));
        char name[64] = "", path[200], enc[140];
        int k, c;
        for (k = 0; b && k < id - ID_BACKUP; k++) b = js_next(b);
        if (!b) return;
        str_of(b, "name", name, sizeof name);
        mn_urlenc(name, enc, sizeof enc);
        c = ui_dialog("This backup", "Putting it back backs up the world as it is now first.", opts, 3);
        if (c == 0) {
            if (!stopped()) { message("Stop it first", "The world can't change while the server runs."); return; }
            snprintf(path, sizeof path, "%s%s/restore", sp("/backups/"), enc);
            call("POST", path, NULL, "It couldn't be put back");
        } else if (c == 1 && ui_confirm("Delete this backup?", "It can't be brought back.", "Delete", "Keep it")) {
            snprintf(path, sizeof path, "%s%s", sp("/backups/"), enc);
            call("DELETE", path, NULL, "It couldn't be deleted");
        }
    }
}

/* ------------------------------------------------------------------ the console */

#define CON_KEEP 400
static struct { char text[240]; char kind[8]; } con[CON_KEEP];
static int ncon;
static long long con_seq;
static char con_for[40];
static uint64_t con_at;

static void console_poll(void)
{
    struct mn_resp r;
    char path[200];
    const char *l;
    if (!nsrv) return;
    if (strcmp(con_for, cur_id)) { ncon = 0; con_seq = 0; console_back = 0; snprintf(con_for, sizeof con_for, "%s", cur_id); }
    if (ui_ms() - con_at < 500) return;
    con_at = ui_ms();
    snprintf(path, sizeof path, "%s?since=%lld", sp("/console"), con_seq);
    if (mn_call("GET", path, NULL, &r) != 200) { mn_free(&r); return; }
    for (l = js_first(js_key(r.body, "lines")); l; l = js_next(l)) {
        if (ncon == CON_KEEP) { memmove(con, con + 1, sizeof con[0] * (CON_KEEP - 1)); ncon--; }
        str_of(l, "text", con[ncon].text, sizeof con[0].text);
        str_of(l, "kind", con[ncon].kind, sizeof con[0].kind);
        con_seq = js_num(js_key(l, "seq"));
        ncon++;
    }
    mn_free(&r);
}

static uint32_t line_color(const char *kind)
{
    if (!strcmp(kind, "warn")) return C_WARN;
    if (!strcmp(kind, "error")) return C_BAD;
    if (!strcmp(kind, "join")) return C_OK;
    if (!strcmp(kind, "chat") || !strcmp(kind, "sent")) return C_TEXT;
    if (!strcmp(kind, "nest")) return RGB(150, 186, 240);
    return RGB(178, 185, 196);
}

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

#define CMD_H   50
#define CMD_GAP 12
static char cmdline[220];
static int cmd_say, cmd_typing;
static struct kb_field cmdfield;

static void console_typing(int on)
{
    if (!cmdfield.buf) kb_field_init(&cmdfield, cmdline, 200, KB_LINE);
    if (on == cmd_typing) return;
    cmd_typing = on;
    plat_text_input(on);
    if (on) kb_field_init(&cmdfield, cmdline, 200, KB_LINE);
}

static void cmdbar_parts(int x, int w, int *field_x, int *field_w, int *send_x, int *quick_x)
{
    *field_x = x + 142;
    *field_w = w - 456;
    *send_x = *field_x + *field_w + 10;
    *quick_x = *send_x + 114;
}

static void draw_cmdbar(int x, int y, int w)
{
    int fx, fw, sx, qx, over;
    const char *placeholder = stopped() ? "Start the server to send it commands" : cmd_say ? "A message for everyone on the server" : "A command, like time set day";
    cmdbar_parts(x, w, &fx, &fw, &sx, &qx);
    over = ui_mouse_in(x, y, 132, CMD_H);
    gfx_fill_rrect((float)x, (float)y, 132, CMD_H, 10, over ? C_SEL : C_SURFACE2, 255);
    gfx_stroke_rrect((float)x, (float)y, 132, CMD_H, 10, 1, C_LINE, 255);
    gfx_str_c(FONT_BODY_SB, x + 66, y + (CMD_H - gfx_line_h(FONT_BODY_SB)) / 2, C_TEXT, cmd_say ? "Say" : "Command");
    kb_field_draw(&cmdfield, fx, y, fw, CMD_H, FONT_BODY, placeholder, cmd_typing);
    over = ui_mouse_in(sx, y, 104, CMD_H);
    gfx_fill_rrect((float)sx, (float)y, 104, CMD_H, 10, over ? 0xFFFFFF : C_TEXT, 255);
    gfx_str_c(FONT_BODY_SB, sx + 52, y + (CMD_H - gfx_line_h(FONT_BODY_SB)) / 2, C_INK, "Send");
    over = ui_mouse_in(qx, y, 190, CMD_H);
    gfx_fill_rrect((float)qx, (float)y, 190, CMD_H, 10, over ? C_SEL : C_SURFACE2, 255);
    gfx_stroke_rrect((float)qx, (float)y, 190, CMD_H, 10, 1, C_LINE, 255);
    gfx_str_c(FONT_BODY_SB, qx + 95, y + (CMD_H - gfx_line_h(FONT_BODY_SB)) / 2, C_TEXT, "Quick commands");
}

static void draw_console(int px, int py, int pw, int ph)
{
    int x, w, lh, y, i, clip[4], skip = console_back;
    if (ui_desktop) {
        draw_cmdbar(px, py + ph - CMD_H, pw);
        ph -= CMD_H + CMD_GAP;
    }
    x = px + 24;
    w = pw - 48 - 86;
    lh = gfx_line_h(FONT_SMALL) + 3;
    y = py + ph - 18;
    ui_panel(px, py, pw, ph);
    gfx_get_clip(clip);
    gfx_clip_intersect(px + 1, py + 10, pw - 2, ph - 20);
    if (!ncon) gfx_str(FONT_BODY, x, py + 24, C_DIM, nsrv ? "The server's messages show up here." : "Make a server first, on the Server page.");
    for (i = ncon - 1; i >= 0 && y > py; i--) {
        char stamp[12];
        const char *t = console_text(con[i].text, stamp);
        int lines;
        if (skip > 0) { skip--; continue; }
        lines = wrap(FONT_SMALL, x + 86, 0, w, 3, 0, t, 6, 0);
        y -= lines * lh;
        if (stamp[0]) gfx_str(FONT_SMALL, x, y, C_DIM, stamp);
        wrap(FONT_SMALL, stamp[0] ? x + 86 : x, y, stamp[0] ? w : w + 86, 3, line_color(con[i].kind), t, 6, 1);
    }
    gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
    if (console_back > 0) {
        char more[48];
        snprintf(more, sizeof more, "%d newer lines below", console_back);
        gfx_fill_rrect((float)(px + pw - 220), (float)(py + ph - 44), 200, 30, 15, C_SURFACE2, 255);
        gfx_str_c(FONT_SMALL, px + pw - 120, py + ph - 40, C_SUB, more);
    }
}

static void scroll_console(int lines)
{
    console_back += lines;
    if (console_back > ncon - 1) console_back = ncon > 0 ? ncon - 1 : 0;
    if (console_back < 0) console_back = 0;
}

static int send_command(const char *cmd)
{
    char json[600], e[500], why[300];
    mn_escape(cmd, e, sizeof e);
    snprintf(json, sizeof json, "{\"command\":\"%s\"}", e);
    if (mn_do("POST", sp("/command"), json, why, sizeof why)) { con_at = 0; console_back = 0; return 1; }
    console_typing(0);
    message("The command didn't reach the server", why);
    return 0;
}

static void quick_commands(void)
{
    static const char *const opts[] = {"Make it day", "Clear the weather", "Save the world", "Who's on", "Cancel"};
    static const char *const cmds[] = {"time set day", "weather clear", "save-all", "list"};
    int c;
    sfx(SFX_SELECT);
    console_typing(0);
    if (!running()) { message("The server isn't running", "Start it on the Server page first."); return; }
    c = ui_dialog("Quick commands", NULL, opts, 5);
    if (c >= 0 && c < 4) send_command(cmds[c]);
}

static void send_line(void)
{
    char cmd[240];
    if (!cmdline[0]) return;
    if (stopped()) { console_typing(0); message("The server isn't running", "Start it on the Server page first."); return; }
    snprintf(cmd, sizeof cmd, "%s%s", cmd_say ? "say " : "", cmdline[0] == '/' ? cmdline + 1 : cmdline);
    if (!send_command(cmd)) return;
    cmdline[0] = 0;
    cmdfield.caret = cmdfield.first = 0;
}

static void console_input(int px, int py, int pw, int ph)
{
    char buf[220] = "", cmd[240];
    int d = ui_nav_y();
    if (d) scroll_console(-3 * d);
    if (ui_desktop) {
        int by = py + ph - CMD_H, fx, fw, sx, qx;
        cmdbar_parts(px, pw, &fx, &fw, &sx, &qx);
        if (mouse.wheel && ui_mouse_in(px, py, pw, ph - CMD_H - CMD_GAP)) scroll_console(3 * mouse.wheel);
        if (ui_key(PLAT_KEY_PAGEUP)) scroll_console(15);
        if (ui_key(PLAT_KEY_PAGEDOWN)) scroll_console(-15);
        if (kb_field_input(&cmdfield)) send_line();
        else if (ui_clicked(sx, by, 104, CMD_H)) { sfx(SFX_SELECT); send_line(); }
        kb_field_click(&cmdfield, fx, by, fw, CMD_H, FONT_BODY);
        if (ui_key(PLAT_KEY_TAB) || ui_clicked(px, by, 132, CMD_H)) { cmd_say = !cmd_say; sfx(SFX_MOVE); }
        if (ui_key(PLAT_KEY_F1 + 1) || ui_clicked(qx, by, 190, CMD_H)) quick_commands();
        return;
    }
    if (PRESSED(lb)) scroll_console(15);
    if (PRESSED(rb)) scroll_console(-15);
    if (PRESSED(a) || PRESSED(y)) {
        int say = PRESSED(y);
        sfx(SFX_SELECT);
        if (stopped()) { message("The server isn't running", "Start it on the Server page first."); return; }
        if (!ask_text(say ? "Say something" : "Send a command", say ? "Everyone on the server sees it" : "Like time set day, without the slash", buf, 200) || !buf[0]) return;
        snprintf(cmd, sizeof cmd, "%s%s", say ? "say " : "", buf[0] == '/' ? buf + 1 : buf);
        send_command(cmd);
    }
    if (PRESSED(x)) quick_commands();
}

/* ------------------------------------------------------------------ drawing */

static int row_height(const struct row *r, int w)
{
    switch (r->kind) {
    case R_STATUS: return 110;
    case R_BUTTON: return 58;
    case R_ITEM: return 50;
    case R_HEAD: return 46;
    case R_INFO: return 35;
    case R_TEXT: return wrap(FONT_BODY, 0, 0, w, 4, 0, r->value, 8, 0) * (gfx_line_h(FONT_BODY) + 4) + 8;
    case R_NAMES: {
        int i, x = 0, lines = 1;
        for (i = 0; i < S->nplayers; i++) {
            int cw = gfx_str_w(FONT_BODY_SB, S->players[i]) + 32;
            if (x + cw > w && x > 0) { lines++; x = 0; }
            x += cw + 10;
        }
        return lines * 46 + 4;
    }
    case R_QR: return 250;
    default: return 14;
    }
}

static void add_hit(int id, int x, int y, int w, int h, int top, int bottom, int pill_x, int pill_w)
{
    struct hit *t;
    if (y < top) { h -= top - y; y = top; }
    if (y + h > bottom) h = bottom - y;
    if (h <= 0 || nhits >= MAX_ROWS) return;
    t = &hits[nhits++];
    t->id = id;
    t->x = x;
    t->y = y;
    t->w = w;
    t->h = h;
    t->pill_x = pill_x;
    t->pill_w = pill_w;
}

static void draw_status(int x, int y, int w)
{
    const char *word, *line2;
    char a[64], b[200], inst[120];
    uint32_t dot;
    if (!strcmp(S->state, "running")) { word = "Running"; dot = C_OK; }
    else if (!strcmp(S->state, "starting")) { word = "Starting"; dot = C_WARN; }
    else if (!strcmp(S->state, "stopping")) { word = "Stopping"; dot = C_WARN; }
    else { word = S->busy[0] ? "Busy" : S->installed ? "Stopped" : "Not set up yet"; dot = S->why[0] ? C_BAD : C_DIM; }
    gfx_circle(x + 12.0f, y + 26.0f, 9, dot, 255);
    if (dot == C_WARN) gfx_ring(x + 12.0f, y + 26.0f, 15, 2, dot, (int)(128 + 100 * ((ui_ms() / 500) % 2)));
    gfx_str(FONT_HEAD, x + 38, y - 2, C_TEXT, word);
    snprintf(inst, sizeof inst, "%s %s", S->flavor_name, S->mc);
    if (S->installed) gfx_str_r(FONT_BODY_SB, x + w, y + 14, C_SUB, inst);
    if (running()) {
        long long m = S->uptime / 60;
        if (m >= 60) snprintf(a, sizeof a, "%lld h %lld min", m / 60, m % 60);
        else snprintf(a, sizeof a, "%lld min", m);
        snprintf(b, sizeof b, "Up for %s, with up to %.1f GB of memory.", a, S->ram / 1024.0);
        line2 = b;
    } else if (!strcmp(S->state, "starting")) line2 = "Getting the world ready. The first start takes a minute or two.";
    else if (!strcmp(S->state, "stopping")) line2 = "Saving the world, then it stops.";
    else line2 = S->installed ? "Ready to start. It keeps running after you leave MineNest." : "MineNest is setting it up.";
    gfx_str_fit(FONT_BODY, x + 2, y + 60, C_SUB, line2, w);
}

static void draw_rows(int px, int py, int pw, int ph)
{
    int x = px + 28, w = pw - 56, y, i, clip[4], sel_i = -1, sel_top = 0, sel_h = 0, total = 0, btn_w = 280;
    int ys[MAX_ROWS], hs_[MAX_ROWS];
    ui_panel(px, py, pw, ph);
    if (footer[0]) {
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
    if (in_page && sel_i >= 0 && (!ui_desktop || reveal)) {
        if (sel_top - scroll_y < 0) scroll_y = sel_top - (sel_i > 0 && !selectable(&rows[sel_i - 1]) ? hs_[sel_i - 1] : 0);
        if (sel_top + sel_h - scroll_y > ph - 44) scroll_y = sel_top + sel_h - (ph - 44);
    }
    reveal = 0;
    if (scroll_y > total - (ph - 44)) scroll_y = total - (ph - 44);
    if (scroll_y < 0) scroll_y = 0;
    gfx_get_clip(clip);
    gfx_clip_intersect(px + 1, py + 1, pw - 2, ph - 2);
    y = py + 22 - scroll_y;
    nhits = 0;
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
        case R_STATUS: draw_status(x, ry, w); break;
        case R_BUTTON: {
            uint32_t fill = on ? C_TEXT : r->primary ? C_ACCENT : C_SURFACE2;
            gfx_fill_rrect((float)x, (float)ry, (float)btn_w, 46, 10, fill, 255);
            gfx_str_vc(FONT_BODY_SB, x + 22, ry + 23, on ? C_INK : C_TEXT, r->label);
            add_hit(r->id, x, ry, btn_w, 46, py + 1, py + ph - 1, 0, 0);
            break;
        }
        case R_ITEM: {
            int cy = ry + (h - 6) / 2, vw = 0;
            gfx_str_fit(FONT_BODY_SB, x, cy - gfx_line_h(FONT_BODY_SB) / 2, C_TEXT, r->label, w / 2);
            if (r->value[0]) {
                char shown[260];
                int maxw = w - gfx_str_w(FONT_BODY_SB, r->label) - 60;
                if (maxw < w / 2 - 20) maxw = w / 2 - 20;
                snprintf(shown, sizeof shown, "%s%s%s", on && r->cycle ? "<  " : "", r->value, on && r->cycle ? "  >" : "");
                vw = gfx_str_w(FONT_SMALL, shown) + 28;
                if (vw > maxw) vw = maxw;
                gfx_fill_rrect((float)(x + w - vw), cy - 15.0f, (float)vw, 30, 15, on ? C_TEXT : C_SURFACE2, 255);
                gfx_str_fit(FONT_SMALL, x + w - vw + 14, cy - gfx_line_h(FONT_SMALL) / 2, on ? C_INK : C_TEXT, shown, vw - 28);
            }
            add_hit(r->id, x - 14, ry, w + 28, h - 6, py + 1, py + ph - 1, r->cycle && vw ? x + w - vw : 0, r->cycle ? vw : 0);
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
        case R_TEXT: wrap(FONT_BODY, x, ry + 2, w, 4, r->color ? r->color : C_TEXT, r->value, 8, 1); break;
        case R_NAMES: {
            int k, cx = x, cyy = ry + 2;
            for (k = 0; k < S->nplayers; k++) {
                int cw = gfx_str_w(FONT_BODY_SB, S->players[k]) + 32;
                if (cx + cw > x + w && cx > x) { cx = x; cyy += 46; }
                gfx_fill_rrect((float)cx, (float)cyy, (float)cw, 36, 18, C_SURFACE2, 255);
                gfx_circle(cx + 14.0f, cyy + 18.0f, 4, C_OK, 255);
                gfx_str_vc(FONT_BODY_SB, cx + 24, cyy + 18, C_TEXT, S->players[k]);
                cx += cw + 10;
            }
            break;
        }
        case R_QR: {
            int qs = 226, tx = x + qs + 32, tw = w - qs - 32, ty = ry + 4;
            qr_draw(r->value, x, ry, qs);
            ty += wrap(FONT_BODY, tx, ty, tw, 4, C_TEXT, r->label, 5, 1) * (gfx_line_h(FONT_BODY) + 4) + 12;
            gfx_str_fit(FONT_LABEL, tx, ty, C_TEXT, strstr(r->value, "://") ? strstr(r->value, "://") + 3 : r->value, tw);
            break;
        }
        }
    }
    gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
    if (total > ph - 30) {
        float vis = (float)(ph - 44) / total, at = (float)scroll_y / total;
        gfx_fill_rrect((float)(px + pw - 10), py + 12 + at * (ph - 24), 4, vis * (ph - 24), 2, C_LINE, 255);
    }
}

#define MENU_ROW 56

static void draw_menu(int x, int y, int w)
{
    int i, rowh = MENU_ROW;
    menu_hl = ui_ease(menu_hl, (float)page, 0.45f);
    if (!in_page) ui_select_box((float)x, y + menu_hl * rowh, (float)w, rowh - 8.0f, 10);
    else gfx_fill_rrect((float)x, y + page * rowh + 0.0f, (float)w, rowh - 8.0f, 10, C_SURFACE, 255);
    for (i = 0; i < P_COUNT; i++) {
        int cy = y + i * rowh + (rowh - 8) / 2, on = i == page;
        uint32_t bg = on ? (in_page ? C_SURFACE : C_SEL) : C_BG;
        const char *name = page_names[i];
        if (i == P_MODS && nsrv) name = !strcmp(S->addon_kind, "plugin") ? "Plugins" : !strcmp(S->addon_kind, "datapack") ? "Data packs" : "Mods";
        if (ui_desktop && !on && ui_mouse_in(x, y + i * rowh, w, rowh - 8)) {
            gfx_fill_rrect((float)x, (float)(y + i * rowh), (float)w, rowh - 8.0f, 10, C_SURFACE, 255);
            bg = C_SURFACE;
        }
        icon_draw(page_icons[i], x + 26.0f, (float)cy, 24, on ? C_TEXT : C_SUB, bg);
        gfx_str_vc(FONT_BODY_SB, x + 52, cy, on ? C_TEXT : C_SUB, name);
        if (i == P_SERVER && nsrv) {
            uint32_t c = running() ? C_OK : stopped() ? C_DIM : C_WARN;
            gfx_circle(x + w - 22.0f, (float)cy, 5, c, 255);
        } else if (i == P_INTERNET && nsrv && S->way.kind[0]) {
            gfx_circle(x + w - 22.0f, (float)cy, 5, S->way.up ? C_OK : stopped() ? C_DIM : !strcmp(S->way.state, "problem") ? C_BAD : C_WARN, 255);
        } else if (i == P_PLAYERS && nsrv && S->nplayers) {
            char n[8];
            snprintf(n, sizeof n, "%d", S->nplayers);
            gfx_str_r(FONT_SMALL, x + w - 14, cy - gfx_line_h(FONT_SMALL) / 2, C_SUB, n);
        }
    }
}

static const char *subtitle(void)
{
    static char sub[120];
    if (!nsrv) return "Minecraft servers on this PC";
    snprintf(sub, sizeof sub, "%s: %s", S->name, running() ? "running" : !strcmp(S->state, "starting") ? "starting"
                                                : !strcmp(S->state, "stopping") ? "stopping" : "stopped");
    return sub;
}

/* ------------------------------------------------------------------ friends and achievements */

/* What friends on XenithOS see: that MineNest is open and, while a server runs and people on the
 * internet can reach it, the address they type. HostMC's achievements carry on here. */
static void share_poll(void)
{
    static int start, online, join;
    const char *addr = nsrv && running() && S->pub[0] ? S->pub : "";
    int any = 0, i;
    social_tick("minenest", addr);
    if (!prof.signed_in) return;
    for (i = 0; i < nsrv; i++) {
        if (!start && !strcmp(srvs[i].state, "running")) { start = 1; any |= social_earn("hostmc-start"); }
        if (!online && !strcmp(srvs[i].state, "running") && srvs[i].pub[0]) { online = 1; any |= social_earn("hostmc-online"); }
        if (!join && srvs[i].nplayers > 0) { join = 1; any |= social_earn("hostmc-join"); }
    }
    if (any) social_flush();
}

/* The XenithOS profile that's signed in goes to MineNest, for the supporter relay. */
static void hand_over_profile(void)
{
    struct mn_resp r;
    char name[40] = "", json[200];
    if (!prof.signed_in || !prof.token[0]) return;
    if (mn_call("GET", "/api/account", NULL, &r) == 200) str_of(r.body, "account", name, sizeof name);
    mn_free(&r);
    if (name[0] && !strcasecmp(name, prof.name)) return;
    snprintf(json, sizeof json, "{\"token\":\"%s\"}", prof.token);
    mn_call("POST", "/api/account/login", json, &r);
    mn_free(&r);
}

/* ------------------------------------------------------------------ main */

static void leave(void)
{
    console_typing(0);
    ui_back_to_xenithos("minenest");
}

static void move_sel(int d)
{
    int i, curr = -1;
    for (i = 0; i < nrows; i++)
        if (rows[i].id == sel_id && selectable(&rows[i])) { curr = i; break; }
    for (i = curr + d; i >= 0 && i < nrows; i += d)
        if (selectable(&rows[i])) { sel_id = rows[i].id; sfx(SFX_MOVE); return; }
    if (d < 0) { if (scroll_y > 0) scroll_y -= 60; }
    else if (curr >= 0) scroll_y += 60;
}

static int first_selectable(void)
{
    int i;
    for (i = 0; i < nrows; i++)
        if (selectable(&rows[i])) return rows[i].id;
    return 0;
}

static void go_page(int p)
{
    page = p;
    scroll_y = 0;
    console_back = 0;
    load_page_data(1);
    if (page != P_CONSOLE) build();
    sel_id = first_selectable();
}

static int enter_page(void)
{
    if (page != P_CONSOLE && !first_selectable()) return 0;
    in_page = 1;
    sel_id = first_selectable();
    sel_y_hl = -1;
    reveal = 1;
    if (page == P_INTERNET && nsrv && !strcmp(S->way.kind, "relay")) {
        struct mn_resp r;
        mn_call("POST", "/api/relay/status", "{}", &r);       /* as it is now */
        mn_free(&r);
        load_page_data(1);
    }
    return 1;
}

static void desktop_mouse(int px, int py, int pw, int ph)
{
    int i;
    for (i = 0; i < P_COUNT; i++) {
        if (!ui_clicked(M, py + i * MENU_ROW, 230, MENU_ROW - 8)) continue;
        if (i != page || !in_page) {
            sfx(SFX_SELECT);
            if (i != page) go_page(i);
            in_page = 0;
            enter_page();
        }
        return;
    }
    if (page == P_CONSOLE) return;
    if (mouse.wheel && ui_mouse_in(px, py, pw, ph)) scroll_y -= mouse.wheel * 60;
    for (i = 0; i < nhits; i++) {
        const struct hit *t = &hits[i];
        int dir = 0;
        if (!ui_mouse_in(t->x, t->y, t->w, t->h)) continue;
        if ((mouse.dx || mouse.dy || mouse.clicks) && (sel_id != t->id || !in_page)) {
            if (sel_id != t->id) sfx(SFX_MOVE);
            sel_id = t->id;
            if (!in_page) { in_page = 1; sel_y_hl = -1; }
        }
        if (t->pill_w && mouse.x < t->pill_x + 36 && mouse.x >= t->pill_x) dir = -1;
        else if (t->pill_w && mouse.x >= t->pill_x + t->pill_w - 36 && mouse.x < t->pill_x + t->pill_w) dir = 1;
        if (mouse.clicks & 1) { sfx(SFX_SELECT); act(t->id, dir); }
        else if ((mouse.clicks & 2) && t->pill_w) { sfx(SFX_SELECT); act(t->id, -1); }
        return;
    }
}

int main(int argc, char **argv)
{
    int started = 0;
    handoff_args(argc, argv);
    startlog_mark("start");
    plat_init();
    gfx_init();
    ui_init();
    ui_keys_on = ui_desktop;
    ui_loading_frame("MineNest", "Loading");
    plat_storage_init();
    config_load();
    settings_load();
    profile_load();
    audio_init();
    audio_set_volume(st.volume);
    audio_set_ui_sounds(st.ui_sounds);
    startlog_mark("sounds");

    if (plat_kind() != PLAT_PC) {
        ui_message("MineNest needs a PC", "A Minecraft server runs on a PC or Raspberry Pi with XenithOS. The Xbox can't run one.");
        ui_back_to_xenithos("minenest");
        return 0;
    }
    if (mn_connect(1)) {
        hand_over_profile();
        check_update();
    }
    refresh(1);
    load_page_data(1);
    ui_fade_in();

    for (;;) {
        int px = M + 252, py = HEADER_H + 4, pw = SCR_W - M - px, ph = SCR_H - py - 66;
        ui_poll();
        refresh(0);
        load_page_data(0);
        if (page == P_CONSOLE) console_poll();
        share_poll();
        if (page != P_CONSOLE) build();
        console_typing(ui_desktop && in_page && page == P_CONSOLE);
        if (ui_desktop) desktop_mouse(px, py, pw, ph);

        if (!in_page) {
            int d = ui_nav_y();
            if (d && page + d >= 0 && page + d < P_COUNT) { sfx(SFX_MOVE); go_page(page + d); }
            if ((PRESSED(a) || PRESSED(right) || (cur.lx > STICK_TH && prev.lx <= STICK_TH)) && (page == P_CONSOLE || first_selectable())) {
                sfx(SFX_SELECT);
                enter_page();
            }
            if (PRESSED(b)) { sfx(SFX_BACK); leave(); return 0; }
        } else if (page == P_CONSOLE) {
            if (PRESSED(b) || (!ui_desktop && PRESSED(left))) { sfx(SFX_BACK); in_page = 0; }
            else if (nsrv) console_input(px, py, pw, ph);
        } else {
            int dy = ui_nav_y(), dx = ui_nav_x(), i;
            struct row *r = NULL;
            for (i = 0; i < nrows; i++)
                if (rows[i].id == sel_id && selectable(&rows[i])) { r = &rows[i]; break; }
            if (!r && first_selectable()) sel_id = first_selectable();
            if (dy) { move_sel(dy); reveal = 1; }
            if (PRESSED(b)) { sfx(SFX_BACK); in_page = 0; }
            else if (dx && r && r->cycle) act(r->id, dx);
            else if (dx < 0) { sfx(SFX_BACK); in_page = 0; }
            else if (PRESSED(a) && r) { sfx(SFX_SELECT); act(r->id, 0); }
        }

        ui_bg();
        ui_header("MineNest", subtitle());
        draw_menu(M, py, 230);
        if (page == P_CONSOLE) draw_console(px, py, pw, ph);
        else { build(); draw_rows(px, py, pw, ph); }
        if (ui_desktop) {
            if (!in_page) ui_hints("Click=Open a page;A=Open;B=Close MineNest");
            else if (page == P_CONSOLE) ui_hints("A=Send;Tab=Command or say;F2=Quick commands;Wheel=Older and newer;B=Back");
            else {
                const char *hint = "Click=Choose;Wheel=Scroll;A=Choose;B=Back";
                int i;
                for (i = 0; i < nrows; i++)
                    if (rows[i].id == sel_id && selectable(&rows[i]) && rows[i].cycle) hint = "Click=Next;Right click=Previous;Left=Less;Right=More;B=Back";
                ui_hints(hint);
            }
        } else if (!in_page) ui_hints("A=Open;B=Leave MineNest");
        else if (page == P_CONSOLE) ui_hints("A=Command;Y=Say;X=Quick commands;LB=Older;RB=Newer;B=Back");
        else {
            const char *hint = "A=Select;B=Back";
            int i;
            for (i = 0; i < nrows; i++)
                if (rows[i].id == sel_id && selectable(&rows[i]) && rows[i].cycle) hint = "A=Change;Left=Less;Right=More;B=Back";
            ui_hints(hint);
        }
        ui_end_frame();
        if (!started) { started = 1; startlog_ready("MINENEST"); }
    }
}
