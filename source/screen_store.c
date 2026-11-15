/* STORE: browse and install from XenithOS Cloud.
 *
 * Xbox: apps, game data and XenithOS updates, all files from XenithOS Cloud.
 * PCs and Raspberry Pis, in sections (LB / RB):
 *   Apps      apps from Flathub (XenithOS Cloud picks which: /v1/store/flathub), installed
 *             with Flatpak behind XenithOS's own progress screen, started from the home screen;
 *   Steam     the Steam app and games on Steam (/v1/store/steam). The games are installed and
 *             played through Steam with your Steam account, which the Store says before it
 *             opens Steam. PCs with x86-64 processors only: Steam doesn't run on ARM;
 *   XenithOS  XenithOS updates and game data, as on the Xbox.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "apps.h"
#include "audio.h"
#include "config.h"
#include "gfx.h"
#include "icons.h"
#include "img.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "sha256.h"
#include "theme.h"
#include "ui.h"

#define MAX_ITEMS 64
enum { ST_NEW, ST_INSTALLED, ST_UPDATE };
enum { TAB_APPS, TAB_STEAM, TAB_XENITH, NTABS };
static const char *const tab_name[NTABS] = {"Apps", "Steam", "XenithOS"};

#define MAX_NEEDS 4
#define STEAM_REF "com.valvesoftware.Steam"

struct item {
    char id[33], kind[8], name[25], version[13], sha[65], file[48], desc[100];
    char ref[64], arches[40], args[48];     /* apps from Flathub */
    int mb, shared_mb;                      /* their download, and the shared parts they need (MB) */
    int appid, free_game;                   /* games on Steam */
    int size, state;
    int own;                     /* the state of its own file; state also counts its game data */
    int needs[MAX_NEEDS], nneeds;   /* game data it comes with (indexes into items) */
    int needed;                  /* comes with another item, so it isn't listed by itself */
    int unavailable;             /* not made for this machine's processor */
    int tab;
    char dest[96];
    struct img icon;
    unsigned char *icon_raw;
    int icon_len;
};

static struct item items[MAX_ITEMS];
static int nitems;
static int view[MAX_ITEMS], nview;      /* the items listed, in order */
static int tab, has_tabs;

static void size_text(int bytes, char *out, int n);

static int safe_name(const char *s)
{
    if (!*s || *s == '.') return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '.' && *s != '_' && *s != '-') return 0;
    return 1;
}

static int is_flathub(const struct item *it) { return !strcmp(it->kind, "flathub"); }
static int is_steam_game(const struct item *it) { return !strcmp(it->kind, "steam"); }
static int is_steam_app(const struct item *it) { return is_flathub(it) && !strcmp(it->ref, STEAM_REF); }

/* Where an item goes on the USB stick. The server never picks the path. */
static int item_dest(struct item *it)
{
    char path[sizeof it->dest];
    if (!safe_name(it->id)) return 0;
    if (!strcmp(it->kind, "app")) {
        snprintf(path, sizeof path, "uda:/apps/%s.elf32", it->id);
    } else if (!strcmp(it->kind, "data")) {
        if (!safe_name(it->file)) return 0;
        snprintf(path, sizeof path, "uda:/data/%s", it->file);
    } else if (!strcmp(it->kind, "system")) {
        snprintf(path, sizeof path, "%s", XENITH_SHELL_PATH);
    } else if (!strcmp(it->kind, "pc") && plat_kind() == PLAT_PC) {
        snprintf(path, sizeof path, "uda:/xenith/xenithos-update.tar.gz");   /* the source; built here */
    } else if (is_flathub(it) && plat_kind() == PLAT_PC && strncmp(it->id, "steam-", 6)) {
        snprintf(path, sizeof path, "uda:/apps/%s.app", it->id);            /* how the home screen finds it */
    } else if (is_steam_game(it) && plat_kind() == PLAT_PC && it->appid > 0) {
        snprintf(path, sizeof path, "uda:/apps/steam-%d.app", it->appid);   /* made by plat_sync_apps() */
    } else {
        return 0;
    }
    memcpy(it->dest, path, sizeof path);
    return 1;
}

/* A new version of XenithOS itself: xenon.elf on the Xbox, the source to build on a PC. */
static int is_update(const struct item *it)
{
    return !strcmp(it->kind, "system") || !strcmp(it->kind, "pc");
}

static int file_size(const char *path)
{
    struct stat s;
    return stat(path, &s) == 0 ? (int)s.st_size : -1;
}

static int own_state(const struct item *it)
{
    int have;
    if (is_flathub(it)) return plat_flatpak_installed(it->ref) ? ST_INSTALLED : ST_NEW;
    if (is_steam_game(it)) return file_size(it->dest) >= 0 ? ST_INSTALLED : ST_NEW;
    have = file_size(it->dest);
    if (is_update(it))
        return !strcmp(it->version, XENITH_VERSION) ? ST_INSTALLED : ST_UPDATE;
    return have < 0 ? ST_NEW : have == it->size ? ST_INSTALLED : ST_UPDATE;
}

/* An item that comes with game data only counts as installed once that's there too. */
static void update_states(void)
{
    int i, n;
    for (i = 0; i < nitems; i++) items[i].own = items[i].state = own_state(&items[i]);
    for (i = 0; i < nitems; i++)
        for (n = 0; n < items[i].nneeds; n++)
            if (items[items[i].needs[n]].own != ST_INSTALLED && items[i].state == ST_INSTALLED)
                items[i].state = ST_UPDATE;
}

