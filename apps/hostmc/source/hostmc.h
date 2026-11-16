/* HostMC: a Minecraft: Java Edition server on a XenithOS PC. See main.c for the tour. */
#ifndef HOSTMC_H
#define HOSTMC_H

#include <stdint.h>
#include <sys/types.h>

/* Everything lives in XenithOS's data folder (the working directory), outside "uda:", whose
 * colon Java would read as a path separator. */
#define HM_DIR        "hostmc"
#define HM_SETTINGS   "hostmc/hostmc.txt"
#define HM_SERVER     "hostmc/server"
#define HM_JAVA       "hostmc/java"
#define HM_CACHE      "hostmc/cache"
#define HM_BACKUPS    "hostmc/backups"
#define HM_PLAYIT     "hostmc/playit"

#define HM_AGENT      "XenithOS-HostMC/1.0"     /* the User-Agent for every download */
#define EULA_URL      "https://aka.ms/MinecraftEULA"
#define DEFAULT_PORT  25565

/* ------------------------------------------------------------------ main.c */

const char *hm_root(void);                       /* absolute path of HM_DIR */
void hm_frame(void);                             /* keeps things running during a long wait */

/* ------------------------------------------------------------- settings.c */

struct hm_settings {
    char type[12];          /* "vanilla" or "paper" */
    char version[24];       /* "latest" or a version id */
    int ram_mb;
    char eula[40];          /* when the EULA was agreed to; "" until it is */
    int internet;           /* playit.gg on */
    int vps;                /* the player's own VPS on (one of the two at a time) */
    int told_detach;        /* has been told the server keeps running after HostMC */
};
extern struct hm_settings hs;
void hs_load(void);
void hs_save(void);

/* What server.jar is (hostmc/server/installed.txt). */
struct hm_installed {
    int ok;                 /* there's a server.jar */
    char type[12], version[24], build[16];
    int java;               /* the Java it needs (major version) */
};
extern struct hm_installed hi;
void hi_load(void);
void hi_save(void);

/* server.properties: read and written in place, keeping what HostMC doesn't manage. */
int  props_get(const char *key, char *out, int n);      /* 0 if it isn't set */
int  props_get_int(const char *key, int fallback);
int  props_set(const char *key, const char *value);     /* 0 if the file couldn't be written */
int  server_port(void);

int  mem_total_mb(void);
int  lan_address(char *out, int n);                     /* this PC on the home network */
void human_size(long long bytes, char *out, int n);     /* "62 MB" */
void human_time(long long seconds, char *out, int n);   /* "1 h 12 min" */

/* Files */
char *file_read(const char *path, long *len);           /* NUL-terminated, malloc'd */
int  file_write(const char *path, const char *data, long len, int mode);   /* atomically */
int  file_exists(const char *path);
long long file_size(const char *path);
void mkdirs(const char *path);

/* -------------------------------------------------------------- jobs.c */

/* Child processes: downloads (curl), checksums, unpacking. */
struct job { pid_t pid; int status; int running; };
int  job_spawn(struct job *j, const char *const argv[], const char *out, const char *err);
int  job_done(struct job *j);                   /* 1 once it has finished (status = exit code) */
void job_stop(struct job *j);
/* Runs argv to the end behind a loading screen. The bar follows the size of `watch` against
 * `total` bytes (no bar while total is 0). B cancels when can_cancel. Returns the exit code,
 * -1 if it couldn't start, -2 if it was cancelled. */
int  job_run(const char *const argv[], const char *out, const char *title, const char *detail,
             const char *watch, long long total, int can_cancel);
int  job_run_err(const char *const argv[], const char *out, const char *err, const char *title, const char *detail,
                 const char *watch, long long total, int can_cancel);    /* stderr kept in err */
const char *find_program(const char *name, char *out, int n);           /* its full path, or NULL */
/* A download over HTTPS to `out` (through out.part). 0 when it's there; -2 cancelled. */
int  fetch(const char *url, const char *out, const char *title, const char *detail, long long total, int can_cancel);
/* A small download (JSON) into memory, behind a loading screen. NULL with why[] set. */
char *fetch_text(const char *url, const char *cache, int max_age_s, const char *title, char *why, int why_len);
/* "sha1" or "sha256" of a file, compared with hex. 1 if it matches. */
int  checksum_ok(const char *algo, const char *path, const char *hex);
void fetch_reason(int curl_code, char *why, int n);

