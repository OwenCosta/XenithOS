/* PHOTOS: your pictures on the TV.
 *
 * It looks for JPEG, PNG, BMP and GIF files in a "photos" or "pictures" folder on every drive
 * (two folders deep), and on a PC in ~/Pictures too, and shows them as a grid. A opens one
 * full screen: left and right (or LB and RB) go through them, A or the triggers zoom, the
 * left stick moves around a zoomed picture, Y turns it, X starts a slideshow.
 *
 * Thumbnails are made as the grid shows them, one a frame so it never stops moving, and kept
 * in uda:/xenith/thumbs so the next visit is instant. Only the rows near the screen are kept
 * in memory.
 */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "audio.h"
#include "config.h"
#include "gfx.h"
#include "handoff.h"
#include "icons.h"
#include "img.h"
#include "picture.h"
#include "platform.h"
#include "startlog.h"
#include "theme.h"
#include "ui.h"

#define MAX_PHOTOS 600
#define COLS 4
#define GAP 18
#define WORK_W 2560                  /* a picture is kept at most this big, enough to zoom into */
#define WORK_H 1440
#define XBOX_MAX_PIXELS (24 * 1000 * 1000)   /* bigger than that doesn't fit in the Xbox's memory */

struct photo {
    char path[200];
    char name[64];
    long size;
    struct img thumb;
    int tried;                       /* making its thumbnail was attempted (it may not be a picture) */
};

static struct photo *photos;
static int nphotos;

/* ------------------------------------------------------------------ finding */

static int is_picture(const char *name)
{
    static const char *const exts[] = {".jpg", ".jpeg", ".png", ".bmp", ".gif"};
    const char *dot = strrchr(name, '.');
    int i;
    if (!dot) return 0;
    for (i = 0; i < (int)(sizeof exts / sizeof exts[0]); i++)
        if (!strcasecmp(dot, exts[i])) return 1;
    return 0;
}

static int is_dir(const char *path, const struct dirent *de)
{
    struct stat s;
#ifdef DT_DIR
    if (de->d_type == DT_DIR) return 1;
    if (de->d_type == DT_REG) return 0;
#endif
    return stat(path, &s) == 0 && S_ISDIR(s.st_mode);
}

static void scan(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *de;
    if (!d) return;
    while ((de = readdir(d)) && nphotos < MAX_PHOTOS) {
        char path[200];
        struct stat s;
        if (de->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s%s%s", dir, dir[strlen(dir) - 1] == '/' ? "" : "/", de->d_name);
        if (is_dir(path, de)) {
            if (depth < 2) scan(path, depth + 1);
            continue;
        }
        if (!is_picture(de->d_name) || stat(path, &s) != 0 || s.st_size <= 0) continue;
        memset(&photos[nphotos], 0, sizeof photos[nphotos]);
        snprintf(photos[nphotos].path, sizeof photos[nphotos].path, "%s", path);
        snprintf(photos[nphotos].name, sizeof photos[nphotos].name, "%s", de->d_name);
        photos[nphotos].size = (long)s.st_size;
        nphotos++;
    }
    closedir(d);
}

static int by_path(const void *a, const void *b)
{
    return strcasecmp(((const struct photo *)a)->path, ((const struct photo *)b)->path);
}

static void find_photos(void)
{
    static const char *const folders[] = {"photos", "Photos", "pictures", "Pictures"};
    const char *drive;
    char root[300];
    int h = -1, i, k;
    while ((h = plat_bdev_next(h, &drive)) >= 0)
        for (i = 0; i < 4; i++) {
            snprintf(root, sizeof root, "%s:/%s", drive, folders[i]);
            scan(root, 0);
        }
#ifdef XENITH_LINUX
    if (getenv("HOME")) {
        snprintf(root, sizeof root, "%s/Pictures", getenv("HOME"));
        scan(root, 0);
    }
#endif
    qsort(photos, nphotos, sizeof *photos, by_path);
    /* "photos" and "Photos" are the same folder on the USB stick's file system */
    for (i = 1, k = 1; i < nphotos; i++)
        if (strcasecmp(photos[i].path, photos[k - 1].path)) photos[k++] = photos[i];
    if (nphotos) nphotos = k;
}

/* ---------------------------------------------------------------- thumbnails */

static unsigned char *read_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64L * 1024 * 1024 || !(buf = malloc((size_t)n))) { fclose(f); return NULL; }
    if ((long)fread(buf, 1, (size_t)n, f) != n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    *len = n;
    return buf;
}