static int total_size(const struct item *it)
{
    int n, bytes = it->size;
    for (n = 0; n < it->nneeds; n++) bytes += items[it->needs[n]].size;
    return bytes;
}

/* "Freedoom: Phase 1 and Freedoom: Phase 2" */
static void needs_text(const struct item *it, char *out, int size)
{
    int n, len = 0;
    out[0] = 0;
    for (n = 0; n < it->nneeds && len < size; n++)
        len += snprintf(out + len, size - len, "%s%s", n == 0 ? "" : n == it->nneeds - 1 ? " and " : ", ",
                        items[it->needs[n]].name);
}

/* "doom|freedoom1,freedoom2": which game data an item comes with. */
static void link_needs(const char *id, char *list)
{
    int a, d;
    char *tok;
    for (a = 0; a < nitems && strcmp(items[a].id, id); a++)
        ;
    if (a == nitems) return;
    for (tok = strtok(list, ","); tok && items[a].nneeds < MAX_NEEDS; tok = strtok(NULL, ",")) {
        for (d = 0; d < nitems; d++)
            if (d != a && !strcmp(items[d].id, tok) && !strcmp(items[d].kind, "data")) {
                items[a].needs[items[a].nneeds++] = d;
                items[d].needed = 1;
            }
    }
}

static void free_items(void)
{
    int i;
    for (i = 0; i < nitems; i++) {
        img_free(&items[i].icon);
        free(items[i].icon_raw);
    }
    nitems = 0;
}

/* "x86_64,aarch64" names this machine's processor. */
static int arch_fits(const char *arches)
{
    char list[48], want[40];
    snprintf(list, sizeof list, ",%s,", arches);
    snprintf(want, sizeof want, ",%s,", plat_arch());
    return plat_arch()[0] && strstr(list, want) != NULL;
}

/* Which section an item is in (PCs). */
static int item_tab(const struct item *it)
{
    if (is_steam_app(it) || is_steam_game(it)) return TAB_STEAM;
    if (is_flathub(it)) return TAB_APPS;
    return TAB_XENITH;
}

static int tab_count(int t)
{
    int i, n = 0;
    for (i = 0; i < nitems; i++) if (!items[i].needed && items[i].tab == t) n++;
    return n;
}

/* The items in the current section, the Steam app first in its own. */
static void build_view(void)
{
    int i, pass;
    nview = 0;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < nitems; i++) {
            const struct item *it = &items[i];
            if (it->needed || (has_tabs && it->tab != tab) || is_steam_app(it) != (pass == 0)) continue;
            view[nview++] = i;
        }
}

/* Apps from Flathub: id|name|flathub id|processors|download MB|shared MB|arguments|description */
static void load_flathub(void)
{
    struct http_resp r;
    char *line;
    if (cloud_get("/v1/store/flathub", 64 * 1024, &r) == 200) {
        for (line = r.body; line && *line && nitems < MAX_ITEMS; ) {
            char *next = strchr(line, '\n'), *f[8];
            if (next) *next++ = 0;
            if (split_fields(line, f, 8) == 8) {
                struct item *it = &items[nitems];
                memset(it, 0, sizeof *it);
                snprintf(it->id, sizeof it->id, "%s", f[0]);
                snprintf(it->kind, sizeof it->kind, "flathub");
                snprintf(it->name, sizeof it->name, "%s", f[1]);
                snprintf(it->ref, sizeof it->ref, "%s", f[2]);
                snprintf(it->arches, sizeof it->arches, "%s", f[3]);
                it->mb = atoi(f[4]);
                it->shared_mb = atoi(f[5]);
                snprintf(it->args, sizeof it->args, "%s", f[6]);
                snprintf(it->desc, sizeof it->desc, "%s", f[7]);
                it->unavailable = !arch_fits(it->arches);
                /* Steam is only worth listing where it runs. */
                if (it->ref[0] && item_dest(it) && !(is_steam_app(it) && it->unavailable)) nitems++;
            }
            line = next;
        }
    }
    http_free(&r);
}

/* Games on Steam: id|name|Steam app id|1 if free to play|description */
static void load_steam(void)
{
    struct http_resp r;
    char *line;
    if (cloud_get("/v1/store/steam", 64 * 1024, &r) == 200) {
        for (line = r.body; line && *line && nitems < MAX_ITEMS; ) {
            char *next = strchr(line, '\n'), *f[5];
            if (next) *next++ = 0;
            if (split_fields(line, f, 5) == 5) {
                struct item *it = &items[nitems];
                memset(it, 0, sizeof *it);
                snprintf(it->id, sizeof it->id, "%s", f[0]);
                snprintf(it->kind, sizeof it->kind, "steam");
                snprintf(it->name, sizeof it->name, "%s", f[1]);
                it->appid = atoi(f[2]);
                it->free_game = atoi(f[3]) == 1;
                snprintf(it->desc, sizeof it->desc, "%s", f[4]);
                if (item_dest(it)) nitems++;
            }
            line = next;
        }
    }
    http_free(&r);
}