/* Programs that outlive HostMC (the server, playit.gg's agent). */
unsigned long long proc_ticks(pid_t pid);       /* when it started, for telling PIDs apart */
int  proc_ours(pid_t pid, unsigned long long ticks, const char *dir);   /* still that, in dir */
pid_t spawn_detached(const char *dir, char *const argv[], int in_fd, int out_fd);

/* ------------------------------------------------------------- json.c */

/* Just enough JSON to find values in Mojang's, Paper's, Adoptium's and playit's answers. A
 * value is a pointer into the text; every function takes NULL and gives NULL back. */
const char *js_key(const char *obj, const char *key);   /* member of an object */
const char *js_first(const char *arr);                   /* first element of an array */
const char *js_next(const char *val);                    /* the element after val */
const char *js_member(const char *obj, int i, char *key, int key_len);   /* i-th member */
int  js_str(const char *val, char *out, int n);          /* 1 if it's a string */
long long js_num(const char *val);
int  js_bool(const char *val);

/* ----------------------------------------------------------- server.c */

enum { SV_STOPPED, SV_STARTING, SV_RUNNING, SV_STOPPING };
enum { LN_INFO, LN_WARN, LN_ERROR, LN_JOIN, LN_CHAT, LN_SENT, LN_HOSTMC };

#define LOG_KEEP  500
#define LOG_LEN   240
#define MAX_PLAYERS 64

struct hm_line { char text[LOG_LEN]; uint8_t kind; };

struct hm_server {
    int state;
    pid_t pid;
    unsigned long long ticks;       /* its start time, so a reused PID isn't taken for it */
    long long started;              /* time() when it was started */
    long long log_off;
    struct hm_line lines[LOG_KEEP]; /* the console, oldest first from `first` */
    int nlines, first;
    unsigned seq;                   /* lines added so far */
    char players[MAX_PLAYERS][17];
    int nplayers;
    int stop_asked;
    uint64_t stop_at;
    char why[200];                  /* why it stopped by itself, last time */
    unsigned saves;                 /* "Saved the game" lines seen */
    int restart;                    /* start again once it has stopped */
};
extern struct hm_server sv;

void sv_attach(void);                           /* a server left running earlier? */
void sv_poll(void);                             /* call every frame */
int  sv_start(char *why, int n);
int  sv_command(const char *cmd);               /* 1 if it reached the server */
void sv_stop(void);
void sv_force_stop(void);
void sv_note(const char *text);                 /* a line from HostMC in the console */
const struct hm_line *sv_line(int i);           /* i = 0 is the oldest kept */
int  sv_wait_saved(unsigned before, int seconds);

/* ---------------------------------------------------------- install.c */

#define MAX_VERSIONS 40
struct version_list { int n; char id[MAX_VERSIONS][24]; char latest[24]; };
int  versions_get(int paper, int refresh, struct version_list *out, char *why, int n);   /* releases, newest first */
int  version_newer(const char *a, const char *b);   /* 1 if a is a later release than b */

/* What a download would be (small requests only), then the download itself. */
struct server_plan {
    int paper;
    char version[24], build[16];
    char url[300], hash[72], algo[8];
    long long size;
    int java;
};
int  plan_server(const char *type, const char *want, struct server_plan *p, char *why, int n);
int  get_server(const struct server_plan *p, char *why, int n);     /* server.jar, installed.txt */

struct java_plan { int major; char url[300], sha256[72], release[40]; long long size; };
const char *java_path(int major);               /* NULL until it's downloaded */
int  plan_java(int major, struct java_plan *p, char *why, int n);
int  get_java(const struct java_plan *p, char *why, int n);

/* ---------------------------------------------------------- players.c */

int  whitelist_add(const char *name, char *why, int n);    /* running or not, online mode or not */
void offline_uuid(const char *name, char out[37]);          /* a player's UUID in offline mode */

/* ------------------------------------------------------------ world.c */

#define MAX_BACKUPS 20
struct backup_list { int n; char name[MAX_BACKUPS][48]; long long size[MAX_BACKUPS]; };
int  world_backup(const char *why_made, char *why, int n);
void world_backups(struct backup_list *out);    /* newest first */
int  world_restore(const char *name, char *why, int n);
int  world_reset(char *why, int n);
int  world_exists(void);

/* ----------------------------------------------------------- playit.c */

