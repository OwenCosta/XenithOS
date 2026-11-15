#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "apps.h"
#include "audio.h"
#include "config.h"
#include "icons.h"
#include "img.h"
#include "platform.h"
#include "theme.h"
#include "ui.h"

struct app apps[MAX_APPS];
int napps;

static int has_ext(const char *name, const char *ext)
{
    const char *dot = strrchr(name, '.');
    return dot && !strcasecmp(dot, ext);
}

static void read_meta(struct app *a, const char *dir, const char *base)
{
    char path[160], line[96];
    FILE *f;
    snprintf(path, sizeof path, "%s%s.txt", dir, base);
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        if (!strncmp(line, "name=", 5) && line[5]) snprintf(a->name, sizeof a->name, "%s", line + 5);
        else if (!strncmp(line, "version=", 8)) snprintf(a->version, sizeof a->version, "%s", line + 8);
    }
    fclose(f);
}

static void scan_dir(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *de;
    if (!d) return;
    while ((de = readdir(d)) && napps < MAX_APPS) {
        struct app *a;
        char base[40], tp[160];
        int i, dup = 0;
        if (de->d_type & DT_DIR) continue;
        if (!has_ext(de->d_name, ".elf") && !has_ext(de->d_name, ".elf32")) continue;
        if (!strcasecmp(de->d_name, "xenon.elf")) continue;           /* that's XenithOS itself */
        snprintf(base, sizeof base, "%s", de->d_name);
        *strrchr(base, '.') = 0;
        for (i = 0; i < napps; i++) if (!strcasecmp(apps[i].id, base)) dup = 1;
        if (dup) continue;

        a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->path, sizeof a->path, "%s%s", dir, de->d_name);
        snprintf(a->id, sizeof a->id, "%s", base);
        for (i = 0; base[i] && i < (int)sizeof a->name - 1; i++) a->name[i] = toupper((unsigned char)base[i]);
        read_meta(a, dir, base);
        snprintf(tp, sizeof tp, "%s%s.ximg", dir, base);
        img_load_file(&a->thumb, tp);
    }
    closedir(d);
}

void apps_scan(void)
{
    const char *name;
    char root[48], sub[56];
    int h = -1, i;
    for (i = 0; i < napps; i++) img_free(&apps[i].thumb);
    napps = 0;
    while ((h = plat_bdev_next(h, &name)) >= 0) {
        snprintf(root, sizeof root, "%s:/", name);
        snprintf(sub, sizeof sub, "%sapps/", root);
        scan_dir(sub);
        scan_dir(root);
    }
}

void app_card(const struct app *a, int x, int y, int w, int h, int selected, float lift)
{
    int g = (int)(lift * 6);
    int cx = x - g, cy = y - g, cw = w + 2 * g, ch = h + 2 * g;
    if (selected) gfx_stroke_rrect(cx - 5.0f, cy - 5.0f, cw + 10.0f, ch + 10.0f, 17, 3, 0xFFFFFF, (int)(255 * lift));
    if (a->thumb.px) {
        gfx_image_rounded(&a->thumb, cx, cy, cw, ch, 12, 255);
    } else {
        gfx_fill_rrect((float)cx, (float)cy, (float)cw, (float)ch, 12, C_SURFACE2, 255);
        icon_draw(ICON_PLAY, cx + cw / 2.0f, cy + ch / 2.0f - 10, ch * 0.30f, C_SUB, C_SURFACE2);
        gfx_str_c(FONT_BODY_SB, cx + cw / 2, cy + ch - 38, C_TEXT, a->name);
    }
}

void app_launch(const struct app *a)
{
    uint64_t start = ui_ms();
    int tw = SCR_W / 3, th = tw * 9 / 16;
    char line[80];
    snprintf(st.last_app, sizeof st.last_app, "%s", a->path);
    settings_save();
    sfx(SFX_LAUNCH);
    snprintf(line, sizeof line, "Starting %s", a->name);
    while (ui_ms() - start < 700) {
        float t = (ui_ms() - start) / 700.0f;
        int al = (int)(255 * (t < 0.3f ? t / 0.3f : 1.0f));
        ui_poll();
        ui_bg();
        if (a->thumb.px) gfx_image_rounded(&a->thumb, (SCR_W - tw) / 2, SCR_H / 2 - th / 2 - 50, tw, th, 16, al);
        gfx_str_c(FONT_LABEL, SCR_W / 2, SCR_H / 2 + th / 2 - 20, C_TEXT, line);
        ui_spinner(SCR_W / 2, SCR_H / 2 + th / 2 + 50, 16, C_ACCENT);
        ui_end_frame();
    }
    plat_launch(a->path);
    sfx(SFX_ERROR);
    ui_message("Couldn't start the app", "The file may be damaged. Try installing it again from the Store.");
}