static int too_big(int w, int h)
{
    return plat_kind() == PLAT_XBOX && (long long)w * h > XBOX_MAX_PIXELS;
}

/* Where a thumbnail is kept: named after the picture's path and size. */
static void cache_path(const struct photo *p, int tw, int th, char *out, int n)
{
    uint32_t hsh = 2166136261u;
    const unsigned char *s;
    for (s = (const unsigned char *)p->path; *s; s++) hsh = (hsh ^ *s) * 16777619u;
    snprintf(out, n, "uda:/xenith/thumbs/%08x-%lx-%dx%d.ximg", (unsigned)hsh, (unsigned long)p->size, tw, th);
}

static void make_thumb(struct photo *p, int tw, int th)
{
    char cache[96];
    unsigned char *data;
    long len;
    int w, h;
    p->tried = 1;
    cache_path(p, tw, th, cache, sizeof cache);
    if (img_load_file(&p->thumb, cache) && p->thumb.w == tw && p->thumb.h == th) return;
    img_free(&p->thumb);
    if (!(data = read_file(p->path, &len))) return;
    if (picture_info(data, (int)len, &w, &h) && !too_big(w, h)) {
        uint32_t *px = picture_decode(data, (int)len, &w, &h);
        if (px && picture_cover(&p->thumb, px, w, h, tw, th)) {
            mkdir("uda:/xenith/thumbs", 0777);
            img_save_file(&p->thumb, cache);
        }
        free(px);
    }
    free(data);
}

/* --------------------------------------------------------------- one photo */

static struct {
    uint32_t *px;                    /* the picture, at most WORK_W x WORK_H */
    int w, h;
    struct img shown;                /* what's on screen now */
    int ox, oy;                      /* where it's drawn */
    int zoom;                        /* 1, 2 or 4 */
    float cx, cy;                    /* the middle of what's shown, in picture pixels */
    int dirty;
    uint64_t drawn_at;
} v;

static void view_free(void)
{
    free(v.px);
    img_free(&v.shown);
    memset(&v, 0, sizeof v);
}

/* Decodes photo i into v. 0 with why[] set if it can't. */
static int view_open(int i, char *why, int why_len)
{
    unsigned char *data;
    long len;
    int w, h, ww, wh;
    uint32_t *px;
    view_free();
    ui_loading_frame("Opening", photos[i].name);
    if (!(data = read_file(photos[i].path, &len))) {
        snprintf(why, why_len, "It couldn't be read.");
        return 0;
    }
    if (!picture_info(data, (int)len, &w, &h)) {
        free(data);
        snprintf(why, why_len, "It isn't a picture Photos can open.");
        return 0;
    }
    if (too_big(w, h)) {
        free(data);
        snprintf(why, why_len, "It's %d x %d: too big for the Xbox's memory. Pictures up to 24 megapixels open here.", w, h);
        return 0;
    }
    px = picture_decode(data, (int)len, &w, &h);
    free(data);
    if (!px) {
        snprintf(why, why_len, "It's damaged, or a kind of %s Photos can't read.", strrchr(photos[i].name, '.') ? strrchr(photos[i].name, '.') + 1 : "picture");
        return 0;
    }
    /* Kept at most WORK_W x WORK_H: enough to zoom into, without filling the memory. */
    ww = w;
    wh = h;
    if (ww > WORK_W) { wh = (int)((long long)wh * WORK_W / ww); ww = WORK_W; }
    if (wh > WORK_H) { ww = (int)((long long)ww * WORK_H / wh); wh = WORK_H; }
    if (ww < 1) ww = 1;
    if (wh < 1) wh = 1;
    if (ww != w || wh != h) {
        struct img small;
        if (!picture_region(&small, px, w, h, 0, 0, w, h, ww, wh)) { free(px); snprintf(why, why_len, "There isn't enough memory to show it."); return 0; }
        free(px);
        px = small.px;                 /* keep the pixels, not the image */
        w = ww;
        h = wh;
    }
    v.px = px;
    v.w = w;
    v.h = h;
    v.zoom = 1;
    v.cx = w / 2.0f;
    v.cy = h / 2.0f;
    v.dirty = 1;
    return 1;
}

