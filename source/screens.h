#ifndef XENITH_SCREENS_H
#define XENITH_SCREENS_H

#include <stdint.h>
#include "net.h"

void screen_apps(void);
void screen_store(void);
void screen_cloud(void);
void screen_profile(void);
void screen_settings(void);
void screen_feedback(void);                 /* bugs and ideas, sent to XenithOS Cloud */
void screen_friends(void);                  /* friends: who's on, what they play, requests */

/* screen_friends.c: the friends list from XenithOS Cloud, which the Cloud screen shows too */
enum { REL_FRIEND, REL_IN, REL_OUT };       /* friends, asking you, asked by you */
struct friend_row {
    char name[20], avatar[16], app_name[40], mc[100];   /* mc: their HostMC server's address */
    int rel, online, seen;                  /* seen: seconds since they were last on */
};
#define MAX_FRIEND_ROWS 120
extern struct friend_row friend_rows[MAX_FRIEND_ROWS];
extern int nfriend_rows;
int  friends_fetch(void);                   /* 200, or why not */
void friend_status(const struct friend_row *p, char *out, int n, uint32_t *color);

/* cloud_saves.c: saves and settings that follow the profile (shared/cloudsave.h) */
struct cloud_entry {                        /* a file in XenithOS Cloud's listing */
    char app[20], name[52], sha[65], from[28], label[28];
    int rev, size, age, gone;               /* age: seconds since it came; gone: deleted */
};
#define CLOUD_LIST_MAX 96
extern struct cloud_entry cloud_list[CLOUD_LIST_MAX];
extern int ncloud_list;
extern long cloud_used, cloud_limit;
int  cloud_saves_on(void);                  /* signed in, and on for this machine (Settings) */
int  cloud_saves_list(const char *app);     /* XenithOS Cloud's listing (NULL: every app); the status */
int  cloud_saves_sync(const char *app_id, int ask);   /* NULL: every app. ask: about clashes */
const char *cloud_saves_problem(void);
void screen_saves(void);                    /* Profile, Cloud saves: what's kept, deleting it */

/* cloud_api.c: talking to XenithOS Cloud */
int  cloud_ready(const char *title);        /* server set and network up, or explains why not */
int  cloud_post(const char *path, const char *form, struct http_resp *r);
int  cloud_get(const char *path, int max, struct http_resp *r);
void cloud_sync(int quiet);                 /* refresh the profile, check the inbox */
void cloud_route_check(void);               /* use XenithOS Cloud's home-network address if it's here */
struct plat_guide;
int  guide_run(struct plat_guide *g);       /* the guide's side panel over a game (plat_set_guide) */
void cloud_poll_inbox(void);                /* cheap; checks about once a minute */
void cloud_playing(const char *app_id);     /* friends see this app running; NULL: back home */
int  cloud_friends_online(void);            /* from the last check; -1 without a profile */
void cloud_ago(long seconds, char *out, int n);   /* "5 minutes ago" */
void kv_get(const char *body, const char *key, char *out, int size);
int  split_fields(char *line, char **f, int max);
const char *cloud_error_text(struct http_resp *r, int status);

/* shared/keyboard.c and avatar_editor.c */
#include "keyboard.h"
int avatar_editor(char *code);              /* code is AVATAR_CODE_LEN+1 bytes; 1 if saved */

#endif
