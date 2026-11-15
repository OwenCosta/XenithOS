/* STORE and CLOUD screens: everything that talks to the XENITH Cloud server. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>

#include <elf/elf.h>
#include <ppc/timebase.h>

#include "gfx.h"
#include "theme.h"
#include "ui.h"
#include "net.h"
#include "config.h"
#include "sha256.h"
#include "screens.h"

#define XENITH_VER "0.3"

/* --------------------------------------------------------------- helpers */

/* Make sure the server is configured and the network is up. Shows why if not. */
static int connect_or_explain(const char *title)
{
    if (!cfg.server[0]) {
        draw_chrome(title, "B BACK");
        ui_alert("SERVER NOT SET", "ADD server=IP:PORT TO XENITH/CONFIG.TXT ON THE USB STICK", C_WARN);
        return 0;
    }
    draw_chrome(title, NULL);
    ui_panel("CONNECTING", "GETTING AN ADDRESS FROM YOUR ROUTER", C_TEXT);
    if (!net_up()) {
        ui_alert("NO NETWORK", "PLUG AN ETHERNET CABLE INTO THE XBOX, THEN TRY AGAIN", C_BAD);
        return 0;
    }
    return 1;
}

/* Value for key in "key=value\n" lines. */
static void kv_get(const char *body, const char *key, char *out, int size)
{
    int klen = strlen(key);
    const char *p = body;
    out[0] = 0;
    while (p && *p) {
        if (!strncmp(p, key, klen) && p[klen] == '=') {
            const char *v = p + klen + 1, *e = strchr(v, '\n');
            int n = e ? (int)(e - v) : (int)strlen(v);
            if (n >= size) n = size - 1;
            memcpy(out, v, n);
            out[n] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

/* Split a line on '|' in place. Returns the number of fields. */
static int split(char *line, char **f, int max)
{
    int n = 0;
    f[n++] = line;
    for (; *line && n < max; line++)
        if (*line == '|') { *line = 0; f[n++] = line + 1; }
    return n;
}

/* ----------------------------------------------------------------- STORE */

#define MAX_ITEMS 32

struct item {
    char id[33], kind[8], name[25], version[13], sha[65], file[48], desc[96];
    int size;
    char dest[96];
    int installed;
};

static struct item items[MAX_ITEMS];
static int nitems;

static int safe_filename(const char *s)
{
    if (!*s || *s == '.') return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '.' && *s != '_' && *s != '-') return 0;
    return 1;
}

/* Where an item lands on the USB stick. The server never chooses arbitrary paths. */
static int item_dest(struct item *it)
{
    char path[sizeof it->dest];
    if (!strcmp(it->kind, "app")) {
        if (!safe_filename(it->id)) return 0;
        snprintf(path, sizeof path, "uda:/apps/%s.elf32", it->id);
    } else if (!strcmp(it->kind, "data")) {
        if (!safe_filename(it->file)) return 0;
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
    struct stat st;
    return stat(path, &st) == 0 ? (int)st.st_size : -1;
}

static void parse_catalog(char *body)
{
    char *line = body;
    nitems = 0;
    while (line && *line && nitems < MAX_ITEMS) {
        char *next = strchr(line, '\n'), *f[8];
        if (next) *next++ = 0;
        if (split(line, f, 8) == 8) {
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
                it->installed = file_size(it->dest) == it->size;
                nitems++;
            }
        }
        line = next;
    }
}

static int last_drawn_kb;
static void on_progress(int got, int total, void *ctx)
{
    if (got / 65536 == last_drawn_kb && got != total) return;   /* redraw every 64 KB */
    last_drawn_kb = got / 65536;
    ui_progress((const char *)ctx, got, total);
}

static void ensure_dir(const char *path)
{
    char dir[96];
    char *slash;
    snprintf(dir, sizeof dir, "%s", path);
    slash = strrchr(dir, '/');
    if (!slash || slash[-1] == ':') return;   /* file in the root */
    *slash = 0;
    mkdir(dir, 0777);
}

static int write_file(const char *path, const char *data, int len)
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
    char path[64], hex[65], q[64], label[48];
    struct http_resp r;
    int st;

    if (!strcmp(it->kind, "system")) {
        if (!confirm("UPDATE XENITH ITSELF?")) return;
    } else {
        snprintf(q, sizeof q, "INSTALL %s?", it->name);
        if (!confirm(q)) return;
    }

    snprintf(path, sizeof path, "/v1/store/file/%s", it->id);
    snprintf(label, sizeof label, "DOWNLOADING %s", it->name);
    last_drawn_kb = -1;
    ui_progress(label, 0, it->size);
    st = http_request(cfg.server, "GET", path, NULL, 0, it->size + 4096, on_progress, label, &r);
    if (st != 200) {
        ui_alert("DOWNLOAD FAILED", st < 0 ? net_error(st) : "SERVER REFUSED THE DOWNLOAD", C_BAD);
        http_free(&r);
        return;
    }

    ui_panel("CHECKING", "VERIFYING SHA-256", C_TEXT);
    sha256_hex(r.body, r.body_len, hex);
    if (r.body_len != it->size || strcmp(hex, it->sha)) {
        http_free(&r);
        ui_alert("CHECK FAILED", "THE DOWNLOAD WAS DAMAGED. NOTHING WAS INSTALLED", C_BAD);
        return;
    }

    ui_panel("INSTALLING", it->dest, C_TEXT);
    if (!write_file(it->dest, r.body, r.body_len)) {
        http_free(&r);
        ui_alert("WRITE FAILED", "IS THE USB STICK FULL OR LOCKED?", C_BAD);
        return;
    }
    http_free(&r);
    it->installed = 1;

    if (!strcmp(it->kind, "system")) {
        if (confirm("UPDATED. RESTART XENITH NOW?")) elf_runFromDisk(XENITH_SHELL_PATH);
    } else if (!strcmp(it->kind, "app")) {
        if (confirm("INSTALLED. LAUNCH IT NOW?")) elf_runFromDisk(it->dest);
    } else {
        ui_alert("INSTALLED", it->dest, C_OK);
    }
}

static void draw_store_row(int i, int sel, int first, int rowh)
{
    char sub[64];
    struct item *it = &items[i];
    snprintf(sub, sizeof sub, "V%s  %d KB  %s%s", it->version, (it->size + 1023) / 1024,
             it->kind, it->installed ? "  INSTALLED" : "");
    draw_row(TOP + (i - first) * rowh, rowh - S * 2, i == sel, S, it->name, sub);
}

static void draw_store_desc(int sel, int visible, int rowh)
{
    int y = TOP + visible * rowh + S * 4;
    gfx_rect(M, y, SCR_W - 2 * M, GFX_TEXT_H(S - 1) + S * 2, C_BG);
    if (nitems)
        gfx_text_fit(M + S * 8, y, S - 1, C_DIM, items[sel].desc, SCR_W - 2 * M - S * 8);
}

void screen_store(void)
{
    struct http_resp r;
    int rowh = GFX_TEXT_H(S) + S * 10;
    int visible = (SCR_H - TOP - M - GFX_TEXT_H(S) * 2) / rowh;
    int sel = 0, first = 0, i, st;

    if (!connect_or_explain("STORE")) return;

reload:
    draw_chrome("STORE", NULL);
    ui_panel("LOADING STORE", cfg.server, C_TEXT);
    st = http_request(cfg.server, "GET", "/v1/store", NULL, 0, 64 * 1024, NULL, NULL, &r);
    if (st != 200) {
        http_free(&r);
        ui_alert("STORE UNAVAILABLE", st < 0 ? net_error(st) : "SERVER ERROR", C_BAD);
        return;
    }
    parse_catalog(r.body);
    http_free(&r);

    draw_chrome("STORE", "A INSTALL     Y REFRESH     B BACK");
    if (!nitems) {
        gfx_text(M, TOP, S, C_TEXT, "THE STORE IS EMPTY");
        gfx_flush();
    }
    if (sel >= nitems) sel = 0;
    first = sel < visible ? 0 : sel - visible + 1;
    for (i = first; i < nitems && i < first + visible; i++) draw_store_row(i, sel, first, rowh);
    draw_store_desc(sel, visible, rowh);
    gfx_flush();

    for (;;) {
        poll_input();
        if (PRESSED(b)) return;
        if (PRESSED(y)) goto reload;
        if (!nitems) continue;

        int d = nav_y();
        if (d) {
            int old = sel, nf;
            sel = (sel + d + nitems) % nitems;
            nf = sel < visible ? 0 : sel - visible + 1;
            if (nf != first) {
                first = nf;
                gfx_rect(M, TOP, SCR_W - 2 * M, visible * rowh, C_BG);
                for (i = first; i < nitems && i < first + visible; i++) draw_store_row(i, sel, first, rowh);
            } else {
                draw_store_row(old, sel, first, rowh);
                draw_store_row(sel, sel, first, rowh);
            }
            draw_store_desc(sel, visible, rowh);
            gfx_flush();
        }

        if (PRESSED(a)) {
            wait_release();
            install(&items[sel]);
            goto reload;
        }
    }
}

/* ----------------------------------------------------------------- CLOUD */

static void bar(int x, int y, int w, int h, long long used, long long total)
{
    gfx_rect(x, y, w, h, C_PANEL);
    if (total > 0) {
        long long fill = w * used / total;
        uint32_t c = used * 100 / total >= 90 ? C_BAD : used * 100 / total >= 70 ? C_WARN : C_OK;
        gfx_rect(x, y, (int)fill, h, c);
    }
}

static int draw_cloud(void)
{
    struct http_resp r;
    char path[96], name[32], v[128], buf[256];
    int st, ping_ms, y, col2, colw, line = GFX_TEXT_H(S) + S * 6, i;
    uint64_t t0;

    net_escape(cfg.name, name, sizeof name);
    snprintf(path, sizeof path, "/v1/hello?name=%s&ver=" XENITH_VER, name);

    t0 = mftb();
    st = http_request(cfg.server, "GET", "/v1/ping", NULL, 0, 1024, NULL, NULL, &r);
    ping_ms = (int)tb_diff_msec(mftb(), t0);
    http_free(&r);
    if (st != 200) {
        ui_alert("SERVER UNREACHABLE", st < 0 ? net_error(st) : "SERVER ERROR", C_BAD);
        return 0;
    }
    st = http_request(cfg.server, "GET", path, NULL, 0, 16 * 1024, NULL, NULL, &r);
    if (st != 200) {
        http_free(&r);
        ui_alert("SERVER ERROR", "COULD NOT READ SERVER STATUS", C_BAD);
        return 0;
    }

    draw_chrome("CLOUD", "Y REFRESH     B BACK     UPDATES EVERY 15 SECONDS");
    colw = (SCR_W - 2 * M) / 2 - S * 6;
    col2 = M + colw + S * 12;

    /* Message of the day across the top. */
    kv_get(r.body, "motd", v, sizeof v);
    gfx_rect(M, TOP, SCR_W - 2 * M, GFX_TEXT_H(S + 1) + S * 8, C_PANEL);
    gfx_rect(M, TOP, S * 2, GFX_TEXT_H(S + 1) + S * 8, C_ACCENT);
    gfx_text_fit(M + S * 8, TOP + S * 4, S + 1, C_TEXT, v, SCR_W - 2 * M - S * 12);
    y = TOP + GFX_TEXT_H(S + 1) + S * 16;

    /* Left: the server. */
    {
        int yy = y;
        long long mu, mt, du, dt;
        char a[32], b[32];
        gfx_text(M, yy, S - 1, C_DIM, "YOUR SERVER");
        yy += GFX_TEXT_H(S - 1) + S * 4;

        kv_get(r.body, "uptime", v, sizeof v);
        snprintf(buf, sizeof buf, "UPTIME   %s", v);
        gfx_text_fit(M, yy, S, C_TEXT, buf, colw); yy += line;

        kv_get(r.body, "load", v, sizeof v);
        kv_get(r.body, "cpus", a, sizeof a);
        snprintf(buf, sizeof buf, "LOAD     %s  (%s CPU)", v, a);
        gfx_text_fit(M, yy, S, C_TEXT, buf, colw); yy += line;

        kv_get(r.body, "mem_used_mb", a, sizeof a);
        kv_get(r.body, "mem_total_mb", b, sizeof b);
        mu = atoll(a); mt = atoll(b);
        snprintf(buf, sizeof buf, "MEMORY   %lld / %lld MB", mu, mt);
        gfx_text_fit(M, yy, S, C_TEXT, buf, colw); yy += GFX_TEXT_H(S) + S * 2;
        bar(M, yy, colw, S * 2, mu, mt); yy += S * 6;

        kv_get(r.body, "disk_used_gb", a, sizeof a);
        kv_get(r.body, "disk_total_gb", b, sizeof b);
        du = atoll(a); dt = atoll(b);
        snprintf(buf, sizeof buf, "DISK     %lld / %lld GB", du, dt);
        gfx_text_fit(M, yy, S, C_TEXT, buf, colw); yy += GFX_TEXT_H(S) + S * 2;
        bar(M, yy, colw, S * 2, du, dt); yy += S * 6;

        snprintf(buf, sizeof buf, "PING     %d MS", ping_ms);
        gfx_text(M, yy, S, ping_ms < 80 ? C_OK : ping_ms < 200 ? C_WARN : C_BAD, buf); yy += line;

        kv_get(r.body, "time", v, sizeof v);
        snprintf(buf, sizeof buf, "TIME     %s", v);
        gfx_text_fit(M, yy, S, C_DIM, buf, colw); yy += line;

        snprintf(buf, sizeof buf, "XBOX IP  %s", net_ip());
        gfx_text_fit(M, yy, S, C_DIM, buf, colw);
    }
    http_free(&r);

    /* Right: inbox, then the VOLT leaderboard. */
    {
        int yy = y;
        gfx_text(col2, yy, S - 1, C_DIM, "INBOX");
        yy += GFX_TEXT_H(S - 1) + S * 4;
        snprintf(path, sizeof path, "/v1/inbox?key=%s", cfg.key);
        st = http_request(cfg.server, "GET", path, NULL, 0, 16 * 1024, NULL, NULL, &r);
        if (st == 200 && r.body_len) {
            char *ln = r.body;
            for (i = 0; i < 4 && ln && *ln; i++) {
                char *next = strchr(ln, '\n'), *f[3];
                if (next) *next++ = 0;
                if (split(ln, f, 3) == 3) {
                    gfx_rect(col2, yy - S, S, GFX_TEXT_H(S) + S * 2, C_ACCENT);
                    gfx_text_fit(col2 + S * 4, yy, S, C_TEXT, f[2], colw - S * 4);
                    yy += line;
                }
                ln = next;
            }
        } else {
            gfx_text(col2, yy, S, C_DIM, st == 200 ? "NO MESSAGES" : "INBOX UNAVAILABLE");
            yy += line;
        }
        http_free(&r);

        yy += S * 4;
        gfx_text(col2, yy, S - 1, C_DIM, "VOLT TOP SCORES");
        yy += GFX_TEXT_H(S - 1) + S * 4;
        st = http_request(cfg.server, "GET", "/v1/scores/volt", NULL, 0, 16 * 1024, NULL, NULL, &r);
        if (st == 200 && r.body_len) {
            char *ln = r.body;
            for (i = 0; i < 5 && ln && *ln; i++) {
                char *next = strchr(ln, '\n'), *f[3];
                if (next) *next++ = 0;
                if (split(ln, f, 3) == 3) {
                    int mine = !strcmp(f[1], cfg.name);
                    snprintf(buf, sizeof buf, "%s.  %-12s %s", f[0], f[1], f[2]);
                    gfx_text_fit(col2, yy, S, mine ? C_ACCENT : C_TEXT, buf, colw);
                    yy += line;
                }
                ln = next;
            }
        } else {
            gfx_text(col2, yy, S, C_DIM, "NO SCORES YET. PLAY VOLT!");
        }
        http_free(&r);
    }

    gfx_flush();
    return 1;
}

void screen_cloud(void)
{
    int frames = 0;
    if (!connect_or_explain("CLOUD")) return;
    draw_chrome("CLOUD", NULL);
    ui_panel("CONTACTING SERVER", cfg.server, C_TEXT);
    if (!draw_cloud()) return;

    for (;;) {
        poll_input();
        if (PRESSED(b)) return;
        if (PRESSED(y) || ++frames >= 15 * 60) {
            frames = 0;
            if (!draw_cloud()) return;
        }
    }
}
