#ifndef XENITH_CLOUDSAVE_H
#define XENITH_CLOUDSAVE_H

/* Cloud saves: the files that follow a profile between machines through XenithOS Cloud, and
 * where each machine keeps them. Every profile has its own folder for an app's files
 * (profile_folder), so two people sharing a console never mix saves, and XenithOS keeps that
 * folder in step with the profile's cloud saves before an app starts and after it closes
 * (source/cloud_saves.c). A guest keeps using the app's folder itself. */

struct cloud_file {
    const char *name;            /* on XenithOS Cloud */
    const char *path;            /* in the profile's folder for the app */
    int save;                    /* a save (asked about when it changed in two places), or settings */
    const char *title;           /* after the app's name: "save slot 1", "settings" */
};

struct cloud_app {
    const char *id, *title;      /* "doom", "DOOM" */
    const char *dir;             /* the app's folder on the stick, which a guest uses: "uda:/doom/" */
    const struct cloud_file *files;
    int nfiles;
};

extern const struct cloud_app cloud_apps[];
extern const int ncloud_apps;
const struct cloud_app *cloud_app_find(const char *id);   /* NULL if it has no cloud saves */

/* The app's folder for the signed-in profile: <dir>profiles/<gamertag>/, or dir for a guest. */
void profile_folder(const char *dir, char *out, int n);
/* The first time a profile uses an app's folder on this machine, the folder starts as a copy
 * of the app's files at dir (made before profiles had their own), so nobody loses a save.
 * Returns 1 if the folder was made now. */
int  profile_folder_start(const struct cloud_app *a);
void make_folders(const char *path);           /* every folder up to path's last '/' */

#endif
