/* HostMC's own settings, what's installed, server.properties, and small helpers. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hostmc.h"
#include "ui.h"

struct hm_settings hs;
struct hm_installed hi;

/* ------------------------------------------------------------------ files */

void mkdirs(const char *path)
{
    char tmp[700], *p;
    snprintf(tmp, sizeof tmp, "%s", path);
    for (p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

int file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

long long file_size(const char *path)
{
    struct stat s;
    return stat(path, &s) == 0 ? (long long)s.st_size : -1;
}

char *file_read(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (len) *len = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 32L * 1024 * 1024 || !(buf = malloc((size_t)n + 1))) { fclose(f); return NULL; }
    if ((long)fread(buf, 1, (size_t)n, f) != n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    buf[n] = 0;
    if (len) *len = n;
    return buf;
}

/* Written to path.new first, so a crash never leaves half a file. */
int file_write(const char *path, const char *data, long len, int mode)
{
    char tmp[720];
    int fd, ok;
    snprintf(tmp, sizeof tmp, "%s.new", path);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return 0;
    fchmod(fd, mode);                       /* an older .new may have had other permissions */
    ok = write(fd, data, (size_t)len) == len;
    ok = close(fd) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) { unlink(tmp); return 0; }
    return 1;
}

/* key=value files (HostMC's own, all ASCII). */
static void kv_get(const char *text, const char *key, char *out, int n)
{
    size_t kl = strlen(key);
    const char *p = text;
    out[0] = 0;
    while (p && *p) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            int i = 0;
            p += kl + 1;
            while (*p && *p != '\n' && *p != '\r' && i < n - 1) out[i++] = *p++;
            out[i] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

/* -------------------------------------------------------------- settings */

void hs_load(void)
{
    char *t = file_read(HM_SETTINGS, NULL), v[64];
    memset(&hs, 0, sizeof hs);
    snprintf(hs.type, sizeof hs.type, "vanilla");
    snprintf(hs.version, sizeof hs.version, "latest");
    hs.ram_mb = mem_total_mb() >= 6000 ? 2048 : 1024;
    if (!t) return;
    kv_get(t, "type", v, sizeof v);
    if (!strcmp(v, "paper") || !strcmp(v, "vanilla")) snprintf(hs.type, sizeof hs.type, "%s", v);
    kv_get(t, "version", v, sizeof v);
    if (v[0]) snprintf(hs.version, sizeof hs.version, "%s", v);
    kv_get(t, "ram", v, sizeof v);
    if (atoi(v) >= 512) hs.ram_mb = atoi(v);
    kv_get(t, "eula", hs.eula, sizeof hs.eula);
    kv_get(t, "internet", v, sizeof v);
    hs.internet = atoi(v) == 1;
    kv_get(t, "told_detach", v, sizeof v);
    hs.told_detach = atoi(v) == 1;
    free(t);
}

void hs_save(void)
{
    char buf[512];
    int n = snprintf(buf, sizeof buf, "type=%s\nversion=%s\nram=%d\neula=%s\ninternet=%d\ntold_detach=%d\n",
                     hs.type, hs.version, hs.ram_mb, hs.eula, hs.internet, hs.told_detach);
    mkdirs(HM_DIR);
    file_write(HM_SETTINGS, buf, n, 0644);
}

void hi_load(void)
{
    char *t = file_read(HM_SERVER "/installed.txt", NULL), v[32];
    memset(&hi, 0, sizeof hi);
    if (!t) return;
    kv_get(t, "type", hi.type, sizeof hi.type);
    kv_get(t, "version", hi.version, sizeof hi.version);
    kv_get(t, "build", hi.build, sizeof hi.build);
    kv_get(t, "java", v, sizeof v);
    hi.java = atoi(v) > 0 ? atoi(v) : 21;
    free(t);
    hi.ok = hi.type[0] && hi.version[0] && file_size(HM_SERVER "/server.jar") > 0;
}

void hi_save(void)
{
    char buf[256];
    int n = snprintf(buf, sizeof buf, "type=%s\nversion=%s\nbuild=%s\njava=%d\n", hi.type, hi.version, hi.build, hi.java);
    mkdirs(HM_SERVER);
    file_write(HM_SERVER "/installed.txt", buf, n, 0644);
    hi.ok = file_size(HM_SERVER "/server.jar") > 0;
}

/* ----------------------------------------------------- server.properties */

#define PROPS HM_SERVER "/server.properties"

static char *props_text;                  /* the file as last read */
static long long props_mtime = -1;
static uint64_t props_checked;

/* The server rewrites the file when it starts, so it's read again whenever it changes. */
static void props_load(void)
{
    struct stat s;
    long long mt;
    if (props_text && ui_ms() - props_checked < 1000) return;
    props_checked = ui_ms();
    mt = stat(PROPS, &s) == 0 ? (long long)s.st_mtime * 1000000000LL + s.st_mtim.tv_nsec : -2;
    if (mt == props_mtime && props_text) return;
    free(props_text);
    props_text = file_read(PROPS, NULL);
    if (!props_text) props_text = strdup("");
    props_mtime = mt;
}

/* Java properties escapes: \= \: \# \! \\ \t \n \r \f and \uXXXX. */
static void unescape(const char *s, char *out, int n)
{
    int k = 0;
    while (*s == ' ' || *s == '\t') s++;         /* space after the = isn't part of the value */
    for (; *s && *s != '\n' && *s != '\r' && k < n - 1; s++) {
        char c = *s;
        if (c == '\\' && s[1]) {
            s++;
            c = *s == 't' ? '\t' : *s == 'n' ? '\n' : *s == 'r' ? '\r' : *s == 'f' ? '\f' : *s;
            if (*s == 'u') {
                int v = 0, i;
                for (i = 1; i <= 4 && s[i]; i++) {
                    char h = s[i];
                    v = v * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                }
                s += i - 1;
                c = v >= 32 && v < 127 ? (char)v : '?';
            }
        }
        out[k++] = c;
    }
    out[k] = 0;
}

static void escape(const char *s, char *out, int n)
{
    int k = 0, first = 1;
    for (; *s && k < n - 3; s++, first = 0) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) continue;
        if (c == '\\' || c == '=' || c == ':' || c == '#' || c == '!' || (c == ' ' && first)) out[k++] = '\\';
        out[k++] = (char)c;
    }
    out[k] = 0;
}

