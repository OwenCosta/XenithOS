#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
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
        else if (!strncmp(line, "flatpak=", 8)) snprintf(a->ref, sizeof a->ref, "%s", line + 8);
        else if (!strncmp(line, "steam=", 6)) {
            snprintf(a->version, sizeof a->version, "Steam");
            a->steam = 1;
        }
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

/* The one played last first ("Jump back in"), then by name. */
static int app_order(const void *pa, const void *pb)
{
    const struct app *a = pa, *b = pb;
    int la = !strcmp(a->path, st.last_app), lb = !strcmp(b->path, st.last_app);
    if (la != lb) return lb - la;
    return strcasecmp(a->name, b->name);
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
    qsort(apps, (size_t)napps, sizeof apps[0], app_order);
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
        /* No picture (Prism Launcher's tile, your own apps): the name on a flat card like the
         * Store's, in a size that suits the card, on two lines at most, and never past its edges. */
        int font = w >= 200 ? FONT_LABEL : w >= 140 ? FONT_BODY_SB : FONT_SMALL;
        int pad = w >= 140 ? 18 : 9, lines, clip[4];
        gfx_fill_rrect((float)x, (float)y, (float)w, (float)h, 10, C_SURFACE2, 255);
        gfx_get_clip(clip);
        gfx_set_clip(0, 0, 0, 0);                    /* measure first: nothing drawn */
        lines = gfx_wrap(font, 0, 0, w - 2 * pad, 2, 0, a->name, 2);
        gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
        gfx_clip_intersect(x, y, w, h);
        gfx_wrap(font, x + pad, y + (h - lines * (gfx_line_h(font) + 2)) / 2, w - 2 * pad, 2, C_TEXT, a->name, 2);
        gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
    }
}

int app_is_other(const struct app *a)
{
    return has_ext(a->path, ".app");
}

static const struct app *app_by_path(const char *path)
{
    int i;
    for (i = 0; path && i < napps; i++)
        if (!strcmp(apps[i].path, path)) return &apps[i];
    return NULL;
}

static char closing_title[64];

static void closing_frame(void)
{
    ui_loading_frame(closing_title, NULL);
}

void app_launch(const struct app *a)
{
    const char *why, *running, *next;
    char title[64], body[160];
    while (a) {
        /* One game at a time: one the guide left running closes first (PCs). */
        running = plat_running();
        if (running && strcmp(running, a->path)) {
            const struct app *r = app_by_path(running);
            snprintf(title, sizeof title, "%s is still running", r ? r->name : "A game");
            snprintf(body, sizeof body, "Close it and start %s? Anything it hasn't saved is lost.", a->name);
            if (!ui_confirm(title, body, "Close it and start", "Cancel")) return;
            snprintf(closing_title, sizeof closing_title, "Closing %s", r ? r->name : "the game");
            plat_close_running(closing_frame);
            running = NULL;
        }
        snprintf(st.last_app, sizeof st.last_app, "%s", a->path);
        settings_save();
        sfx(SFX_LAUNCH);
        startlog_launching(a->name);
        /* The apps open with a loading screen laid out like this one, and on the Xbox the screen
         * is kept across the switch, so going into an app doesn't show. Other programs (PCs)
         * take the whole screen: the guide (the Xbox button, or Esc held) is the way back,
         * or, where there's no guide, holding the Xbox button. */
        if (!ui_launch(a->path, a->name, running ? "Going back" :
                       !app_is_other(a) ? "Starting" :
                       plat_has_guide() ? "Starting. Press the Xbox button or hold Esc for the guide." :
                       "Starting. Hold the Xbox button for 3 seconds to come back to XenithOS.")) {
            sfx(SFX_ERROR);
            why = plat_launch_error();
            snprintf(title, sizeof title, "Couldn't start %s", a->name);
            ui_message(title, why && *why ? why : "The file may be damaged. Try installing it again from the Store.");
            return;
        }
        /* On a PC it ran as its own program and has quit, or was left running. Switch game in
         * the guide chose what's next. */
        next = plat_next_launch();
        a = next ? app_by_path(next) : NULL;
    }
}