/* A quarter turn clockwise. */
static void view_turn(void)
{
    uint32_t *t = malloc(sizeof(uint32_t) * (size_t)v.w * v.h);
    int x, y;
    float cx = v.cx;
    if (!t) return;
    for (y = 0; y < v.h; y++)
        for (x = 0; x < v.w; x++)
            t[(size_t)x * v.h + (v.h - 1 - y)] = v.px[(size_t)y * v.w + x];
    free(v.px);
    v.px = t;
    x = v.w; v.w = v.h; v.h = x;
    v.cx = v.w - v.cy;
    v.cy = cx;
    v.dirty = 1;
}

/* Makes what's on screen from the picture, the zoom and the middle. */
static void view_render(void)
{
    float fit = (float)SCR_W / v.w, scale;
    int sw, sh, dw, dh, sx, sy;
    if ((float)SCR_H / v.h < fit) fit = (float)SCR_H / v.h;
    scale = fit * v.zoom;
    sw = (int)(SCR_W / scale);
    sh = (int)(SCR_H / scale);
    if (sw > v.w) sw = v.w;
    if (sh > v.h) sh = v.h;
    /* Keep the middle where the picture still fills what's shown. */
    if (v.cx < sw / 2.0f) v.cx = sw / 2.0f;
    if (v.cx > v.w - sw / 2.0f) v.cx = v.w - sw / 2.0f;
    if (v.cy < sh / 2.0f) v.cy = sh / 2.0f;
    if (v.cy > v.h - sh / 2.0f) v.cy = v.h - sh / 2.0f;
    sx = (int)(v.cx - sw / 2.0f);
    sy = (int)(v.cy - sh / 2.0f);
    dw = (int)(sw * scale + 0.5f);
    dh = (int)(sh * scale + 0.5f);
    if (dw > SCR_W) dw = SCR_W;
    if (dh > SCR_H) dh = SCR_H;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    img_free(&v.shown);
    picture_region(&v.shown, v.px, v.w, v.h, sx, sy, sw, sh, dw, dh);
    v.ox = (SCR_W - dw) / 2;
    v.oy = (SCR_H - dh) / 2;
    v.dirty = 0;
    v.drawn_at = ui_ms();
}