/* The start of key's line, or NULL. */
static const char *props_find(const char *text, const char *key)
{
    size_t kl = strlen(key);
    const char *p = text;
    while (p && *p) {
        const char *q = p;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '#' && *q != '!' && !strncmp(q, key, kl) && (q[kl] == '=' || q[kl] == ':' || q[kl] == ' ')) return p;
        p = strchr(p, '\n');
        if (p) p++;
    }
    return NULL;
}

int props_get(const char *key, char *out, int n)
{
    const char *line, *eq;
    out[0] = 0;
    props_load();
    if (!(line = props_find(props_text, key))) return 0;
    eq = line + strspn(line, " \t") + strlen(key);
    while (*eq == ' ' || *eq == '\t') eq++;
    if (*eq == '=' || *eq == ':') eq++;
    unescape(eq, out, n);
    return 1;
}

int props_get_int(const char *key, int fallback)
{
    char v[32];
    if (!props_get(key, v, sizeof v) || !v[0]) return fallback;
    return atoi(v);
}

int props_set(const char *key, const char *value)
{
    char esc[400], line[480];
    const char *at;
    size_t len, pre, post_at;
    char *out;
    int ok;
    props_checked = 0;
    props_load();
    escape(value, esc, sizeof esc);
    snprintf(line, sizeof line, "%s=%s\n", key, esc);
    len = strlen(props_text);
    out = malloc(len + strlen(line) + 2);
    if (!out) return 0;
    if ((at = props_find(props_text, key))) {
        const char *end = strchr(at, '\n');
        pre = (size_t)(at - props_text);
        post_at = end ? (size_t)(end + 1 - props_text) : len;
        memcpy(out, props_text, pre);
        strcpy(out + pre, line);
        strcat(out, props_text + post_at);
    } else {
        strcpy(out, props_text);
        if (len && props_text[len - 1] != '\n') strcat(out, "\n");
        strcat(out, line);
    }
    mkdirs(HM_SERVER);
    ok = file_write(PROPS, out, (long)strlen(out), 0644);
    free(out);
    props_checked = 0;
    props_mtime = -1;
    return ok;
}

int server_port(void)
{
    int p = props_get_int("server-port", DEFAULT_PORT);
    return p > 0 && p < 65536 ? p : DEFAULT_PORT;
}

/* ---------------------------------------------------------------- the PC */

int mem_total_mb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[128];
    long kb = 0;
    if (!f) return 4096;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "MemTotal: %ld kB", &kb) == 1) break;
    fclose(f);
    return kb > 0 ? (int)(kb / 1024) : 4096;
}

static int private_v4(uint32_t a)
{
    return (a >> 24) == 10 || (a >> 20) == 0xAC1 || (a >> 16) == 0xC0A8;   /* 10/8, 172.16/12, 192.168/16 */
}

/* The address friends on the same network use: a private IPv4 address on a real interface
 * (not Docker's or a VM's bridge), else any IPv4 address that isn't loopback. */
int lan_address(char *out, int n)
{
    struct ifaddrs *list, *i;
    int best = 0;
    out[0] = 0;
    if (getifaddrs(&list) != 0) return 0;
    for (i = list; i; i = i->ifa_next) {
        uint32_t a;
        int score;
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
        if (!strncmp(i->ifa_name, "docker", 6) || !strncmp(i->ifa_name, "br-", 3) || !strncmp(i->ifa_name, "virbr", 5) ||
            !strncmp(i->ifa_name, "veth", 4) || !strncmp(i->ifa_name, "tun", 3) || !strncmp(i->ifa_name, "wg", 2)) continue;
        a = ntohl(((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr);
        score = private_v4(a) ? 2 : 1;
        if (score > best) {
            best = score;
            inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, out, (socklen_t)n);
        }
    }
    freeifaddrs(list);
    return best > 0;
}

void human_size(long long bytes, char *out, int n)
{
    if (bytes >= 1024LL * 1024 * 1024) snprintf(out, n, "%.1f GB", bytes / 1073741824.0);
    else if (bytes >= 1024 * 1024) snprintf(out, n, "%lld MB", (bytes + 524288) / 1048576);
    else if (bytes >= 1024) snprintf(out, n, "%lld KB", (bytes + 512) / 1024);
    else snprintf(out, n, "%lld bytes", bytes < 0 ? 0 : bytes);
}

void human_time(long long s, char *out, int n)
{
    if (s < 60) snprintf(out, n, "%lld s", s < 0 ? 0 : s);
    else if (s < 3600) snprintf(out, n, "%lld min", s / 60);
    else if (s < 86400) snprintf(out, n, "%lld h %lld min", s / 3600, s / 60 % 60);
    else snprintf(out, n, "%lld days %lld h", s / 86400, s / 3600 % 24);
}
