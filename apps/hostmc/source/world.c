/* The world: backups (tar.gz files in hostmc/backups), restoring one, and starting afresh.
 *
 * A world is the level-name folder, plus Paper's separate folders for the Nether and the End.
 * While the server runs, a backup turns its autosave off and has it write everything first
 * (save-off, save-all flush), so the files don't change under tar, then turns autosave back on. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hostmc.h"
#include "ui.h"

#define MAX_WORLD_DIRS 3

static void level_name(char *out, int n)
{
    if (!props_get("level-name", out, n) || !out[0] || strchr(out, '/') || !strcmp(out, ".") || !strcmp(out, ".."))
        snprintf(out, n, "world");
}

/* The world's folders that exist, relative to the server folder. */
static int world_dirs(char dirs[MAX_WORLD_DIRS][80])
{
    static const char *const suffix[MAX_WORLD_DIRS] = {"", "_nether", "_the_end"};
    char level[64], path[200];
    int i, n = 0;
    struct stat s;
    level_name(level, sizeof level);
    for (i = 0; i < MAX_WORLD_DIRS; i++) {
        snprintf(dirs[n], sizeof dirs[n], "%s%s", level, suffix[i]);
        snprintf(path, sizeof path, HM_SERVER "/%s", dirs[n]);
        if (stat(path, &s) == 0 && S_ISDIR(s.st_mode)) n++;
    }
    return n;
}

int world_exists(void)
{
    char dirs[MAX_WORLD_DIRS][80];
    return world_dirs(dirs) > 0;
}

static int by_name_desc(const void *a, const void *b)
{
    return strcmp((const char *)b, (const char *)a);
}

void world_backups(struct backup_list *out)
{
    DIR *d = opendir(HM_BACKUPS);
    struct dirent *de;
    char names[200][48];
    int n = 0, i;
    memset(out, 0, sizeof *out);
    if (!d) return;
    while ((de = readdir(d)) && n < 200) {
        size_t l = strlen(de->d_name);
        if (l < 8 || l >= 48 || strcmp(de->d_name + l - 7, ".tar.gz")) continue;
        snprintf(names[n++], sizeof names[0], "%s", de->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], by_name_desc);    /* names are dates: newest first */
    for (i = 0; i < n && out->n < MAX_BACKUPS; i++) {
        char path[120];
        snprintf(out->name[out->n], sizeof out->name[0], "%s", names[i]);
        snprintf(path, sizeof path, HM_BACKUPS "/%s", names[i]);
        out->size[out->n++] = file_size(path);
    }
}

static int prune_held;                 /* a restore is using the oldest backup, maybe */

/* Keep the newest MAX_BACKUPS. */
static void prune(void)
{
    if (prune_held) return;
    DIR *d = opendir(HM_BACKUPS);
    struct dirent *de;
    char names[200][48];
    int n = 0, i;
    if (!d) return;
    while ((de = readdir(d)) && n < 200) {
        size_t l = strlen(de->d_name);
        if (l < 8 || l >= 48 || strcmp(de->d_name + l - 7, ".tar.gz")) continue;
        snprintf(names[n++], sizeof names[0], "%s", de->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], by_name_desc);
    for (i = MAX_BACKUPS; i < n; i++) {
        char path[120];
        snprintf(path, sizeof path, HM_BACKUPS "/%s", names[i]);
        unlink(path);
        snprintf(path + strlen(path) - 7, 8, ".txt");
        unlink(path);
    }
}