/* Full screen, from photo i (as a slideshow if asked). Returns the photo it ended on. */
static int viewer(int i, int slideshow)
{
    char why[160], label[120];
    uint64_t shown_at, slide_at = ui_ms();

    if (!view_open(i, why, sizeof why)) {
        char title[96];
        sfx(SFX_ERROR);
        snprintf(title, sizeof title, "Can't show %s", photos[i].name);
        ui_message(title, why);
        return i;
    }
    sfx(SFX_SELECT);
    shown_at = ui_ms();
    for (;;) {
        int step = 0, zoom = v.zoom, info;
        float mx, my;
        ui_poll();
        if (PRESSED(b)) { sfx(SFX_BACK); break; }
        if (PRESSED(x)) {
            slideshow = !slideshow;
            slide_at = ui_ms();
            sfx(SFX_SELECT);
            if (slideshow) ui_toast("Slideshow", "A new picture every 5 seconds. X stops it.");
        }
        if (PRESSED(right) || PRESSED(rb)) step = 1;
        if (PRESSED(left) || PRESSED(lb)) step = -1;
        if (slideshow && ui_ms() - slide_at > 5000) { step = 1; slide_at = ui_ms(); }
        if (step && nphotos > 1) {
            int next = (i + step + nphotos) % nphotos, tries = 0;
            /* Skip pictures that won't open (a slideshow shouldn't stop on one). */
            while (tries++ < nphotos && !view_open(next, why, sizeof why)) next = (next + step + nphotos) % nphotos;
            if (tries > nphotos) break;
            i = next;
            if (!slideshow) sfx(SFX_MOVE);
            shown_at = ui_ms();
            continue;
        }
        if (PRESSED(a)) zoom = zoom >= 4 ? 1 : zoom * 2;
        if (PRESSED(rt) && zoom < 4) zoom *= 2;
        if (PRESSED(lt) && zoom > 1) zoom /= 2;
        if (zoom != v.zoom) { v.zoom = zoom; v.dirty = 1; sfx(SFX_MOVE); }
        if (PRESSED(y)) { view_turn(); sfx(SFX_MOVE); }
        /* The left stick moves around a zoomed picture. */
        mx = cur.lx / 32768.0f;
        my = -cur.ly / 32768.0f;
        if (v.zoom > 1 && (mx * mx + my * my) > 0.04f) {
            float speed = 14.0f / v.zoom * ((float)v.w / SCR_W + 0.5f);
            v.cx += mx * speed;
            v.cy += my * speed;
            if (ui_ms() - v.drawn_at > 50) v.dirty = 1;
        }
        if (v.dirty) view_render();

        gfx_clear(0x000000);
        if (v.shown.px) gfx_image(&v.shown, v.ox, v.oy, v.shown.w, v.shown.h, 255);
        info = ui_ms() - shown_at < 3000 && !slideshow;
        if (info) {
            gfx_rect_a(0, SCR_H - 96, SCR_W, 96, 0x000000, 170);
            snprintf(label, sizeof label, "%s", photos[i].name);
            gfx_str_fit(FONT_BODY_SB, M, SCR_H - 86, C_TEXT, label, SCR_W - 2 * M - 160);
            snprintf(label, sizeof label, "%d of %d", i + 1, nphotos);
            gfx_str_r(FONT_SMALL, SCR_W - M, SCR_H - 84, C_SUB, label);
            ui_hints("A=Zoom;Y=Turn;X=Slideshow;B=Back");
        }
        if (v.zoom > 1) {
            snprintf(label, sizeof label, "%dx", v.zoom);
            gfx_fill_rrect(SCR_W - M - 56.0f, 24, 56, 34, 17, 0x000000, 160);
            gfx_str_c(FONT_SMALL, SCR_W - M - 28, 30, C_TEXT, label);
        }
        ui_end_frame();
    }
    view_free();
    return i;
}

/* ------------------------------------------------------------------- grid */

static void back_to_xenithos(void)
{
    ui_back_to_xenithos("photos");
}

