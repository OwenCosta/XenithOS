#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include "avatar.h"
#include "config.h"

struct xenith_config cfg = {"", "PLAYER", "", 0};
struct xenith_settings st = {80, 1, 1, 1, 0, ""};
struct xenith_profile prof;

static void trim(char *s)
{
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
}

/* Calls fn(key, value) for each "key=value" line. Returns 0 if the file is missing. */
static int read_kv(const char *path, void (*fn)(const char *, const char *))
{
    char line[160];
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *key = line, *eq, *val;
        while (isspace((unsigned char)*key)) key++;
        if (*key == '#' || !*key) continue;
        eq = strchr(key, '=');
        if (!eq) continue;
        *eq = 0;
        val = eq + 1;
        trim(key);
        trim(val);
        while (isspace((unsigned char)*val)) val++;
        fn(key, val);
    }
    fclose(f);
    return 1;
}

static void copy_alnum(char *dst, int size, const char *src, int upper)
{
    int o = 0;
    for (; *src && o < size - 1; src++)
        if (isalnum((unsigned char)*src)) dst[o++] = upper ? toupper((unsigned char)*src) : *src;
    dst[o] = 0;
}

/* ------------------------------------------------------------- config.txt */

static void on_config(const char *k, const char *v)
{
    if (!strcmp(k, "server")) snprintf(cfg.server, sizeof cfg.server, "%s", v);
    else if (!strcmp(k, "key")) copy_alnum(cfg.key, sizeof cfg.key, v, 0);
    else if (!strcmp(k, "name")) {
        char n[16];
        copy_alnum(n, 13, v, 1);
        if (n[0]) strcpy(cfg.name, n);
    }
}

void config_load(void)
{
    cfg.loaded = read_kv(XENITH_CONFIG_PATH, on_config);
}

/* ----------------------------------------------------------- settings.txt */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void on_settings(const char *k, const char *v)
{
    if (!strcmp(k, "volume")) st.volume = clampi(atoi(v), 0, 100);
    else if (!strcmp(k, "ui_sounds")) st.ui_sounds = atoi(v) != 0;
    else if (!strcmp(k, "boot_sound")) st.boot_sound = atoi(v) != 0;
    else if (!strcmp(k, "auto_connect")) st.auto_connect = atoi(v) != 0;
    else if (!strcmp(k, "last_inbox")) st.last_inbox = atoi(v);
    else if (!strcmp(k, "last_app")) snprintf(st.last_app, sizeof st.last_app, "%s", v);
}

void settings_load(void)
{
    read_kv(XENITH_SETTINGS_PATH, on_settings);
}

static FILE *open_write(const char *path)
{
    mkdir(XENITH_DIR, 0777);
    return fopen(path, "w");
}

void settings_save(void)
{
    FILE *f = open_write(XENITH_SETTINGS_PATH);
    if (!f) return;
    fprintf(f, "# Written by XenithOS\nvolume=%d\nui_sounds=%d\nboot_sound=%d\nauto_connect=%d\nlast_inbox=%d\nlast_app=%s\n",
            st.volume, st.ui_sounds, st.boot_sound, st.auto_connect, st.last_inbox, st.last_app);
    fclose(f);
}

/* ------------------------------------------------------------ profile.txt */

static void on_profile(const char *k, const char *v)
{
    if (!strcmp(k, "name")) snprintf(prof.name, sizeof prof.name, "%s", v);
    else if (!strcmp(k, "token")) copy_alnum(prof.token, sizeof prof.token, v, 0);
    else if (!strcmp(k, "avatar")) snprintf(prof.avatar, sizeof prof.avatar, "%s", v);
    else if (!strcmp(k, "joined")) snprintf(prof.joined, sizeof prof.joined, "%s", v);
}

void profile_load(void)
{
    memset(&prof, 0, sizeof prof);
    read_kv(XENITH_PROFILE_PATH, on_profile);
    if (!avatar_valid(prof.avatar)) strcpy(prof.avatar, AVATAR_DEFAULT);
    prof.signed_in = prof.name[0] && prof.token[0];
}

void profile_save(void)
{
    FILE *f = open_write(XENITH_PROFILE_PATH);
    if (!f) return;
    fprintf(f, "# Written by XenithOS. Keep this file private: the token signs you in.\n"
               "name=%s\ntoken=%s\navatar=%s\njoined=%s\n", prof.name, prof.token, prof.avatar, prof.joined);
    fclose(f);
}

void profile_clear(void)
{
    memset(&prof, 0, sizeof prof);
    strcpy(prof.avatar, AVATAR_DEFAULT);
    remove(XENITH_PROFILE_PATH);
}