int world_backup(const char *why_made, char *why, int n)
{
    char dirs[MAX_WORLD_DIRS][80], name[48], path[120], part[130], note[160];
    const char *argv[8];
    time_t now = time(NULL);
    struct tm tm;
    int nd = world_dirs(dirs), i, code, live = sv.state == SV_RUNNING;
    if (!nd) {
        snprintf(why, n, "There's no world to back up yet. One is made the first time the server starts.");
        return 0;
    }
    mkdirs(HM_BACKUPS);
    localtime_r(&now, &tm);
    strftime(name, sizeof name, "%Y-%m-%d_%H-%M-%S", &tm);
    snprintf(path, sizeof path, HM_BACKUPS "/%s.tar.gz", name);
    for (i = 2; file_exists(path) && i < 100; i++) snprintf(path, sizeof path, HM_BACKUPS "/%s-%d.tar.gz", name, i);
    snprintf(part, sizeof part, "%s.part", path);
    if (live) {
        unsigned before = sv.saves;
        sv_command("save-off");
        sv_command("save-all flush");
        if (!sv_wait_saved(before, 90)) {
            sv_command("save-on");
            snprintf(why, n, "The server didn't finish saving the world, so no backup was made. Try again.");
            return 0;
        }
    }
    argv[0] = "tar";
    argv[1] = "-czf";
    argv[2] = part;
    argv[3] = "-C";
    argv[4] = HM_SERVER;
    for (i = 0; i < nd; i++) argv[5 + i] = dirs[i];
    argv[5 + nd] = NULL;
    code = job_run(argv, NULL, "Backing up the world", why_made, NULL, 0, 0);
    if (live) sv_command("save-on");
    /* tar says 1 when a file changed as it read it, which the save-off above prevents. */
    if ((code != 0 && code != 1) || rename(part, path) != 0) {
        unlink(part);
        snprintf(why, n, "The backup couldn't be written. Is the disk full?");
        return 0;
    }
    snprintf(note, sizeof note, "%s\n", why_made);
    snprintf(path + strlen(path) - 7, 8, ".txt");
    file_write(path, note, (long)strlen(note), 0644);
    prune();
    return 1;
}

/* Deletes the world's folders. */
static int remove_world(void)
{
    char dirs[MAX_WORLD_DIRS][80], paths[MAX_WORLD_DIRS][120];
    const char *argv[MAX_WORLD_DIRS + 3];
    int nd = world_dirs(dirs), i;
    if (!nd) return 1;
    argv[0] = "rm";
    argv[1] = "-rf";
    for (i = 0; i < nd; i++) {
        snprintf(paths[i], sizeof paths[i], HM_SERVER "/%s", dirs[i]);
        argv[2 + i] = paths[i];
    }
    argv[2 + nd] = NULL;
    return job_run(argv, NULL, "Removing the world", NULL, NULL, 0, 0) == 0;
}

int world_reset(char *why, int n)
{
    if (sv.state != SV_STOPPED) {
        snprintf(why, n, "Stop the server first.");
        return 0;
    }
    if (world_exists() && !world_backup("Before a new world was started", why, n)) return 0;
    if (!remove_world()) {
        snprintf(why, n, "The world couldn't be removed.");
        return 0;
    }
    return 1;
}

int world_restore(const char *name, char *why, int n)
{
    char path[120], when[64];
    const char *argv[] = {"tar", "-xzf", path, "-C", HM_SERVER, NULL};
    if (sv.state != SV_STOPPED) {
        snprintf(why, n, "Stop the server first.");
        return 0;
    }
    snprintf(path, sizeof path, HM_BACKUPS "/%s", name);
    if (!file_exists(path)) {
        snprintf(why, n, "That backup is gone.");
        return 0;
    }
    snprintf(when, sizeof when, "Before the backup from %.10s was put back", name);
    prune_held = 1;                         /* the one being put back may be the oldest */
    if (world_exists() && !world_backup(when, why, n)) { prune_held = 0; return 0; }
    if (!remove_world()) {
        prune_held = 0;
        snprintf(why, n, "The world couldn't be removed.");
        return 0;
    }
    if (job_run(argv, NULL, "Putting the backup back", NULL, NULL, 0, 0) != 0) {
        prune_held = 0;
        snprintf(why, n, "The backup didn't unpack. The world from before is in the newest backup.");
        return 0;
    }
    prune_held = 0;
    prune();
    return 1;
}
