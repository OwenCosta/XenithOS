#ifndef XENITH_CONFIG_H
#define XENITH_CONFIG_H

#include "net.h"

/* Files XenithOS keeps on the USB stick, all plain key=value text:
 *
 *   xenith/config.txt    written by you or build.sh: server address, console key
 *   xenith/settings.txt  written by XenithOS: volume, sounds, inbox position
 *   xenith/profile.txt   written by XenithOS: the signed-in account
 *   xenith/route.txt     written by XenithOS: other ways to reach XenithOS Cloud, checked at
 *                        each start (cloud_route_check): its address on this network, when
 *                        the console is on the network it runs on, and on PCs its HTTPS one
 */

struct xenith_config {
    char server[NET_ADDR_LEN];        /* where requests go: the home-network route, or else server_https, or else server_main */
    char server_main[NET_ADDR_LEN];   /* the address in config.txt ("a.b.c.d:port"; on PCs it may be "https://name") */
    char server_https[NET_ADDR_LEN];  /* PCs: "https://name[:port]", XenithOS Cloud's HTTPS address, while it answers there */
    char route_id[24];        /* XenithOS Cloud's id, while server is its home-network address */
    char name[16];            /* legacy leaderboard name, used when nobody is signed in */
    char key[72];             /* console key for reading the inbox */
    int loaded;
};

struct xenith_settings {
    int volume;               /* 0..100 */
    int ui_sounds;            /* 0/1 */
    int boot_sound;           /* 0/1 */
    int auto_connect;         /* connect to the network after boot */
    int last_inbox;           /* newest inbox message already shown */
    char last_app[96];        /* most recently launched app */
    int crash_reports;        /* 0/1: send crash reports without asking (crash.h); off at first */
    int cloud_saves;          /* keep saves and settings in step with the profile's cloud saves */
};

struct xenith_profile {
    int signed_in;
    char name[20];
    char token[64];
    char avatar[16];          /* up to AVATAR_CODE_LEN + 1 */
    char joined[16];
    int sharing;              /* friends see what's playing (Settings); kept on XenithOS Cloud too */
};

extern struct xenith_config cfg;
extern struct xenith_settings st;
extern struct xenith_profile prof;

void config_load(void);       /* needs storage; takes the routes in route.txt when they're for this server */
void route_save(const char *lan, const char *id);   /* use lan from now on, and remember it */
void route_forget(void);                             /* back to server_https or server_main */
void https_save(const char *addr);                   /* PCs: use this "https://" address away from home, and remember it */
void https_forget(void);                             /* back to server_main away from home */
void settings_load(void);
void settings_save(void);
void profile_load(void);
void profile_save(void);
void profile_clear(void);

#define XENITH_DIR           "uda:/xenith"
#define XENITH_CONFIG_PATH   "uda:/xenith/config.txt"
#define XENITH_SETTINGS_PATH "uda:/xenith/settings.txt"
#define XENITH_PROFILE_PATH  "uda:/xenith/profile.txt"
#define XENITH_ROUTE_PATH    "uda:/xenith/route.txt"

#endif