static int load_store(void)
{
    struct http_resp r;
    char *line, path[96];
    int code, i;

    free_items();
    ui_busy_begin("Opening the Store", NULL);
    code = cloud_get("/v1/store", 64 * 1024, &r);
    if (code != 200) {
        ui_busy_end();
        sfx(SFX_ERROR);
        ui_message("The Store didn't load", cloud_error_text(&r, code));
        http_free(&r);
        return 0;
    }
    for (line = r.body; line && *line && nitems < MAX_ITEMS; ) {
        char *next = strchr(line, '\n'), *f[8];
        if (next) *next++ = 0;
        if (split_fields(line, f, 8) == 8) {
            struct item *it = &items[nitems];
            memset(it, 0, sizeof *it);
            snprintf(it->id, sizeof it->id, "%s", f[0]);
            snprintf(it->kind, sizeof it->kind, "%s", f[1]);
            snprintf(it->name, sizeof it->name, "%s", f[2]);
            snprintf(it->version, sizeof it->version, "%s", f[3]);
            it->size = atoi(f[4]);
            snprintf(it->sha, sizeof it->sha, "%s", f[5]);
            snprintf(it->file, sizeof it->file, "%s", f[6]);
            snprintf(it->desc, sizeof it->desc, "%s", f[7]);
            /* The Store's apps and "system" updates are Xbox programs. On a PC or Pi the apps
             * come with XenithOS itself, so it offers game data and the "pc" update there. */
            int fits = plat_kind() == PLAT_XBOX ? strcmp(it->kind, "pc") != 0
                                                : !strcmp(it->kind, "data") || !strcmp(it->kind, "pc");
            if (fits && it->size > 0 && strlen(it->sha) == 64 && item_dest(it)) nitems++;
        }
        line = next;
    }
    http_free(&r);

    /* Game data that comes with an app (Freedoom with DOOM) is installed along with it, so
     * it isn't listed by itself. On a PC the apps aren't listed, and the data stays. */
    if (cloud_get("/v1/store/needs", 8 * 1024, &r) == 200) {
        for (line = r.body; line && *line; ) {
            char *next = strchr(line, '\n'), *f[2];
            if (next) *next++ = 0;
            if (split_fields(line, f, 2) == 2) link_needs(f[0], f[1]);
            line = next;
        }
    }
    http_free(&r);

    /* PCs: apps from Flathub, and games on Steam where Steam runs. */
    has_tabs = plat_kind() == PLAT_PC;
    if (has_tabs) {
        load_flathub();
        if (!strcmp(plat_arch(), "x86_64")) {
            plat_sync_apps();                         /* which Steam games are installed */
            load_steam();
        }
    }
    for (i = 0; i < nitems; i++) items[i].tab = has_tabs ? item_tab(&items[i]) : 0;
    update_states();

    for (i = 0; i < nitems; i++) {
        struct http_resp ir;
        snprintf(path, sizeof path, "/v1/store/icon/%s.ximg", items[i].id);
        if (cloud_get(path, 512 * 1024, &ir) == 200 && img_load_mem(&items[i].icon, ir.body, ir.body_len)) {
            items[i].icon_raw = malloc(ir.body_len);
            if (items[i].icon_raw) {
                memcpy(items[i].icon_raw, ir.body, ir.body_len);
                items[i].icon_len = ir.body_len;
            }
        }
        http_free(&ir);
    }
    ui_busy_end();

    /* Start in Apps, or in XenithOS when there's an update for it. */
    tab = TAB_APPS;
    for (i = 0; i < nitems; i++)
        if (is_update(&items[i]) && items[i].state == ST_UPDATE) tab = TAB_XENITH;
    if (has_tabs && !tab_count(tab)) for (tab = 0; tab < NTABS - 1 && !tab_count(tab); tab++) { }
    build_view();
    return 1;
}

/* ---------------------------------------------------------------- installing */

static const char *install_name;
static uint64_t last_draw;
static void on_progress(int got, int total, void *ctx)
{
    uint64_t now = ui_ms();
    (void)ctx;
    if (now - last_draw < 40 && got != total) return;
    last_draw = now;
    ui_progress_frame("Downloading", install_name, got, total);
}

static void ensure_dir(const char *path)
{
    char dir[96], *slash;
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (!slash || slash[-1] == ':') return;
    *slash = 0;
    mkdir(dir, 0777);
}

static int write_file(const char *path, const void *data, int len)
{
    char tmp[104];
    FILE *f;
    ensure_dir(path);
    snprintf(tmp, sizeof tmp, "%s.part", path);
    f = fopen(tmp, "wb");
    if (!f) return 0;
    if ((int)fwrite(data, 1, len, f) != len) { fclose(f); remove(tmp); return 0; }
    fclose(f);
    remove(path);
    if (rename(tmp, path) != 0) { remove(tmp); return 0; }
    return 1;
}

/* Download one item, check it against the Store's SHA-256 and save it. step of steps when
 * several go together. Says what went wrong itself; returns 1 when it's in place. */
