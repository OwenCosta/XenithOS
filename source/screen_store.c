/* STORE: browse and install from XenithOS Cloud. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

#define MAX_ITEMS 32
enum { ST_NEW, ST_INSTALLED, ST_UPDATE };

#define MAX_NEEDS 4

struct item {
    char id[33], kind[8], name[25], version[13], sha[65], file[48], desc[100];
    int size, state;
    int own;                     /* the state of its own file; state also counts its game data */
    int needs[MAX_NEEDS], nneeds;   /* game data it comes with (indexes into items) */
    int needed;                  /* comes with another item, so it isn't listed by itself */
    char dest[96];
    struct img icon;
    unsigned char *icon_raw;
    int icon_len;
};

static struct item items[MAX_ITEMS];
static int nitems;
static int view[MAX_ITEMS], nview;      /* the items listed, in order */

static void size_text(int bytes, char *out, int n);

static int safe_name(const char *s)
{
    if (!*s || *s == '.') return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '.' && *s != '_' && *s != '-') return 0;
    return 1;
}

/* Where an item goes on the USB stick. The server never picks the path. */
static int item_dest(struct item *it)
{
    char path[sizeof it->dest];
    if (!strcmp(it->kind, "app")) {
        if (!safe_name(it->id)) return 0;
        snprintf(path, sizeof path, "uda:/apps/%s.elf32", it->id);
    } else if (!strcmp(it->kind, "data")) {
        if (!safe_name(it->file)) return 0;
        snprintf(path, sizeof path, "uda:/data/%s", it->file);
    } else if (!strcmp(it->kind, "system")) {
        snprintf(path, sizeof path, "%s", XENITH_SHELL_PATH);
    } else if (!strcmp(it->kind, "pc") && plat_kind() == PLAT_PC) {
        snprintf(path, sizeof path, "uda:/xenith/xenithos-update.tar.gz");   /* the source; built here */
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
    int have = file_size(it->dest);
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
    update_states();
    nview = 0;
    for (i = 0; i < nitems; i++)
        if (!items[i].needed) view[nview++] = i;

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

static void install(struct item *it)
{
    struct item *todo[1 + MAX_NEEDS];
    char q[96], body[200], list[120], sz[24];
    int n = 0, i, bytes = 0;

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
    if (!strcmp(k, "app")) return "App";
    if (!strcmp(k, "data")) return "Game data";
    if (!strcmp(k, "system") || !strcmp(k, "pc")) return "System update";
    return k;
}

static void size_text(int bytes, char *out, int n)
{
    if (bytes >= 1048576) snprintf(out, n, "%.1f MB", bytes / 1048576.0);
    else snprintf(out, n, "%d KB", (bytes + 1023) / 1024);
}

static void draw_row(struct item *it, int x, int y, int w, int h, int sel, float hl_y)
{
    uint32_t fg = sel ? 0xFFFFFF : C_TEXT, sub = sel ? 0xDDE8FF : C_SUB;
    int tw = (h - 20) * 16 / 9;
    char meta[64], sz[24];
    (void)hl_y;
    if (it->icon.px) gfx_image_rounded(&it->icon, x + 12, y + 10, tw, h - 20, 8, 255);
    else {
        gfx_fill_rrect(x + 12.0f, y + 10.0f, (float)tw, h - 20.0f, 8, C_SURFACE2, 255);
        icon_draw(ICON_DOWNLOAD, x + 12 + tw / 2.0f, y + h / 2.0f, 26, C_DIM, C_SURFACE2);
    }
    size_text(total_size(it), sz, sizeof sz);
    snprintf(meta, sizeof meta, "%s  -  %s", kind_label(it->kind), sz);
    gfx_str_fit(FONT_BODY_SB, x + tw + 28, y + 14, fg, it->name, w - tw - 150);
    gfx_str(FONT_SMALL, x + tw + 28, y + 44, sub, meta);
    if (it->state == ST_INSTALLED) gfx_str_r(FONT_SMALL, x + w - 16, y + h / 2 - 10, sel ? 0xFFFFFF : C_OK, "Installed");
    if (it->state == ST_UPDATE) gfx_str_r(FONT_SMALL, x + w - 16, y + h / 2 - 10, sel ? 0xFFFFFF : C_ACCENT_HI, "Update");
}

void screen_store(void)
{
    int sel = 0, first = 0, i;
    float hl = 0;

    if (!cloud_ready("Store")) return;
    if (!load_store()) return;
    ui_fade_in();

    for (;;) {
        int listw = (SCR_W - 2 * M) * 45 / 100, rowh = 84, top = HEADER_H + 16;
        int visible = (SCR_H - top - 70) / rowh, px, pw, d;
        char sz[24], meta[96];

        ui_poll();
        d = ui_nav_y();
        if (d && nview && sel + d >= 0 && sel + d < nview) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(b)) { sfx(SFX_BACK); free_items(); return; }
        if (PRESSED(y)) { sfx(SFX_SELECT); if (!load_store()) return; if (sel >= nview) sel = 0; }
        if (PRESSED(a) && nview) { sfx(SFX_SELECT); install(&items[view[sel]]); }

        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;
        hl = ui_ease(hl, (float)(sel - first), 0.4f);

        ui_bg();
        ui_header("Store", plat_kind() == PLAT_XBOX ? "Apps, games and updates from XenithOS Cloud" : "Updates and game data from XenithOS Cloud. Apps come with XenithOS here.");
        if (!nview) {
            gfx_str(FONT_LABEL, M, top + 10, C_TEXT, "The Store is empty");
        } else {
            gfx_fill_rrect((float)M, top + hl * rowh, (float)listw, rowh - 8.0f, 14, C_ACCENT, 255);
            for (i = 0; i < visible && first + i < nview; i++)
                draw_row(&items[view[first + i]], M, top + i * rowh, listw, rowh - 8, first + i == sel, hl);

            /* Details */
            {
                struct item *it = &items[view[sel]];
                int iw, ih, ix, y, lines, ph = SCR_H - top - 70, below;
                px = M + listw + 28;
                pw = SCR_W - M - px;
                ui_panel(px, top, pw, ph);
                /* The picture gets what the text under it leaves: the name, the details, up to
                 * three lines of description and a line for the game data an app comes with.
                 * The same for every item, so the picture doesn't change size between them. */
                below = 20 + gfx_line_h(FONT_TITLE) + 4 + gfx_line_h(FONT_SMALL) + 12 + 3 * (gfx_line_h(FONT_BODY) + 4)
                        + 10 + gfx_line_h(FONT_SMALL) + 18;
                iw = pw - 48;
                ih = iw * 9 / 16;
                if (ih > ph - 24 - below) { ih = ph - 24 - below; iw = ih * 16 / 9; }
                ix = px + (pw - iw) / 2;
                if (it->icon.px) gfx_image_rounded(&it->icon, ix, top + 24, iw, ih, 12, 255);
                else gfx_fill_rrect((float)ix, top + 24.0f, (float)iw, (float)ih, 12, C_SURFACE2, 255);
                y = top + 24 + ih + 20;
                gfx_str_fit(FONT_TITLE, px + 24, y, C_TEXT, it->name, pw - 48);
                y += gfx_line_h(FONT_TITLE) + 4;
                size_text(total_size(it), sz, sizeof sz);
                snprintf(meta, sizeof meta, "%s  -  Version %s  -  %s", kind_label(it->kind), it->version, sz);
                gfx_str(FONT_SMALL, px + 24, y, C_SUB, meta);
                y += gfx_line_h(FONT_SMALL) + 12;
                lines = gfx_wrap(FONT_BODY, px + 24, y, pw - 48, 4, C_TEXT, it->desc, 3);
                if (it->nneeds) {
                    char list[120], line[160];
                    needs_text(it, list, sizeof list);
                    snprintf(line, sizeof line, "Comes with %s.", list);
                    y += lines * (gfx_line_h(FONT_BODY) + 4) + 10;
                    gfx_wrap(FONT_SMALL, px + 24, y, pw - 48, 4, C_SUB, line, 1);
                }
            }
        }
        if (nview) {
            const struct item *it = &items[view[sel]];
            const char *act = it->state == ST_INSTALLED ? "A=Reinstall;Y=Refresh;B=Back"
                            : it->state == ST_UPDATE ? "A=Update;Y=Refresh;B=Back" : "A=Install;Y=Refresh;B=Back";
            ui_hints(act);
        } else {
            ui_hints("Y=Refresh;B=Back");
        }
        ui_end_frame();
    }
}
