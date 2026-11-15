#ifndef XENITH_APPS_H
#define XENITH_APPS_H

/* Installed apps: .elf / .elf32 files in the root or apps/ folder of each drive.
 * Next to an app, "<name>.ximg" is its thumbnail and "<name>.txt" can hold
 * "name=" and "version=" (the Store writes both when it installs something).
 * On PCs, "<name>.app" files are other programs too: apps from Flathub, games in Steam, or
 * your own (see plat_launch in platform.h). */
#include "gfx.h"

#define MAX_APPS 48

struct app {
    char path[128];
    char id[40];
    char name[40];
    char version[16];
    char ref[64];             /* PCs: its Flathub id ("tv.kodi.Kodi"), for removing it; else "" */
    int steam;                /* PCs: a game in Steam (removed in Steam) */
    struct img thumb;         /* thumb.px is NULL when there's no thumbnail */
};

extern struct app apps[MAX_APPS];
extern int napps;

void apps_scan(void);
void app_card(const struct app *a, int x, int y, int w, int h, int selected, float lift);
void app_launch(const struct app *a);   /* shows the launch screen; returns only if it failed */
int  app_is_other(const struct app *a); /* a ".app": a program that takes the whole screen (PCs) */

#endif