static int fetch(struct item *it, int step, int steps)
{
    char path[96], hex[65], detail[64];
    struct http_resp r;
    int code;

    if (steps > 1) snprintf(detail, sizeof detail, "%s (%d of %d)", it->name, step, steps);
    else snprintf(detail, sizeof detail, "%s", it->name);
    snprintf(path, sizeof path, "/v1/store/file/%s", it->id);
    install_name = detail;
    last_draw = 0;
    ui_progress_frame("Downloading", detail, 0, it->size);
    code = http_request(cfg.server, "GET", path, NULL, 0, it->size + 4096, on_progress, NULL, &r);
    if (code != 200) {
        sfx(SFX_ERROR);
        ui_message("Download failed", cloud_error_text(&r, code));
        http_free(&r);
        return 0;
    }

    ui_loading_frame("Checking the download", "Comparing its SHA-256 with the Store");
    sha256_hex(r.body, r.body_len, hex);
    if (r.body_len != it->size || strcmp(hex, it->sha)) {
        http_free(&r);
        sfx(SFX_ERROR);
        ui_message("The download was damaged", "Nothing more was installed. Try again in a moment.");
        return 0;
    }

    ui_loading_frame("Installing", detail);
    if (!write_file(it->dest, r.body, r.body_len)) {
        http_free(&r);
        sfx(SFX_ERROR);
        ui_message("Couldn't save it", "Is the USB stick full or write-protected?");
        return 0;
    }
    http_free(&r);

    if (!strcmp(it->kind, "app")) {
        char meta[160];
        int n;
        if (it->icon_raw) {
            snprintf(path, sizeof path, "uda:/apps/%s.ximg", it->id);
            write_file(path, it->icon_raw, it->icon_len);
        }
        n = snprintf(meta, sizeof meta, "name=%s\nversion=%s\n", it->name, it->version);
        snprintf(path, sizeof path, "uda:/apps/%s.txt", it->id);
        write_file(path, meta, n);
    }
    return 1;
}

/* While a PC update builds: what it's doing, and for how long. */
static const char *pc_version;
static void pc_step(const char *what, int seconds)
{
    static uint64_t drawn;
    char title[64], detail[96];
    uint64_t now = ui_ms();
    if (now - drawn < 30) return;
    drawn = now;
    snprintf(title, sizeof title, "Updating XenithOS to %s", pc_version);
    if (seconds >= 3) snprintf(detail, sizeof detail, "%s: %d s (about a minute on a PC, a few on a Raspberry Pi)", what, seconds);
    else snprintf(detail, sizeof detail, "%s", what);
    ui_loading_progress(title, detail, 0, 0);
}

/* The Store's "XenithOS update" on a PC: download the source, build it here, restart into it. */
static void install_pc_update(struct item *it)
{
    char body[200], why[200];
    int ok;
    snprintf(body, sizeof body, "XenithOS %s builds itself here from its source: about a minute on a PC, a few on a Raspberry Pi. The current version stays if anything goes wrong.", it->version);
    if (!ui_confirm("Update XenithOS?", body, "Update", "Cancel")) return;
    if (!fetch(it, 1, 1)) {
        update_states();
        return;
    }
    pc_version = it->version;
    ok = plat_install_update(it->dest, pc_step, why, sizeof why);
    remove(it->dest);
    if (!ok) {
        update_states();
        sfx(SFX_ERROR);
        ui_message("The update didn't install", why);
        return;
    }
    it->state = it->own = ST_INSTALLED;          /* XENITH_VERSION is still the old one until the restart */
    sfx(SFX_SUCCESS);
    snprintf(body, sizeof body, "XenithOS %s is installed. Restart now to use it?", it->version);
    if (ui_confirm("XenithOS updated", body, "Restart now", "Later"))
        ui_launch(XENITH_SHELL_PATH, "XenithOS", "Restarting");
}

/* ------------------------------------------------------- apps from Flathub */

static void mb_text(int mb, char *out, int n)
{
    if (mb >= 1000) snprintf(out, n, "%d.%d GB", mb / 1000, mb % 1000 / 100);
    else snprintf(out, n, "%d MB", mb > 0 ? mb : 1);
}

/* Flatpak at work: a progress screen B can stop. */
static const char *fp_title;
static int fp_step(int pct, int seconds)
{
    int cy = SCR_H / 2, w = 320, x = (SCR_W - w) / 2;
    char detail[96];
    ui_poll();
    if (PRESSED(b)) {
        sfx(SFX_BACK);
        if (ui_confirm("Stop now?", "What's been downloaded so far is kept, so trying again carries on from there.", "Stop", "Keep going"))
            return 0;
    }
    if (pct >= 0) snprintf(detail, sizeof detail, "%d%% done, %d:%02d so far", pct, seconds / 60, seconds % 60);
    else snprintf(detail, sizeof detail, "Getting ready, %d:%02d so far", seconds / 60, seconds % 60);
    ui_bg();
    ui_spinner(SCR_W / 2, cy - 44, 24, C_ACCENT);
    gfx_str_c(FONT_LABEL, SCR_W / 2, cy + 6, C_TEXT, fp_title);
    gfx_str_c(FONT_BODY, SCR_W / 2, cy + 46, C_SUB, detail);
    gfx_fill_rrect((float)x, cy + 96.0f, (float)w, 4, 2, C_SURFACE2, 255);
    if (pct > 0) gfx_fill_rrect((float)x, cy + 96.0f, w * pct / 100.0f, 4, 2, C_ACCENT, 255);
    ui_hints("B=Stop");
    ui_end_frame();
    return 1;
}

/* Runs Flatpak for an item behind fp_step's screen. 1 when it worked; says what went wrong. */
static int flathub_run(struct item *it, const char *verb, const char *title, const char *fail_title)
{
    char why[240];
    int r;
    fp_title = title;
    r = plat_flatpak(verb, it->ref, fp_step, why, sizeof why);
    if (r < 0) {
        ui_message("Stopped", "Nothing changed. Anything already downloaded is kept for next time.");
        return 0;
    }
    if (!r) {
        sfx(SFX_ERROR);
        ui_message(fail_title, why);
        return 0;
    }
    return 1;
}

