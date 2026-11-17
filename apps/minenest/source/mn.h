/* MineNest on XenithOS: the Minecraft server app. See main.c for the tour. */
#ifndef MN_H
#define MN_H

#include <stdint.h>
#include <sys/types.h>

#define MN_AGENT      "XenithOS-MineNest/1.0"
#define EULA_URL      "https://aka.ms/MinecraftEULA"
#define HOSTMC_SERVER "hostmc/server"            /* HostMC's server, in XenithOS's data folder */

/* ------------------------------------------------------------- api.c */

/* MineNest's background service (the minenest program, from github.com/OwenCosta/minenest),
 * reached on this PC's loopback with the key in its folder. */
struct mn_resp {
    int status;                                  /* HTTP status, or -1 when it couldn't be reached */
    char *body;                                  /* NUL-terminated, malloc'd (NULL if none) */
};
const char *mn_engine(void);                     /* the minenest program, or NULL if it isn't here */
int  mn_connect(int start);                      /* 1 once the service answers (start: run it if not) */
int  mn_call(const char *method, const char *path, const char *json, struct mn_resp *r);
void mn_free(struct mn_resp *r);
/* A call that should work: 1 if it did, else 0 with why[] set from MineNest's answer. Its
 * reply's "job", if any, is followed behind a progress screen. */
int  mn_do(const char *method, const char *path, const char *json, char *why, int n);
int  mn_wait_job(const char *job, const char *title, char *why, int n);
void mn_escape(const char *in, char *out, int n); /* for a JSON string, without the quotes */
void mn_urlenc(const char *in, char *out, int n);
pid_t mn_spawn(char *const argv[]);              /* a program of its own, outliving this one */

/* ------------------------------------------------------------- json.c */

/* Just enough JSON to find values in MineNest's answers. A value is a pointer into the text;
 * every function takes NULL and gives NULL back. */
const char *js_key(const char *obj, const char *key);   /* member of an object */
const char *js_first(const char *arr);                   /* first element of an array */
const char *js_next(const char *val);                    /* the element after val */
const char *js_member(const char *obj, int i, char *key, int key_len);   /* i-th member */
int  js_str(const char *val, char *out, int n);          /* 1 if it's a string */
long long js_num(const char *val);
int  js_bool(const char *val);

/* --------------------------------------------------------------- qr.c */

#define QR_MAX 57                               /* version 10 */
int  qr_make(const char *text, uint8_t out[QR_MAX][QR_MAX]);   /* the size, 0 if it doesn't fit */
void qr_draw(const char *text, int x, int y, int size);        /* white square with the code */

#endif
