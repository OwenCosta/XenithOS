/* MineNest's background service, from XenithOS: plain HTTP to 127.0.0.1 (its port in
 * daemon.json, its key in api.token, both in MineNest's folder), started the first time it's
 * needed and left running, so servers keep going after the app closes.
 *
 * The minenest program is XenithOS's copy (/usr/lib/xenithos/bin/minenest, which install.sh
 * fetches from MineNest's releases on GitHub), MINENEST_BIN, or one on the PATH. */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "audio.h"
#include "mn.h"
#include "platform.h"
#include "theme.h"
#include "ui.h"

#define DEFAULT_PORT 47575

static int port = DEFAULT_PORT;
static char token[80];

static void home_path(const char *name, char *out, int n)
{
    const char *h = getenv("MINENEST_HOME"), *x = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (h && *h) snprintf(out, n, "%s/%s", h, name);
    else if (x && *x) snprintf(out, n, "%s/minenest/%s", x, name);
    else snprintf(out, n, "%s/.local/share/minenest/%s", home ? home : ".", name);
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long len;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || len > (1 << 20) || !(buf = malloc(len + 1))) { fclose(f); return NULL; }
    len = (long)fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);
    return buf;
}

/* The port and the key, as the service wrote them. */
static void read_files(void)
{
    char path[600], *t;
    home_path("daemon.json", path, sizeof path);
    if ((t = slurp(path))) {
        long long p = js_num(js_key(t, "port"));
        if (p > 0 && p < 65536) port = (int)p;
        free(t);
    }
    home_path("api.token", path, sizeof path);
    token[0] = 0;
    if ((t = slurp(path))) {
        int i = 0;
        for (char *c = t; *c && i < (int)sizeof token - 1; c++)
            if ((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f')) token[i++] = *c;
        token[i] = 0;
        free(t);
    }
}

const char *mn_engine(void)
{
    static char found[600];
    const char *env = getenv("MINENEST_BIN"), *path;
    char dirs[1024], *d, *save = NULL;
    if (env && *env && access(env, X_OK) == 0) return env;
    if (access("/usr/lib/xenithos/bin/minenest", X_OK) == 0) return "/usr/lib/xenithos/bin/minenest";
    path = getenv("PATH");
    snprintf(dirs, sizeof dirs, "%s", path && *path ? path : "/usr/local/bin:/usr/bin:/bin");
    for (d = strtok_r(dirs, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
        snprintf(found, sizeof found, "%s/minenest", d);
        if (access(found, X_OK) == 0) return found;
    }
    return NULL;
}

void mn_free(struct mn_resp *r)
{
    free(r->body);
    r->body = NULL;
}

static int send_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) { if (w < 0 && errno == EINTR) continue; return 0; }
        p += w;
        n -= (size_t)w;
    }
    return 1;
}

int mn_call(const char *method, const char *path, const char *json, struct mn_resp *r)
{
    struct sockaddr_in a;
    char head[1024], *buf = NULL, *body;
    size_t len = 0, cap = 0;
    int fd, blen = json ? (int)strlen(json) : 0;
    memset(r, 0, sizeof *r);
    r->status = -1;
    if (!token[0]) read_files();
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return -1; }
    snprintf(head, sizeof head,
             "%s %s HTTP/1.0\r\nHost: 127.0.0.1:%d\r\nAuthorization: Bearer %s\r\nX-MineNest: 1\r\n"
             "User-Agent: " MN_AGENT "\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n",
             method, path, port, token, blen);
    if (!send_all(fd, head, strlen(head)) || (blen && !send_all(fd, json, (size_t)blen))) { close(fd); return -1; }
    for (;;) {
        struct pollfd p = {fd, POLLIN, 0};
        ssize_t got;
        if (cap - len < 4096) {
            char *nb;
            cap = cap ? cap * 2 : 16384;
            if (cap > (8u << 20) || !(nb = realloc(buf, cap + 1))) break;
            buf = nb;
        }
        if (poll(&p, 1, 30000) <= 0) break;            /* MineNest answers local calls quickly */
        got = recv(fd, buf + len, cap - len, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        len += (size_t)got;
    }
    close(fd);
    if (!buf) return -1;
    buf[len] = 0;
    if (strncmp(buf, "HTTP/1.", 7) || !(body = strstr(buf, "\r\n\r\n"))) { free(buf); return -1; }
    r->status = atoi(buf + 9);
    memmove(buf, body + 4, len - (size_t)(body + 4 - buf) + 1);
    r->body = buf;
    return r->status;
}