/* What the home screen starts: "uda:/apps/<id>.app", with the Store's thumbnail. */
static void write_app_file(const struct item *it)
{
    char path[96], text[300];
    int n = snprintf(text, sizeof text, "name=%s\nflatpak=%s\nargs=%s\n", it->name, it->ref, it->args);
    write_file(it->dest, text, n);
    if (it->icon_raw) {
        snprintf(path, sizeof path, "uda:/apps/%s.ximg", it->id);
        write_file(path, it->icon_raw, it->icon_len);
    }
}

static void start_item(const struct item *it)
{
    struct app a;
    memset(&a, 0, sizeof a);
    if (is_flathub(it) && file_size(it->dest) < 0) write_app_file(it);   /* installed some other way */
    snprintf(a.path, sizeof a.path, "%s", it->dest);
    snprintf(a.name, sizeof a.name, "%s", it->name);
    app_launch(&a);                                   /* says what went wrong itself */
}

static void not_here(const struct item *it)
{
    char body[240];
    snprintf(body, sizeof body, "%s is only made for %s, and this machine has a %s processor.", it->name,
             strstr(it->arches, "x86_64") ? "PCs with x86-64 processors" : "other processors", plat_arch());
    sfx(SFX_ERROR);
    ui_message("Not available here", body);
}

static int need_flatpak(void)
{
    if (plat_flatpak_ready()) return 1;
    sfx(SFX_ERROR);
    ui_message("Apps need Flatpak", "It isn't on this machine yet. Run the XenithOS install command once more (it adds Flatpak), then come back to the Store.");
    return 0;
}

/* Returns 1 once it's installed. offer_start: ask whether to start it straight away. */
static int install_flathub(struct item *it, int offer_start)
{
    char q[96], body[300], dl[24], shared[24], title[64];
    if (it->unavailable) { not_here(it); return 0; }
    if (!need_flatpak()) return 0;
    mb_text(it->mb, dl, sizeof dl);
    mb_text(it->shared_mb, shared, sizeof shared);
    snprintf(q, sizeof q, "Install %s?", it->name);
    if (it->shared_mb > 0)
        snprintf(body, sizeof body, "%s It downloads about %s, plus up to %s of shared parts if no other app has fetched them yet.", it->desc, dl, shared);
    else
        snprintf(body, sizeof body, "%s It downloads about %s.", it->desc, dl);
    if (!ui_confirm(q, body, "Install", "Cancel")) return 0;
    snprintf(title, sizeof title, "Installing %s", it->name);
    if (!flathub_run(it, "install", title, "It didn't install")) {
        update_states();
        return 0;
    }
    write_app_file(it);
    update_states();
    sfx(SFX_SUCCESS);
    if (!offer_start) return 1;
    if (is_steam_app(it)) {
        if (ui_confirm("Steam is installed", "Open it once to sign in with your Steam account. After that, games you install in Steam show up on XenithOS's home screen.", "Open Steam", "Later"))
            start_item(it);
        return 1;
    }
    snprintf(q, sizeof q, "%s is installed", it->name);
    if (ui_confirm(q, "It's on the home screen and in Apps. To come back to XenithOS from it, hold Guide for 3 seconds.", "Start it", "Done"))
        start_item(it);
    return 1;
}

/* X on an installed app from Flathub. */
static void flathub_options(struct item *it)
{
    static const char *const opts[] = {"Update", "Remove", "Cancel"};
    char title[64], body[160];
    int c;
    if (!need_flatpak()) return;
    c = ui_dialog(it->name, "Get Flathub's newest version of it, or remove it from this machine.", opts, 3);
    if (c == 0) {
        snprintf(title, sizeof title, "Updating %s", it->name);
        if (flathub_run(it, "update", title, "It didn't update")) {
            sfx(SFX_SUCCESS);
            snprintf(body, sizeof body, "%s is on Flathub's newest version.", it->name);
            ui_message("Up to date", body);
        }
    } else if (c == 1) {
        snprintf(title, sizeof title, "Remove %s?", it->name);
        if (!ui_confirm(title, "Its settings and saves stay on this machine, in case you install it again.", "Remove", "Cancel")) return;
        snprintf(title, sizeof title, "Removing %s", it->name);
        if (flathub_run(it, "uninstall", title, "It wasn't removed")) {
            char path[96];
            remove(it->dest);
            snprintf(path, sizeof path, "uda:/apps/%s.ximg", it->id);
            remove(path);
            update_states();
            sfx(SFX_SUCCESS);
            snprintf(title, sizeof title, "%s was removed", it->name);
            ui_toast(title, NULL);
        }
    }
}

/* ------------------------------------------------------------ games on Steam */

static struct item *steam_app_item(void)
{
    int i;
    for (i = 0; i < nitems; i++) if (is_steam_app(&items[i])) return &items[i];
    return NULL;
}

/* A Steam game: Steam opens on it (free games straight to installing, others on their page,
 * where they're bought), after the Store says how Steam games work. */