enum { PI_OFF, PI_CLAIM, PI_STARTING, PI_ON, PI_PROBLEM };
struct hm_playit {
    int state;
    char claim_url[64];
    int claim_seen;                 /* the link has been opened */
    char address[96];               /* the public address, once there is one */
    char note[200];                 /* what's happening, or what went wrong */
};
extern struct hm_playit pi;
void pi_init(void);
void pi_poll(void);
int  pi_turn_on(char *why, int n);              /* downloads the agent and starts the claim */
void pi_turn_off(void);
void pi_forget(void);
void pi_server_started(void);
void pi_server_stopped(void);
int  pi_has_secret(void);
int  pi_agent_running(void);

/* ------------------------------------------------------------- ping.c */

/* A Minecraft server list ping, a step at a time: start it, then poll it every frame. */
enum { PING_IDLE, PING_CONNECTING, PING_READING, PING_DONE };
enum { PING_OK = 1, PING_REFUSED, PING_TIMEOUT, PING_NO_HOST, PING_UNREACHABLE, PING_SILENT, PING_NOT_MC, PING_CANCELLED };
struct mcping {
    int state, result, fd;
    uint64_t deadline;
    unsigned char out[320];
    int out_len;
    char in[65536];
    int in_len;
    char version[32];                           /* what answered: "26.3" */
    int online, max;
};
int  mcping_start(struct mcping *p, const char *host, int port, int timeout_ms);   /* 0: done already */
int  mcping_poll(struct mcping *p);                                                 /* 1 once it's done */
void mcping_cancel(struct mcping *p);

/* ----------------------------------------------------------- tunnel.c */

/* An SSH reverse tunnel to a server on the internet (see tunnel.c). */
struct tunnel_cfg {
    char host[128];                             /* the server: an address or a name */
    char user[48];
    int ssh_port;
    char bind[48];                              /* where it listens: "0.0.0.0", or "" for sshd's choice */
    int public_port;                            /* what players connect to there */
    int local_port;                             /* the Minecraft server here */
};
struct tunnel {
    char dir[64];                               /* its folder (key, known_hosts, PID file, log) */
    char abs[740];                              /* the same, as an absolute path */
    pid_t pid;
    unsigned long long ticks;
    uint64_t started;
};
int  tunnel_cfg_ok(const struct tunnel_cfg *c, char *why, int n);
void tunnel_init(struct tunnel *t, const char *dir, const char *abs);   /* finds one left running */
int  tunnel_key(struct tunnel *t, char *why, int n);                    /* makes HostMC's key, once */
int  tunnel_pubkey(struct tunnel *t, char *out, int n);                 /* its authorized_keys line */
int  tunnel_start(struct tunnel *t, const struct tunnel_cfg *c, char *why, int n);
void tunnel_stop(struct tunnel *t);
int  tunnel_alive(struct tunnel *t);                                    /* the loop that keeps ssh going */
enum { TS_STOPPED, TS_CONNECTING, TS_UP, TS_RETRYING };
int  tunnel_state(struct tunnel *t, const struct tunnel_cfg *c, char *why, int n, long long *attempt);
int  tunnel_test(struct tunnel *t, const struct tunnel_cfg *c, char *why, int n);     /* log in and out */
void tunnel_forget_host(struct tunnel *t);                              /* trust a new host key */

/* -------------------------------------------------------------- vps.c */

/* The player's own VPS as the way in from the internet. */
struct hm_vps {
    struct tunnel_cfg cfg;
    struct tunnel t;
    char why[220];                              /* why the tunnel isn't up */
    char check[320];                            /* what the check from the internet found */
    int check_ok;
};
extern struct hm_vps vps;
void vps_init(void);
void vps_load(void);
void vps_save(void);
void vps_poll(void);
int  vps_turn_on(char *why, int n);             /* makes the key, turns playit.gg off */
void vps_turn_off(void);
void vps_server_started(void);
void vps_server_stopped(void);
void vps_check(void);
int  vps_checking(void);
int  vps_up(void);
void vps_address(char *out, int n);             /* what friends type */
const char *vps_status(uint32_t *color);

/* --------------------------------------------------------------- qr.c */

#define QR_MAX 57                               /* version 10 */
int  qr_make(const char *text, uint8_t out[QR_MAX][QR_MAX]);   /* the size, 0 if it doesn't fit */
void qr_draw(const char *text, int x, int y, int size);        /* white square with the code */

#endif
