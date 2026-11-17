#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include "avatar.h"
#include "config.h"
#include "gfx.h"
#include "net.h"
#include "platform.h"
#include "theme.h"
#include "ui.h"

struct xenith_config cfg = {.name = "PLAYER"};
/* By name, so a setting added to the struct can't take another's default: crash reports start at
 * "Ask first" (0), cloud saves on (1). The look starts as XenithOS always looked: its own theme,
 * no wallpaper, normal text, no high contrast, every button doing its own job (0 in pad_map). */
struct xenith_settings st = {
    .volume = 80, .ui_sounds = 1, .boot_sound = 1, .auto_connect = 1, .last_inbox = 0, .last_app = "",
    .crash_reports = 0, .cloud_saves = 1,
    .theme = "", .wallpaper = "", .text_large = 0, .high_contrast = 0, .pad_map = {0}, .swap_sticks = 0,
};
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
static char route_for[NET_ADDR_LEN], route_lan[32], route_id[24], route_https[NET_ADDR_LEN];
static char lan_now[32];          /* the home-network address in use */

static void on_route(const char *k, const char *v)
{
    if (!strcmp(k, "for")) snprintf(route_for, sizeof route_for, "%s", v);
    else if (!strcmp(k, "lan")) snprintf(route_lan, sizeof route_lan, "%s", v);
    else if (!strcmp(k, "id")) copy_alnum(route_id, sizeof route_id, v, 0);
    else if (!strcmp(k, "https")) snprintf(route_https, sizeof route_https, "%s", v);
}

/* Where requests go away from the network XenithOS Cloud runs on. */
static const char *away(void)
{
    return cfg.server_https[0] ? cfg.server_https : cfg.server_main;
}

static void route_use(const char *lan, const char *id)
{
    snprintf(lan_now, sizeof lan_now, "%s", lan);
    snprintf(cfg.server, sizeof cfg.server, "%s", lan);
    snprintf(cfg.route_id, sizeof cfg.route_id, "%s", id);
    net_set_lan_route(cfg.server, sizeof cfg.server, away());
}

static void https_use(const char *addr)       /* "" for none */
{
    snprintf(cfg.server_https, sizeof cfg.server_https, "%s", addr);
    if (net_lan_route_on()) net_set_lan_route(cfg.server, sizeof cfg.server, away());   /* at home: the way out */
    else snprintf(cfg.server, sizeof cfg.server, "%s", away());
}

/* route.txt says what's in use, and goes when nothing is. */
static void route_write(void)
{
    FILE *f;
    if (!cfg.route_id[0] && !cfg.server_https[0]) {
        remove(XENITH_ROUTE_PATH);
        return;
    }
    f = open_write(XENITH_ROUTE_PATH);
    if (!f) return;
    fprintf(f, "# Written by XenithOS each time it starts: other ways to reach XenithOS Cloud from here.\n"
               "for=%s\nlan=%s\nid=%s\nhttps=%s\n",
            cfg.server_main, cfg.route_id[0] ? lan_now : "", cfg.route_id, cfg.server_https);
    fclose(f);
}

void config_load(void)
{
    unsigned char ip[4];
    int port;
    cfg.loaded = read_kv(XENITH_CONFIG_PATH, on_config);
    snprintf(cfg.server_main, sizeof cfg.server_main, "%s", cfg.server);
    cfg.route_id[0] = cfg.server_https[0] = lan_now[0] = 0;
    route_for[0] = route_lan[0] = route_id[0] = route_https[0] = 0;
    net_set_lan_route(NULL, 0, NULL);
    /* The dashboard checked these when it came online (cloud_route_check), so the apps it starts
     * take them as they are. If the machine has moved since, the first request finds out. */
    if (!cfg.server[0] || !read_kv(XENITH_ROUTE_PATH, on_route) || strcmp(route_for, cfg.server_main)) return;
    if (plat_kind() == PLAT_PC && !strncmp(route_https, "https://", 8)) https_use(route_https);
    if (route_id[0] && net_parse_host(route_lan, ip, &port)) route_use(route_lan, route_id);
}

void route_save(const char *lan, const char *id)
{
    route_use(lan, id);
    route_write();
}

void route_forget(void)
{
    cfg.route_id[0] = lan_now[0] = 0;
    net_set_lan_route(NULL, 0, NULL);
    snprintf(cfg.server, sizeof cfg.server, "%s", away());
    route_write();
}

void https_save(const char *addr)
{
    https_use(addr);
    route_write();
}

void https_forget(void)
{
    if (!cfg.server_https[0]) return;
    https_use("");
    route_write();
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
    else if (!strcmp(k, "crash_reports")) st.crash_reports = atoi(v) != 0;
    else if (!strcmp(k, "cloud_saves")) st.cloud_saves = atoi(v) != 0;
    else if (!strcmp(k, "theme")) copy_alnum(st.theme, sizeof st.theme, v, 0);
    else if (!strcmp(k, "wallpaper")) copy_alnum(st.wallpaper, sizeof st.wallpaper, v, 0);
    else if (!strcmp(k, "text_large")) st.text_large = atoi(v) != 0;
    else if (!strcmp(k, "high_contrast")) st.high_contrast = atoi(v) != 0;
    else if (!strcmp(k, "buttons")) ui_pad_map_parse(v, st.pad_map);
    else if (!strcmp(k, "swap_sticks")) st.swap_sticks = atoi(v) != 0;
}

/* The look and the controller as the settings have them (theme.h, gfx.h, ui.h). */
void settings_apply(void)
{
    theme_set_contrast(st.high_contrast);
    theme_apply(theme_find(st.theme));
    gfx_text_size(st.text_large);
}

void settings_load(void)
{
    read_kv(XENITH_SETTINGS_PATH, on_settings);
    /* Every program loads the settings, so every program takes the look from here. */
    settings_apply();
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
    fprintf(f, "crash_reports=%d\n", st.crash_reports);
    fprintf(f, "theme=%s\nwallpaper=%s\n", st.theme, st.wallpaper);
    {
        char buttons[96];
        ui_pad_map_text(st.pad_map, buttons, sizeof buttons);
        fprintf(f, "text_large=%d\nhigh_contrast=%d\nbuttons=%s\nswap_sticks=%d\n",
                st.text_large, st.high_contrast, buttons, st.swap_sticks);
    }
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