static void get_on_steam(struct item *it)
{
    char q[96], body[320], url[48];
    struct item *steam = steam_app_item();
    int ok;
    if (it->state == ST_INSTALLED) { start_item(it); return; }
    if (!plat_steam_ready()) {
        snprintf(body, sizeof body, "%s is a Steam game, so it needs the Steam app. Install Steam now?", it->name);
        if (!steam) { sfx(SFX_ERROR); ui_message("Steam isn't in the Store right now", "Try again later."); return; }
        if (!ui_confirm("Get Steam first", body, "Install Steam", "Cancel")) return;
        if (!install_flathub(steam, 0) || !plat_steam_ready()) return;
    }
    snprintf(q, sizeof q, "%s is on Steam", it->name);
    snprintf(body, sizeof body, "Steam games are installed and played through Steam, with your Steam account. %s Steam opens on the game: choose %s there. When it's installed, it shows up on XenithOS's home screen.",
             it->free_game ? "This one is free to play, so the account is all it needs." : "This one is sold on Steam, so it has to be bought there first.",
             it->free_game ? "Install" : "Install or buy");
    if (!ui_confirm(q, body, "Open Steam", "Cancel")) return;
    snprintf(url, sizeof url, "steam://%s/%d", it->free_game ? "install" : "store", it->appid);
    sfx(SFX_LAUNCH);
    ui_loading_frame("Opening Steam", "Hold Guide for 3 seconds to come back to XenithOS.");
    ok = plat_steam_open(url);
    ui_settle_input();
    if (!ok) {
        const char *why = plat_launch_error();
        sfx(SFX_ERROR);
        ui_message("Steam didn't open", why && *why ? why : "Try again in a moment.");
        return;
    }
    plat_sync_apps();
    update_states();
    if (it->state == ST_INSTALLED) {
        snprintf(q, sizeof q, "%s is ready", it->name);
        ui_message(q, "It's on the home screen and in Apps.");
    }
}

/* ---------------------------------------------------------------- the rest */

static void install(struct item *it)
{
    struct item *todo[1 + MAX_NEEDS];
    char q[96], body[200], list[120], sz[24];
    int n = 0, i, bytes = 0;

    if (is_flathub(it)) {
        if (it->unavailable) not_here(it);
        else if (it->state == ST_INSTALLED) start_item(it);
        else install_flathub(it, 1);
        return;
    }
    if (is_steam_game(it)) {
        get_on_steam(it);
        return;
    }
    if (!strcmp(it->kind, "pc")) {
        install_pc_update(it);
        return;
    }
    if (!strcmp(it->kind, "system")) {
        if (!ui_confirm("Update XenithOS?", "This replaces xenon.elf on your USB stick with the version from the Store.", "Update", "Cancel")) return;
        todo[n++] = it;
    } else if (it->nneeds && it->state != ST_INSTALLED) {
        /* It comes with game data: everything that's missing, in one go. */
        const char *verb = it->own == ST_NEW ? "Install" : "Update";
        if (it->own != ST_INSTALLED) todo[n++] = it;
        for (i = 0; i < it->nneeds; i++)
            if (items[it->needs[i]].own != ST_INSTALLED) todo[n++] = &items[it->needs[i]];
        for (i = 0; i < n; i++) bytes += todo[i]->size;
        size_text(bytes, sz, sizeof sz);
        needs_text(it, list, sizeof list);
        snprintf(q, sizeof q, "%s %s?", verb, it->name);
        snprintf(body, sizeof body, "%s comes with %s, so it's ready to play. %s in all.", it->name, list, sz);
        if (!ui_confirm(q, body, verb, "Cancel")) return;
    } else {
        snprintf(q, sizeof q, "%s %s?", it->state == ST_UPDATE ? "Update" : "Install", it->name);
        if (!ui_confirm(q, it->desc, it->state == ST_UPDATE ? "Update" : "Install", "Cancel")) return;
        todo[n++] = it;
    }

    for (i = 0; i < n; i++)
        if (!fetch(todo[i], i + 1, n)) {
            update_states();
            return;
        }
    update_states();
    if (!strcmp(it->kind, "system")) it->state = it->own = ST_INSTALLED;
    sfx(SFX_SUCCESS);

    if (!strcmp(it->kind, "system")) {
        if (ui_confirm("XenithOS updated", "Restart XenithOS now to use the new version?", "Restart now", "Later"))
            ui_launch(XENITH_SHELL_PATH, "XenithOS", "Restarting");
    } else if (!strcmp(it->kind, "app")) {
        snprintf(q, sizeof q, "%s is installed", it->name);
        if (ui_confirm(q, "You'll find it on the home screen and in Apps.", "Start it", "Done"))
            ui_launch(it->dest, it->name, "Starting");
    } else {
        snprintf(q, sizeof q, "%s is installed", it->name);
        ui_message(q, "Apps that use it will find it automatically.");
    }
}

/* ------------------------------------------------------------------ screen */

static const char *kind_label(const char *k)
{
    if (!strcmp(k, "app") || !strcmp(k, "flathub")) return "App";
    if (!strcmp(k, "steam")) return "Steam game";
    if (!strcmp(k, "data")) return "Game data";
    if (!strcmp(k, "system") || !strcmp(k, "pc")) return "System update";
    return k;
}

static void size_text(int bytes, char *out, int n)
{
    if (bytes >= 1048576) snprintf(out, n, "%.1f MB", bytes / 1048576.0);
    else snprintf(out, n, "%d KB", (bytes + 1023) / 1024);
}

