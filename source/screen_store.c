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

struct item {
    char id[33], kind[8], name[25], version[13], sha[65], file[48], desc[100];
    int size, state;
    char dest[96];
    struct img icon;
    unsigned char *icon_raw;
    int icon_len;
};

static struct item items[MAX_ITEMS];
static int nitems;

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
    } else {
        return 0;
    }
    memcpy(it->dest, path, sizeof path);
    return 1;
}

static int file_size(const char *path)
{
    struct stat s;
    return stat(path, &s) == 0 ? (int)s.st_size : -1;
}

static void update_state(struct item *it)
{
    int have = file_size(it->dest);
    if (!strcmp(it->kind, "system"))
        it->state = !strcmp(it->version, XENITH_VERSION) ? ST_INSTALLED : ST_UPDATE;
    else
        it->state = have < 0 ? ST_NEW : have == it->size ? ST_INSTALLED : ST_UPDATE;
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
            if (it->size > 0 && strlen(it->sha) == 64 && item_dest(it)) {
                update_state(it);
                nitems++;
            }
        }
        line = next;
    }
    http_free(&r);

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

static void install(struct item *it)
{
    char path[96], hex[65], q[96];
    struct http_resp r;
    int code;

    if (!strcmp(it->kind, "system")) {
        if (!ui_confirm("Update XenithOS?", "This replaces xenon.elf on your USB stick with the version from the Store.", "Update", "Cancel")) return;
    } else {
        snprintf(q, sizeof q, "%s %s?", it->state == ST_UPDATE ? "Update" : "Install", it->name);
        if (!ui_confirm(q, it->desc, it->state == ST_UPDATE ? "Update" : "Install", "Cancel")) return;
    }

    snprintf(path, sizeof path, "/v1/store/file/%s", it->id);
    install_name = it->name;
    last_draw = 0;
    ui_progress_frame("Downloading", it->name, 0, it->size);
    code = http_request(cfg.server, "GET", path, NULL, 0, it->size + 4096, on_progress, NULL, &r);
    if (code != 200) {
        sfx(SFX_ERROR);
        ui_message("Download failed", cloud_error_text(&r, code));
        http_free(&r);
        return;
    }

    ui_loading_frame("Checking the download", "Comparing its SHA-256 with the Store");
    sha256_hex(r.body, r.body_len, hex);
    if (r.body_len != it->size || strcmp(hex, it->sha)) {
        http_free(&r);
        sfx(SFX_ERROR);
        ui_message("The download was damaged", "Nothing was installed. Try again in a moment.");
        return;
    }

    ui_loading_frame("Installing", it->dest);
    if (!write_file(it->dest, r.body, r.body_len)) {
        http_free(&r);
        sfx(SFX_ERROR);
        ui_message("Couldn't save it", "Is the USB stick full or write-protected?");
        return;
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
    update_state(it);
    if (!strcmp(it->kind, "system")) it->state = ST_INSTALLED;
    sfx(SFX_SUCCESS);

    if (!strcmp(it->kind, "system")) {
        if (ui_confirm("XenithOS updated", "Restart XenithOS now to use the new version?", "Restart now", "Later"))
            plat_launch(XENITH_SHELL_PATH);
    } else if (!strcmp(it->kind, "app")) {
        snprintf(q, sizeof q, "%s is installed", it->name);
        if (ui_confirm(q, "You'll find it on the home screen and in Apps.", "Start it", "Done"))
            plat_launch(it->dest);
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
    if (!strcmp(k, "system")) return "System update";
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
    size_text(it->size, sz, sizeof sz);
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
        if (d && nitems && sel + d >= 0 && sel + d < nitems) { sel += d; sfx(SFX_MOVE); }
        if (PRESSED(b)) { sfx(SFX_BACK); free_items(); return; }
        if (PRESSED(y)) { sfx(SFX_SELECT); if (!load_store()) return; if (sel >= nitems) sel = 0; }
        if (PRESSED(a) && nitems) { sfx(SFX_SELECT); install(&items[sel]); }

        if (sel < first) first = sel;
        if (sel >= first + visible) first = sel - visible + 1;
        hl = ui_ease(hl, (float)(sel - first), 0.4f);

        ui_bg();
        ui_header("Store", "Apps, games and updates from XenithOS Cloud");
        if (!nitems) {
            gfx_str(FONT_LABEL, M, top + 10, C_TEXT, "The Store is empty");
        } else {
            gfx_fill_rrect((float)M, top + hl * rowh, (float)listw, rowh - 8.0f, 14, C_ACCENT, 255);
            for (i = 0; i < visible && first + i < nitems; i++)
                draw_row(&items[first + i], M, top + i * rowh, listw, rowh - 8, first + i == sel, hl);

            /* Details */
            {
                struct item *it = &items[sel];
                int iw, ih, y;
                px = M + listw + 28;
                pw = SCR_W - M - px;
                ui_panel(px, top, pw, SCR_H - top - 70);
                iw = pw - 48;
                ih = iw * 9 / 16;
                if (ih > (SCR_H - top) / 2) { ih = (SCR_H - top) / 2; iw = ih * 16 / 9; }
                if (it->icon.px) gfx_image_rounded(&it->icon, px + 24, top + 24, iw, ih, 12, 255);
                else gfx_fill_rrect(px + 24.0f, top + 24.0f, (float)iw, (float)ih, 12, C_SURFACE2, 255);
                y = top + 24 + ih + 20;
                gfx_str_fit(FONT_TITLE, px + 24, y, C_TEXT, it->name, pw - 48);
                y += gfx_line_h(FONT_TITLE) + 4;
                size_text(it->size, sz, sizeof sz);
                snprintf(meta, sizeof meta, "%s  -  Version %s  -  %s", kind_label(it->kind), it->version, sz);
                gfx_str(FONT_SMALL, px + 24, y, C_SUB, meta);
                y += gfx_line_h(FONT_SMALL) + 12;
                gfx_wrap(FONT_BODY, px + 24, y, pw - 48, 4, C_TEXT, it->desc, 3);
            }
        }
        if (nitems) {
            const char *act = items[sel].state == ST_INSTALLED ? "A=Reinstall;Y=Refresh;B=Back"
                            : items[sel].state == ST_UPDATE ? "A=Update;Y=Refresh;B=Back" : "A=Install;Y=Refresh;B=Back";
            ui_hints(act);
        } else {
            ui_hints("Y=Refresh;B=Back");
        }
        ui_end_frame();
    }
}
