#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include "avatar.h"
#include "config.h"
#include "net.h"

struct xenith_config cfg = {.name = "PLAYER"};
struct xenith_settings st = {80, 1, 1, 1, 0, "", 1};
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

/* ---------------------------------------------------------------- route.txt */

static FILE *open_write(const char *path);
static char route_for[32], route_lan[32], route_id[24];

static void on_route(const char *k, const char *v)
{
    if (!strcmp(k, "for")) snprintf(route_for, sizeof route_for, "%s", v);
    else if (!strcmp(k, "lan")) snprintf(route_lan, sizeof route_lan, "%s", v);
    else if (!strcmp(k, "id")) copy_alnum(route_id, sizeof route_id, v, 0);
}

static void route_use(const char *lan, const char *id)
{
    snprintf(cfg.server, sizeof cfg.server, "%s", lan);
    snprintf(cfg.route_id, sizeof cfg.route_id, "%s", id);
    net_set_lan_route(cfg.server, sizeof cfg.server, cfg.server_main);
}

void config_load(void)
{
    unsigned char ip[4];
    int port;
    cfg.loaded = read_kv(XENITH_CONFIG_PATH, on_config);
    snprintf(cfg.server_main, sizeof cfg.server_main, "%s", cfg.server);
    cfg.route_id[0] = route_for[0] = route_lan[0] = route_id[0] = 0;
    /* The dashboard checked this route when it came online (cloud_route_check), so the apps it
     * starts take it as it is. If the console has moved since, the first request finds out. */
    if (cfg.server[0] && read_kv(XENITH_ROUTE_PATH, on_route) && !strcmp(route_for, cfg.server_main)
            && route_id[0] && net_parse_host(route_lan, ip, &port))
        route_use(route_lan, route_id);
}

void route_save(const char *lan, const char *id)
{
    FILE *f;
    route_use(lan, id);
    f = open_write(XENITH_ROUTE_PATH);
    if (!f) return;
    fprintf(f, "# Written by XenithOS: XenithOS Cloud's address on this network.\nfor=%s\nlan=%s\nid=%s\n",
            cfg.server_main, cfg.server, cfg.route_id);
    fclose(f);
}

void route_forget(void)
{
    snprintf(cfg.server, sizeof cfg.server, "%s", cfg.server_main);
    cfg.route_id[0] = 0;
    net_set_lan_route(NULL, 0, NULL);
    remove(XENITH_ROUTE_PATH);
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
    else if (!strcmp(k, "cloud_saves")) st.cloud_saves = atoi(v) != 0;
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
    fprintf(f, "# Written by XenithOS\nvolume=%d\nui_sounds=%d\nboot_sound=%d\nauto_connect=%d\nlast_inbox=%d\nlast_app=%s\n"
               "cloud_saves=%d\n",
            st.volume, st.ui_sounds, st.boot_sound, st.auto_connect, st.last_inbox, st.last_app, st.cloud_saves);
    fclose(f);
}

/* ------------------------------------------------------------ profile.txt */

static void on_profile(const char *k, const char *v)
{
    if (!strcmp(k, "name")) snprintf(prof.name, sizeof prof.name, "%s", v);
    else if (!strcmp(k, "token")) copy_alnum(prof.token, sizeof prof.token, v, 0);
    else if (!strcmp(k, "avatar")) snprintf(prof.avatar, sizeof prof.avatar, "%s", v);
    else if (!strcmp(k, "joined")) snprintf(prof.joined, sizeof prof.joined, "%s", v);
    else if (!strcmp(k, "sharing")) prof.sharing = atoi(v) != 0;
}

void profile_load(void)
{
    memset(&prof, 0, sizeof prof);
    prof.sharing = 1;
    read_kv(XENITH_PROFILE_PATH, on_profile);
    if (!avatar_valid(prof.avatar)) strcpy(prof.avatar, AVATAR_DEFAULT);
    prof.signed_in = prof.name[0] && prof.token[0];
}

void profile_save(void)
{
    FILE *f = open_write(XENITH_PROFILE_PATH);
    if (!f) return;
    fprintf(f, "# Written by XenithOS. Keep this file private: the token signs you in.\n"
               "name=%s\ntoken=%s\navatar=%s\njoined=%s\nsharing=%d\n", prof.name, prof.token, prof.avatar, prof.joined,
            prof.sharing);
    fclose(f);
}

void profile_clear(void)
{
    memset(&prof, 0, sizeof prof);
    strcpy(prof.avatar, AVATAR_DEFAULT);
    remove(XENITH_PROFILE_PATH);
}