/* "App  -  About 285 MB", "Steam game  -  Free to play", "Game data  -  27.4 MB" */
static void meta_text(const struct item *it, int with_version, char *out, int n)
{
    char sz[24];
    if (is_flathub(it)) {
        mb_text(it->mb, sz, sizeof sz);
        snprintf(out, n, "App  -  About %s", sz);
    } else if (is_steam_game(it)) {
        snprintf(out, n, "Steam game  -  %s", it->free_game ? "Free to play" : "Sold on Steam");
    } else {
        size_text(total_size(it), sz, sizeof sz);
        if (with_version) snprintf(out, n, "%s  -  Version %s  -  %s", kind_label(it->kind), it->version, sz);
        else snprintf(out, n, "%s  -  %s", kind_label(it->kind), sz);
    }
}

/* The line under an item's description. */
static void note_text(const struct item *it, char *out, int n)
{
    char sz[24];
    out[0] = 0;
    if (it->nneeds) {
        char list[120];
        needs_text(it, list, sizeof list);
        snprintf(out, n, "Comes with %s.", list);
    } else if (is_flathub(it) && it->unavailable) {
        snprintf(out, n, "Not available for this machine's processor (%s).", plat_arch());
    } else if (is_steam_app(it)) {
        snprintf(out, n, "From Flathub. Sign in with your Steam account the first time it opens.");
    } else if (is_flathub(it)) {
        mb_text(it->shared_mb, sz, sizeof sz);
        if (it->shared_mb > 0) snprintf(out, n, "From Flathub. The first app that needs them also gets up to %s of shared parts.", sz);
        else snprintf(out, n, "From Flathub.");
    } else if (is_steam_game(it)) {
        snprintf(out, n, "Installed and played through Steam, with your Steam account.%s", it->free_game ? "" : " Bought on Steam.");
    }
}

static void draw_row(struct item *it, int x, int y, int w, int h, int sel)
{
    uint32_t fg = C_TEXT, sub = C_SUB;
    int tw = (h - 20) * 16 / 9;
    char meta[64];
    if (it->icon.px) gfx_image_rounded(&it->icon, x + 12, y + 10, tw, h - 20, 8, 255);
    else {
        gfx_fill_rrect(x + 12.0f, y + 10.0f, (float)tw, h - 20.0f, 8, C_SURFACE2, 255);
        icon_draw(ICON_DOWNLOAD, x + 12 + tw / 2.0f, y + h / 2.0f, 26, C_DIM, C_SURFACE2);
    }
    meta_text(it, 0, meta, sizeof meta);
    gfx_str_fit(FONT_BODY_SB, x + tw + 28, y + 14, fg, it->name, w - tw - 150);
    gfx_str_fit(FONT_SMALL, x + tw + 28, y + 44, sub, meta, w - tw - 150);
    if (it->unavailable) gfx_str_r(FONT_SMALL, x + w - 16, y + h / 2 - 10, C_DIM, "Not for this PC");
    else if (it->state == ST_INSTALLED) gfx_str_r(FONT_SMALL, x + w - 16, y + h / 2 - 10, C_OK, "Installed");
    else if (it->state == ST_UPDATE) gfx_str_r(FONT_SMALL, x + w - 16, y + h / 2 - 10, C_ACCENT_HI, "Update");
}

/* Apps / Steam / XenithOS, for LB and RB. Returns the height it takes. */
static float tab_hl;
static int draw_tabs(int x, int y, int w)
{
    int shown[NTABS], n = 0, i, tw, at = 0;
    for (i = 0; i < NTABS; i++) if (tab_count(i)) shown[n++] = i;
    if (n < 2) return 0;
    for (i = 0; i < n; i++) if (shown[i] == tab) at = i;
    tw = (w - 8) / n;
    tab_hl = ui_ease(tab_hl, (float)at, 0.4f);
    gfx_fill_rrect((float)x, (float)y, (float)w, 48, 24, C_SURFACE, 255);
    gfx_stroke_rrect((float)x, (float)y, (float)w, 48, 24, 1, C_LINE, 255);
    gfx_fill_rrect(x + 4 + tab_hl * tw, y + 4.0f, (float)tw, 40, 20, C_ACCENT, 255);
    for (i = 0; i < n; i++)
        gfx_str_c(FONT_BODY_SB, x + 4 + i * tw + tw / 2, y + 11, i == at ? 0xFFFFFF : C_SUB, tab_name[shown[i]]);
    return 60;
}

static void switch_tab(int dir)
{
    int t = tab, k;
    for (k = 0; k < NTABS; k++) {
        t = (t + dir + NTABS) % NTABS;
        if (tab_count(t)) break;
    }
    if (t == tab) return;
    tab = t;
    sfx(SFX_MOVE);
    build_view();
}

static const char *hints_for(const struct item *it)
{
    if (is_flathub(it)) {
        if (it->unavailable) return has_tabs ? "A=Why not;RB=Next section;Y=Refresh;B=Back" : "A=Why not;Y=Refresh;B=Back";
        if (it->state == ST_INSTALLED) return "A=Start;X=Update or remove;RB=Next section;B=Back";
        return "A=Install;RB=Next section;Y=Refresh;B=Back";
    }
    if (is_steam_game(it))
        return it->state == ST_INSTALLED ? "A=Play;RB=Next section;Y=Refresh;B=Back" : "A=Get it on Steam;RB=Next section;Y=Refresh;B=Back";
    if (has_tabs)
        return it->state == ST_INSTALLED ? "A=Reinstall;RB=Next section;Y=Refresh;B=Back"
             : it->state == ST_UPDATE ? "A=Update;RB=Next section;Y=Refresh;B=Back" : "A=Install;RB=Next section;Y=Refresh;B=Back";
    return it->state == ST_INSTALLED ? "A=Reinstall;Y=Refresh;B=Back"
         : it->state == ST_UPDATE ? "A=Update;Y=Refresh;B=Back" : "A=Install;Y=Refresh;B=Back";
}

