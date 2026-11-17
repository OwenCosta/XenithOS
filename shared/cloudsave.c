/* Cloud saves: which files follow a profile, and where they're kept. See cloudsave.h. */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cloudsave.h"
#include "config.h"

/* DOOM keeps its settings (default.cfg) and its six save slots in its config folder, the saves
 * in .savegame/ (doomgeneric's m_config.c). Its other config file holds things about the
 * machine, so it stays. */
static const struct cloud_file doom_files[] = {
    {"default.cfg", "default.cfg", 0, "settings"},
    {"doomsav0.dsg", ".savegame/doomsav0.dsg", 1, "save slot 1"},
    {"doomsav1.dsg", ".savegame/doomsav1.dsg", 1, "save slot 2"},
    {"doomsav2.dsg", ".savegame/doomsav2.dsg", 1, "save slot 3"},
    {"doomsav3.dsg", ".savegame/doomsav3.dsg", 1, "save slot 4"},
    {"doomsav4.dsg", ".savegame/doomsav4.dsg", 1, "save slot 5"},
    {"doomsav5.dsg", ".savegame/doomsav5.dsg", 1, "save slot 6"},
};

/* XenithOS's settings that are about the person rather than the machine: source/cloud_saves.c
 * writes them to personal.txt from settings.txt, and back. */
static const struct cloud_file xenithos_files[] = {
    {"settings.txt", "personal.txt", 0, "settings"},
};

const struct cloud_app cloud_apps[] = {
    {"xenithos", "XenithOS", "uda:/xenith/", xenithos_files, 1},
    {"doom", "DOOM", "uda:/doom/", doom_files, (int)(sizeof doom_files / sizeof doom_files[0])},
};
const int ncloud_apps = (int)(sizeof cloud_apps / sizeof cloud_apps[0]);

const struct cloud_app *cloud_app_find(const char *id)
{
    int i;
    for (i = 0; id && i < ncloud_apps; i++)
        if (!strcmp(cloud_apps[i].id, id)) return &cloud_apps[i];
    return NULL;
}

void profile_folder(const char *dir, char *out, int n)
{
    char key[24];
    int i;
    if (!prof.signed_in || !prof.name[0]) { snprintf(out, n, "%s", dir); return; }
    for (i = 0; prof.name[i] && i < (int)sizeof key - 1; i++)       /* "Bob Two" -> "bob_two" */
        key[i] = prof.name[i] == ' ' ? '_' : (char)tolower((unsigned char)prof.name[i]);
    key[i] = 0;
    snprintf(out, n, "%sprofiles/%s/", dir, key);
}

void make_folders(const char *path)
{
    char dir[160], *p;
    snprintf(dir, sizeof dir, "%s", path);
    for (p = strchr(dir, '/'); p; p = strchr(p + 1, '/')) {
        if (p > dir && p[-1] == ':') continue;      /* "uda:/" is the drive itself */
        *p = 0;
        mkdir(dir, 0777);
        *p = '/';
    }
}

static int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb"), *out;
    char buf[4096];
    size_t n;
    if (!in) return 0;
    make_folders(to);
    out = fopen(to, "wb");
    if (!out) { fclose(in); return 0; }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 1;
}

int profile_folder_start(const struct cloud_app *a)
{
    char dir[96], from[160], to[200];
    DIR *d;
    int i;
    profile_folder(a->dir, dir, sizeof dir);
    if (!strcmp(dir, a->dir)) return 0;              /* a guest uses the app's folder itself */
    d = opendir(dir);
    if (d) { closedir(d); return 0; }
    make_folders(dir);
    for (i = 0; i < a->nfiles; i++) {
        snprintf(from, sizeof from, "%s%s", a->dir, a->files[i].path);
        snprintf(to, sizeof to, "%s%s", dir, a->files[i].path);
        copy_file(from, to);
    }
    return 1;
}
