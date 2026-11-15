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
#include "startlog.h"
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

/* "<id>.app" (PCs): another program, from Flathub, Steam or your own. Its name is inside. */
static void read_app_file(struct app *a)
{
    char line[96];
    FILE *f = fopen(a->path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        if (!strncmp(line, "name=", 5) && line[5]) snprintf(a->name, sizeof a->name, "%s", line + 5);
        else if (!strncmp(line, "steam=", 6)) snprintf(a->version, sizeof a->version, "Steam");
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
        int i, dup = 0, other = plat_kind() == PLAT_PC && has_ext(de->d_name, ".app");
        if (de->d_type & DT_DIR) continue;
        if (!other && !has_ext(de->d_name, ".elf") && !has_ext(de->d_name, ".elf32")) continue;
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
        if (other) read_app_file(a);
        else read_meta(a, dir, base);
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
    if (plat_kind() == PLAT_PC) plat_sync_apps();     /* the games installed in Steam */
    while ((h = plat_bdev_next(h, &name)) >= 0) {
        snprintf(root, sizeof root, "%s:/", name);
        snprintf(sub, sizeof sub, "%sapps/", root);
        scan_dir(sub);
        scan_dir(root);
    }
}

/* The card itself never changes size (so its scaled thumbnail stays cached); only the
 * selection ring closes in around it. */
void app_card(const struct app *a, int x, int y, int w, int h, int selected, float lift)
{
    float g = (1 - lift) * 8;
    if (selected) gfx_stroke_rrect(x - 5 - g, y - 5 - g, w + 10 + 2 * g, h + 10 + 2 * g, 17 + g, 3, 0xFFFFFF, (int)(255 * lift));
    if (a->thumb.px) {
        gfx_image_rounded(&a->thumb, x, y, w, h, 10, 255);
    } else {
        gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 12, C_SURFACE2, 255);
        icon_draw(ICON_PLAY, x + w / 2.0f, y + h / 2.0f - 10, h * 0.30f, C_SUB, C_SURFACE2);
        gfx_str_c(FONT_BODY_SB, x + w / 2, y + h - 38, C_TEXT, a->name);
    }
}

int app_is_other(const struct app *a)
{
    return has_ext(a->path, ".app");
}

void app_launch(const struct app *a)
{
    const char *why;
    char title[64];
    snprintf(st.last_app, sizeof st.last_app, "%s", a->path);
    settings_save();
    sfx(SFX_LAUNCH);
    startlog_launching(a->name);
    /* The apps open with a loading screen laid out like this one, and on the Xbox the screen
     * is kept across the switch, so going into an app doesn't show. Other programs (PCs)
     * take the whole screen, and holding Guide is the way back from them. */
    if (ui_launch(a->path, a->name, app_is_other(a) ? "Starting. Hold Guide for 3 seconds to come back to XenithOS." : "Starting"))
        return;                                       /* on a PC it ran as its own program and has quit */
    sfx(SFX_ERROR);
    why = plat_launch_error();
    snprintf(title, sizeof title, "Couldn't start %s", a->name);
    ui_message(title, why && *why ? why : "The file may be damaged. Try installing it again from the Store.");
}