void screen_store(void)
{
    int sel = 0, first = 0, i;
    float hl = 0;

    if (!cloud_ready("Store")) return;
    if (!load_store()) return;
    tab_hl = -1;
    ui_fade_in();

    for (;;) {
        int listw = (SCR_W - 2 * M) * 45 / 100, rowh = 84, top = HEADER_H + 16, list_top, visible, px, pw, d;
        char meta[96], note[160];

        ui_poll();
        d = ui_nav_y();
        if (d && nview && sel + d >= 0 && sel + d < nview) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(b)) { sfx(SFX_BACK); free_items(); return; }
        if (has_tabs && (PRESSED(rb) || PRESSED(lb))) {
            switch_tab(PRESSED(rb) ? 1 : -1);
            sel = first = 0;
            hl = 0;
        }
        if (PRESSED(y)) {
            int keep = tab;
            sfx(SFX_SELECT);
            if (!load_store()) return;
            if (has_tabs && tab_count(keep)) { tab = keep; build_view(); }
            if (sel >= nview) sel = 0;
        }
        if (PRESSED(a) && nview) { sfx(SFX_SELECT); install(&items[view[sel]]); }
        if (PRESSED(x) && nview && is_flathub(&items[view[sel]]) && items[view[sel]].state == ST_INSTALLED) {
            sfx(SFX_SELECT);
            flathub_options(&items[view[sel]]);
        }
        if (sel >= nview) sel = nview ? nview - 1 : 0;

        ui_bg();
        ui_header("Store", !has_tabs ? "Apps, games and updates from XenithOS Cloud"
                           : tab_count(TAB_STEAM) ? "Apps, Steam games and XenithOS updates. LB and RB switch sections."
                                                  : "Apps and XenithOS updates. LB and RB switch sections.");
        list_top = top + (has_tabs ? draw_tabs(M, top, listw) : 0);
        if (tab_hl < 0) tab_hl = 0;
        visible = (SCR_H - list_top - 70) / rowh;
        if (visible < 1) visible = 1;
        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;
        hl = ui_ease(hl, (float)(sel - first), 0.4f);

        if (!nview) {
            gfx_str(FONT_LABEL, M, list_top + 10, C_TEXT, "The Store is empty");
        } else {
            ui_select_box((float)M, list_top + hl * rowh, (float)listw, rowh - 8.0f, 10);
            for (i = 0; i < visible && first + i < nview; i++)
                draw_row(&items[view[first + i]], M, list_top + i * rowh, listw, rowh - 8, first + i == sel);

            /* Details */
            {
                struct item *it = &items[view[sel]];
                int iw, ih, ix, y, lines, ph = SCR_H - top - 70, below;
                px = M + listw + 28;
                pw = SCR_W - M - px;
                ui_panel(px, top, pw, ph);
                /* The picture gets what the text under it leaves: the name, the details, up to
                 * three lines of description and two of notes. The same for every item, so the
                 * picture doesn't change size between them. */
                below = 20 + gfx_line_h(FONT_TITLE) + 4 + gfx_line_h(FONT_SMALL) + 12 + 3 * (gfx_line_h(FONT_BODY) + 4)
                        + 10 + 2 * (gfx_line_h(FONT_SMALL) + 4) + 14;
                iw = pw - 48;
                ih = iw * 9 / 16;
                if (ih > ph - 24 - below) { ih = ph - 24 - below; iw = ih * 16 / 9; }
                ix = px + (pw - iw) / 2;
                if (it->icon.px) gfx_image_rounded(&it->icon, ix, top + 24, iw, ih, 12, 255);
                else gfx_fill_rrect((float)ix, top + 24.0f, (float)iw, (float)ih, 12, C_SURFACE2, 255);
                y = top + 24 + ih + 20;
                gfx_str_fit(FONT_TITLE, px + 24, y, C_TEXT, it->name, pw - 48);
                y += gfx_line_h(FONT_TITLE) + 4;
                meta_text(it, 1, meta, sizeof meta);
                gfx_str_fit(FONT_SMALL, px + 24, y, C_SUB, meta, pw - 48);
                y += gfx_line_h(FONT_SMALL) + 12;
                lines = gfx_wrap(FONT_BODY, px + 24, y, pw - 48, 4, C_TEXT, it->desc, 3);
                note_text(it, note, sizeof note);
                if (note[0]) {
                    y += lines * (gfx_line_h(FONT_BODY) + 4) + 10;
                    gfx_wrap(FONT_SMALL, px + 24, y, pw - 48, 4, it->unavailable ? C_WARN : C_SUB, note, 2);
                }
            }
        }
        if (nview) ui_hints(hints_for(&items[view[sel]]));
        else ui_hints(has_tabs ? "RB=Next section;Y=Refresh;B=Back" : "Y=Refresh;B=Back");
        ui_end_frame();
    }
}
