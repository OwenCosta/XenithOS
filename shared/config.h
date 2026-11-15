#ifndef XENITH_CONFIG_H
#define XENITH_CONFIG_H

/* Files XenithOS keeps on the USB stick, all plain key=value text:
 *
 *   xenith/config.txt    written by you or build.sh: server address, console key
 *   xenith/settings.txt  written by XenithOS: volume, sounds, inbox position
 *   xenith/profile.txt   written by XenithOS: the signed-in account
 */

struct xenith_config {
    char server[32];          /* "a.b.c.d:port" */
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
};

struct xenith_profile {
    int signed_in;
    char name[20];
    char token[64];
    char avatar[16];          /* up to AVATAR_CODE_LEN + 1 */
    char joined[16];
};

extern struct xenith_config cfg;
extern struct xenith_settings st;
extern struct xenith_profile prof;

void config_load(void);       /* needs storage */
void settings_load(void);
void settings_save(void);
void profile_load(void);
void profile_save(void);
void profile_clear(void);

#define XENITH_DIR           "uda:/xenith"
#define XENITH_CONFIG_PATH   "uda:/xenith/config.txt"
#define XENITH_SETTINGS_PATH "uda:/xenith/settings.txt"
#define XENITH_PROFILE_PATH  "uda:/xenith/profile.txt"

#endif