int main(int argc, char **argv)
{
    int sel = 0, first_row = 0, i;
    float lift[MAX_PHOTOS];
    handoff_args(argc, argv);
    startlog_mark("start");
    plat_init();
    gfx_init();
    ui_init();
    ui_loading_frame("Photos", "Loading");
    plat_storage_init();
    config_load();
    settings_load();
    profile_load();
    audio_init();
    audio_set_volume(st.volume);
    audio_set_ui_sounds(st.ui_sounds);
    startlog_mark("sounds");

    photos = calloc(MAX_PHOTOS, sizeof *photos);
    if (!photos) { ui_message("Photos can't start", "There isn't enough memory."); back_to_xenithos(); return 0; }
    ui_loading_frame("Photos", "Looking for pictures");
    find_photos();
    memset(lift, 0, sizeof lift);
    ui_fade_in();

    for (;;) {
        int cw = (SCR_W - 2 * M - GAP * (COLS - 1)) / COLS, ch = cw * 9 / 16, rowh = ch + GAP;
        int top = HEADER_H + 16, rows = (SCR_H - top - 70) / rowh, dx, dy, made = 0;
        char sub[160];
        if (rows < 1) rows = 1;

        ui_poll();
        dx = ui_nav_x();
        dy = ui_nav_y();
        if (nphotos) {
            int n = sel + dx + dy * COLS;
            if (dy && n >= nphotos && sel / COLS < (nphotos - 1) / COLS) n = nphotos - 1;   /* down to a short last row */
            if ((dx || dy) && n >= 0 && n < nphotos && !(dx && n / COLS != sel / COLS)) { sel = n; sfx(SFX_MOVE); }
        }
        if (PRESSED(b)) { sfx(SFX_BACK); back_to_xenithos(); return 0; }
        if (PRESSED(y)) {
            sfx(SFX_SELECT);
            for (i = 0; i < nphotos; i++) img_free(&photos[i].thumb);
            nphotos = 0;
            ui_loading_frame("Photos", "Looking for pictures");
            find_photos();
            memset(lift, 0, sizeof lift);
            sel = first_row = 0;
        }
        if (nphotos && (PRESSED(a) || PRESSED(x))) {
            sel = viewer(sel, PRESSED(x));
            ui_settle_input();
        }

        if (sel / COLS < first_row) first_row = sel / COLS;
        if (sel / COLS >= first_row + rows) first_row = sel / COLS - rows + 1;

        /* Thumbnails: the next missing one on screen (one a frame), and none far off it. */
        for (i = 0; i < nphotos; i++) {
            int r = i / COLS;
            if (r < first_row - 2 || r > first_row + rows + 1) { if (photos[i].thumb.px) img_free(&photos[i].thumb); photos[i].tried = 0; }
            else if (!photos[i].tried && !made && r >= first_row && r < first_row + rows) { make_thumb(&photos[i], cw, ch); made = 1; }
        }

        ui_bg();
        if (nphotos) snprintf(sub, sizeof sub, "%s  -  %d of %d", photos[sel].name, sel + 1, nphotos);
        else snprintf(sub, sizeof sub, "Your pictures");
        ui_header("Photos", sub);
        if (!nphotos) {
            icon_draw(ICON_APPS, SCR_W / 2.0f, SCR_H / 2.0f - 40, 70, C_DIM, C_BG);
            gfx_str_c(FONT_LABEL, SCR_W / 2, SCR_H / 2 + 20, C_TEXT, "No photos yet");
            gfx_str_c(FONT_BODY, SCR_W / 2, SCR_H / 2 + 58, C_SUB,
                      plat_kind() == PLAT_PC ? "Put pictures in your Pictures folder, or a folder called photos in XenithOS's data."
                                             : "Put JPEG or PNG pictures in a folder called photos on your USB stick.");
        }
        for (i = first_row * COLS; i < nphotos && i < (first_row + rows) * COLS; i++) {
            int r = i / COLS - first_row, c = i % COLS, x = M + c * (cw + GAP), y = top + r * rowh;
            float g;
            lift[i] = ui_ease(lift[i], i == sel ? 1.0f : 0.0f, 0.35f);
            g = (1 - lift[i]) * 8;
            if (i == sel) gfx_stroke_rrect(x - 5 - g, y - 5 - g, cw + 10 + 2 * g, ch + 10 + 2 * g, 15 + g, 3, 0xFFFFFF, (int)(255 * lift[i]));
            if (photos[i].thumb.px) gfx_image_rounded(&photos[i].thumb, x, y, cw, ch, 10, 255);
            else {
                gfx_fill_rrect((float)x, (float)y, (float)cw, (float)ch, 10, C_SURFACE2, 255);
                if (!photos[i].tried) ui_spinner(x + cw / 2, y + ch / 2, 14, C_DIM);
                else gfx_str_c(FONT_SMALL, x + cw / 2, y + ch / 2 - 10, C_DIM, "Can't open");
            }
        }
        ui_hints(nphotos ? "A=View;X=Slideshow;Y=Look again;B=Back" : "Y=Look again;B=Back");
        ui_end_frame();
    }
}