pid_t mn_spawn(char *const argv[])
{
    pid_t mid = fork();
    if (mid == 0) {
        pid_t g;
        int fd;
        setsid();
        g = fork();
        if (g == 0) {
            fd = open("/dev/null", O_RDWR);
            if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); }
            for (fd = 3; fd < 1024; fd++) close(fd);
            signal(SIGPIPE, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGHUP, SIG_DFL);
            execv(argv[0], argv);
            _exit(127);
        }
        _exit(g > 0 ? 0 : 1);
    }
    if (mid > 0) while (waitpid(mid, NULL, 0) < 0 && errno == EINTR) { }
    return mid;
}

int mn_connect(int start)
{
    struct mn_resp r;
    const char *engine;
    char *argv[3];
    int i;
    read_files();
    if (mn_call("GET", "/api/info", NULL, &r) == 200) { mn_free(&r); return 1; }
    mn_free(&r);
    if (!start || !(engine = mn_engine())) return 0;
    argv[0] = (char *)engine;
    argv[1] = "daemon";
    argv[2] = NULL;
    mn_spawn(argv);
    for (i = 0; i < 60; i++) {                     /* up to 15 seconds */
        ui_loading_frame("MineNest", "Starting MineNest");
        usleep(250000);
        read_files();
        if (mn_call("GET", "/api/info", NULL, &r) == 200) { mn_free(&r); return 1; }
        mn_free(&r);
    }
    return 0;
}

void mn_escape(const char *in, char *out, int n)
{
    int o = 0;
    for (; *in && o < n - 7; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c < 0x20) o += snprintf(out + o, n - o, "\\u%04x", c);
        else out[o++] = (char)c;
    }
    out[o] = 0;
}

void mn_urlenc(const char *in, char *out, int n)
{
    static const char hex[] = "0123456789ABCDEF";
    int o = 0;
    for (; *in && o < n - 4; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out[o++] = (char)c;
        else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
    }
    out[o] = 0;
}

static void reply_error(struct mn_resp *r, char *why, int n)
{
    why[0] = 0;
    if (r->body) js_str(js_key(r->body, "error"), why, n);
    if (!why[0]) {
        if (r->status < 0) snprintf(why, n, "MineNest isn't answering. Close MineNest and open it again.");
        else snprintf(why, n, "MineNest answered with error %d.", r->status);
    }
}

int mn_wait_job(const char *job, const char *title, char *why, int n)
{
    char path[80], step[200] = "", err[300];
    snprintf(path, sizeof path, "/api/jobs/%s", job);
    for (;;) {
        struct mn_resp r;
        long long done, total;
        int finished;
        if (mn_call("GET", path, NULL, &r) != 200) {
            reply_error(&r, why, n);
            mn_free(&r);
            return 0;
        }
        js_str(js_key(r.body, "step"), step, sizeof step);
        done = js_num(js_key(r.body, "done"));
        total = js_num(js_key(r.body, "total"));
        finished = js_bool(js_key(r.body, "finished"));
        err[0] = 0;
        js_str(js_key(r.body, "error"), err, sizeof err);
        mn_free(&r);
        if (finished) {
            if (err[0]) { snprintf(why, n, "%s", err); return 0; }
            return 1;
        }
        ui_poll();
        if (total > 0) ui_progress_frame(title, step, (int)(done / 1024), (int)(total / 1024));
        else ui_loading_frame(title, step);
        usleep(120000);
    }
}

int mn_do(const char *method, const char *path, const char *json, char *why, int n)
{
    struct mn_resp r;
    char job[32] = "";
    int ok;
    ui_busy_begin("MineNest", NULL);
    ok = mn_call(method, path, json, &r) == 200;
    ui_busy_end();
    if (!ok) {
        reply_error(&r, why, n);
        mn_free(&r);
        return 0;
    }
    js_str(js_key(r.body, "job"), job, sizeof job);
    mn_free(&r);
    why[0] = 0;
    if (job[0]) return mn_wait_job(job, "MineNest", why, n);
    return 1;
}
