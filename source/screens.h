#ifndef XENITH_SCREENS_H
#define XENITH_SCREENS_H

#include "net.h"

void screen_apps(void);
void screen_store(void);
void screen_cloud(void);
void screen_profile(void);
void screen_settings(void);

/* cloud_api.c: talking to XenithOS Cloud */
int  cloud_ready(const char *title);        /* server set and network up, or explains why not */
int  cloud_post(const char *path, const char *form, struct http_resp *r);
int  cloud_get(const char *path, int max, struct http_resp *r);
void cloud_sync(int quiet);                 /* refresh the profile, check the inbox */
void cloud_poll_inbox(void);                /* cheap; checks about once a minute */
void kv_get(const char *body, const char *key, char *out, int size);
int  split_fields(char *line, char **f, int max);
const char *cloud_error_text(struct http_resp *r, int status);

/* keyboard.c and avatar_editor.c */
#define KB_TEXT 0
#define KB_PIN  1
int keyboard(const char *title, const char *hint, char *buf, int maxlen, int mode);
int avatar_editor(char *code);              /* code is AVATAR_CODE_LEN+1 bytes; 1 if saved */

#endif
