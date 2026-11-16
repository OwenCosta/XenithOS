/* Apps from Flathub and games from Steam, on PCs and Raspberry Pis (see platform.h).
 *
 * Flathub: XenithOS's Store lists apps from Flathub (XenithOS Cloud picks which, in
 * /v1/store/flathub) and installs them with Flatpak for this user only, so no sudo, showing
 * Flatpak's own progress in XenithOS's screens. Installed, an app is "uda:/apps/<id>.app":
 *
 *     name=Kodi
 *     flatpak=tv.kodi.Kodi
 *     args=
 *
 * Steam: the Steam app comes from Flathub like the rest. The games installed in it are found
 * in Steam's own library folders and each gets "uda:/apps/steam-<appid>.app" (steam=<appid>)
 * with Steam's picture of it, so they're on XenithOS's home screen too. They start through
 * Steam, in Big Picture.
 *
 * Your own: "exec=" runs any program (its arguments split at spaces; no shell).
 *
 * platform_sdl.c does the starting (the screen has to be handed over).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "img.h"
#include "picture.h"
#include "platform.h"
#include "platform_linux.h"

#define FLATHUB_URL "https://dl.flathub.org/repo/flathub.flatpakrepo"
#define STEAM_REF   "com.valvesoftware.Steam"
#define PRISM_REF   "org.prismlauncher.PrismLauncher"

/* ---------------------------------------------------------------- helpers */

const char *plat_arch(void)
{
    static char arch[32];
    struct utsname u;
    if (arch[0]) return arch;
    if (uname(&u) != 0) return "";
    snprintf(arch, sizeof arch, "%s", !strcmp(u.machine, "arm64") ? "aarch64" : u.machine);
    return arch;
}

char *linux_find_program(const char *name, char *out, int n)
{
    static const char *const dirs[] = {"/usr/bin", "/usr/local/bin", "/bin", "/usr/games", "/usr/local/games"};
    char var[48];
    const char *env;
    int i;
    snprintf(var, sizeof var, "XENITHOS_%s", name);
    for (i = 9; var[i]; i++) var[i] = (char)(var[i] == '-' ? '_' : toupper((unsigned char)var[i]));
    env = getenv(var);
    if (env && *env) {
        if (access(env, X_OK) != 0) return NULL;
        snprintf(out, n, "%s", env);
        return out;
    }
    for (i = 0; i < (int)(sizeof dirs / sizeof dirs[0]); i++) {
        snprintf(out, n, "%s/%s", dirs[i], name);
        if (access(out, X_OK) == 0) return out;
    }
    return NULL;
}

pid_t linux_spawn(char *const argv[], char *const envp[], const char *log, int new_group)
{
    int out = log ? open(log, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644) : open("/dev/null", O_WRONLY | O_CLOEXEC);
    int in = open("/dev/null", O_RDONLY | O_CLOEXEC);
    pid_t pid = fork();
    if (pid == 0) {
        if (new_group) setpgid(0, 0);
        if (in >= 0) dup2(in, 0);
        if (out >= 0) { dup2(out, 1); dup2(out, 2); }
        if (envp) execve(argv[0], argv, envp);
        else execv(argv[0], argv);
        _exit(127);
    }
    if (pid > 0 && new_group) setpgid(pid, pid);   /* either side may get there first */
    if (out >= 0) close(out);
    if (in >= 0) close(in);
    return pid;
}

/* The last n - 1 bytes of a file. */
static int read_tail(const char *path, char *out, int n)
{
    FILE *f = fopen(path, "rb");
    long size;
    int got;
    out[0] = 0;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, size > n - 1 ? size - (n - 1) : 0, SEEK_SET);
    got = (int)fread(out, 1, (size_t)(n - 1), f);
    fclose(f);
    if (got < 0) got = 0;
    out[got] = 0;
    return got;
}

/* Text for XenithOS's font: printable ASCII only; curly quotes become plain ones. */
static void ascii_copy(char *out, int n, const char *in)
{
    const unsigned char *s = (const unsigned char *)in;
    int k = 0;
    while (*s && k < n - 1) {
        if (s[0] == 0xE2 && s[1] == 0x80 && (s[2] == 0x98 || s[2] == 0x99)) { out[k++] = '\''; s += 3; continue; }
        if (s[0] == 0xE2 && s[1] == 0x80 && (s[2] == 0x9C || s[2] == 0x9D)) { out[k++] = '"'; s += 3; continue; }
        if (*s >= 32 && *s < 127) out[k++] = (char)*s;
        s++;
    }
    out[k] = 0;
    while (k > 0 && out[k - 1] == ' ') out[--k] = 0;
}

int linux_log_reason(const char *log, char *out, int n)
{
    char tail[4096], *line, *save = NULL, *err = NULL, *last = NULL;
    out[0] = 0;
    read_tail(log, tail, sizeof tail);
    for (line = strtok_r(tail, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        while (*line == ' ' || *line == '\t') line++;
        if (!*line) continue;
        last = line;
        if (!strncasecmp(line, "error:", 6)) err = line + 6;
    }
    if (err) {
        size_t word;
        while (*err == ' ') err++;
        ascii_copy(out, n, err);
        word = strcspn(out, " ");                 /* a sentence gets a capital; "app/..." doesn't */
        if (out[0] && !memchr(out, '/', word) && !memchr(out, '.', word)) out[0] = (char)toupper((unsigned char)out[0]);
        return 2;
    }
    if (last) ascii_copy(out, n, last);
    return out[0] ? 1 : 0;
}

/* Flathub ids look like "tv.kodi.Kodi": letters, digits, _ and - in three or more parts. */
static int valid_ref(const char *r)
{
    int dots = 0, i;
    if (!isalpha((unsigned char)r[0]) || strlen(r) > 120) return 0;
    for (i = 0; r[i]; i++) {
        if (r[i] == '.') {
            if (r[i + 1] == '.' || !r[i + 1]) return 0;
            dots++;
        } else if (!isalnum((unsigned char)r[i]) && r[i] != '_' && r[i] != '-') {
            return 0;
        }
    }
    return dots >= 2;
}

/* ---------------------------------------------------------------- Flatpak */

int plat_flatpak_ready(void)
{
    char fp[256];
    return linux_find_program("flatpak", fp, sizeof fp) != NULL;
}

/* Installed for this user (~/.local/share/flatpak) or for everyone (/var/lib/flatpak). Its
 * folder there has "current" pointing at the deployed version. */
int plat_flatpak_installed(const char *ref)
{
    char path[700];
    const char *home = getenv("HOME");
    struct stat s;
    if (!valid_ref(ref)) return 0;
    if (home && *home) {
        snprintf(path, sizeof path, "%s/.local/share/flatpak/app/%s/current/active", home, ref);
        if (stat(path, &s) == 0) return 1;
    }
    snprintf(path, sizeof path, "/var/lib/flatpak/app/%s/current/active", ref);
    return stat(path, &s) == 0;
}

/* Flatpak prints "Installing 2/3… ████  45%  3.1 MB/s" as it goes (one line each time when
 * it isn't printing to a terminal). The whole job's progress, each part counted as an equal
 * share; -1 until it says. */
static int flatpak_percent(const char *t)
{
    const char *p, *at = NULL;
    int pct = -1, op = 1, ops = 1, a, b;
    for (p = t + strlen(t); p > t; p--)
        if (p[-1] == '%' && p - 1 > t && isdigit((unsigned char)p[-2])) {
            const char *d = p - 1;
            int v = 0, mul = 1, digits = 0;
            while (d > t && isdigit((unsigned char)d[-1]) && digits < 3) { v += (d[-1] - '0') * mul; mul *= 10; d--; digits++; }
            pct = v > 100 ? 100 : v;
            at = d;
            break;
        }
    if (pct < 0) return -1;
    for (p = t; (p = strstr(p, "ing ")) && p < at; p += 4)
        if (sscanf(p + 4, "%d/%d", &a, &b) == 2 && a >= 1 && b >= a && b < 100) { op = a; ops = b; }
    return ((op - 1) * 100 + pct) / ops;
}

/* Run Flatpak until it's done, calling step() as it goes. Its exit code, -1 if it couldn't
 * start, -2 if step() asked to stop. */
static int run_flatpak(char *const argv[], const char *log, int (*step)(int, int), uint64_t start)
{
    char tail[1024];
    uint64_t parsed = 0, stop_at = 0;
    int st, pct = -1;
    pid_t pid = linux_spawn(argv, NULL, log, 1), r;
    if (pid < 0) return -1;
    for (;;) {
        uint64_t now = plat_time_us();
        r = waitpid(pid, &st, WNOHANG);
        if (r == pid) {
            if (stop_at) return -2;
            return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
        }
        if (r < 0 && errno != EINTR) return -1;
        if (now - parsed > 150000) {
            parsed = now;
            read_tail(log, tail, sizeof tail);
            pct = flatpak_percent(tail);
        }
        if (step) {
            if (!step(pct, (int)((now - start) / 1000000)) && !stop_at) {
                kill(-pid, SIGINT);              /* Flatpak stops; what it downloaded is kept for next time */
                stop_at = now;
            }
        } else {
            plat_sleep_us(20000);
        }
        if (stop_at && now - stop_at > 8000000) kill(-pid, SIGTERM);
        if (stop_at && now - stop_at > 15000000) kill(-pid, SIGKILL);
    }
}

int plat_flatpak(const char *verb, const char *ref, int (*step)(int percent, int seconds), char *why, int why_len)
{
    char fp[256], log[700], reason[200], verb_c[16], ref_c[128];
    char *argv[8];
    uint64_t start = plat_time_us();
    int code;
    FILE *f;

    why[0] = 0;
    if (!valid_ref(ref)) {
        snprintf(why, why_len, "The Store sent an app name XenithOS doesn't accept.");
        return 0;
    }
    if (!linux_find_program("flatpak", fp, sizeof fp)) {
        snprintf(why, why_len, "Apps need Flatpak, which isn't on this machine yet. Run the XenithOS install command once more and come back to the Store.");
        return 0;
    }
    snprintf(verb_c, sizeof verb_c, "%s", verb);
    snprintf(ref_c, sizeof ref_c, "%s", ref);
    snprintf(log, sizeof log, "%s/uda:/xenith/flatpak.log", linux_data_dir());
    if ((f = fopen(log, "w"))) fclose(f);

    if (!strcmp(verb, "install")) {
        char *add[] = {fp, "remote-add", "--user", "--if-not-exists", "flathub", FLATHUB_URL, NULL};
        code = run_flatpak(add, log, step, start);
        if (code == -2) return -1;
        if (code != 0) {
            linux_log_reason(log, reason, sizeof reason);
            snprintf(why, why_len, "Couldn't reach Flathub. Is this machine online?%s%s", reason[0] ? " " : "", reason);
            return 0;
        }
        argv[0] = fp; argv[1] = verb_c; argv[2] = "--user"; argv[3] = "-y"; argv[4] = "flathub"; argv[5] = ref_c; argv[6] = NULL;
    } else if (!strcmp(verb, "update") || !strcmp(verb, "uninstall")) {
        argv[0] = fp; argv[1] = verb_c; argv[2] = "--user"; argv[3] = "-y"; argv[4] = ref_c; argv[5] = NULL;
    } else if (!strcmp(verb, "purge")) {          /* uninstall, and its data folder with it */
        argv[0] = fp; argv[1] = "uninstall"; argv[2] = "--user"; argv[3] = "-y"; argv[4] = "--delete-data";
        argv[5] = ref_c; argv[6] = NULL;
    } else {
        snprintf(why, why_len, "XenithOS can't do that.");
        return 0;
    }
    code = run_flatpak(argv, log, step, start);
    if (code == 0) return 1;
    if (code == -2) return -1;
    linux_log_reason(log, reason, sizeof reason);
    if (code == -1) snprintf(why, why_len, "Flatpak didn't start.");
    else snprintf(why, why_len, "%s", reason[0] ? reason : "Flatpak stopped without saying why.");
    return 0;
}

/* ---------------------------------------------------------------- Steam */

/* Where Steam keeps its things: the Flathub Steam's, then a Steam from the distribution. */
static const char *const steam_dirs[] = {
    "/.var/app/" STEAM_REF "/.local/share/Steam",
    "/.local/share/Steam",
    "/.steam/debian-installation",
    "/.steam/steam",
};
static const char *const steam_registries[] = {
    "/.var/app/" STEAM_REF "/.steam/registry.vdf",
    "/.steam/registry.vdf",
};

static char *native_steam(char *out, int n)
{
    return linux_find_program("steam", out, n);
}

int plat_steam_ready(void)
{
    char p[256];
    return plat_flatpak_installed(STEAM_REF) || native_steam(p, sizeof p) != NULL;
}

/* Adds s to a's arguments, copied into a->buf. 0 if it doesn't fit. */
/* Prism Launcher's instance played most recently (its folder name), from the instance.cfg
 * files in its Flatpak data folder. 0 when there's none yet. */
static int prism_last_instance(char *out, int n)
{
    const char *home = getenv("HOME");
    char dir[400];
    long long best = 0;
    DIR *d;
    struct dirent *de;
    if (!home || !*home) return 0;
    snprintf(dir, sizeof dir, "%s/.var/app/" PRISM_REF "/data/PrismLauncher/instances", home);
    if (!(d = opendir(dir))) return 0;
    out[0] = 0;
    while ((de = readdir(d))) {
        char cfg[700], line[256];
        FILE *f;
        if (de->d_name[0] == '.' || strlen(de->d_name) >= (size_t)n) continue;
        snprintf(cfg, sizeof cfg, "%s/%s/instance.cfg", dir, de->d_name);
        if (!(f = fopen(cfg, "r"))) continue;
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "lastLaunchTime=", 15)) {
                long long t = atoll(line + 15);
                if (t > best) {
                    best = t;
                    snprintf(out, (size_t)n, "%s", de->d_name);
                }
            }
        fclose(f);
    }
    closedir(d);
    return best > 0;
}

static int put_arg(struct linux_app *a, int *k, int *used, const char *s)
{
    int len = (int)strlen(s);
    if (*k >= (int)(sizeof a->argv / sizeof a->argv[0]) - 1 || *used + len + 1 > (int)sizeof a->buf) return 0;
    memcpy(a->buf + *used, s, (size_t)len + 1);
    a->argv[(*k)++] = a->buf + *used;
    *used += len + 1;
    a->argv[*k] = NULL;
    return 1;
}

int linux_steam_command(const char *url, struct linux_app *a)
{
    char path[256];
    int k = 0, used = 0;
    memset(a, 0, sizeof *a);
    snprintf(a->name, sizeof a->name, "Steam");
    a->steam = 1;
    if (plat_flatpak_installed(STEAM_REF) && linux_find_program("flatpak", path, sizeof path)) {
        if (!put_arg(a, &k, &used, path) || !put_arg(a, &k, &used, "run") || !put_arg(a, &k, &used, STEAM_REF)) return 0;
    } else if (native_steam(path, sizeof path)) {
        if (!put_arg(a, &k, &used, path)) return 0;
    } else {
        return 0;
    }
    if (!put_arg(a, &k, &used, "-gamepadui")) return 0;
    return !url || !*url || put_arg(a, &k, &used, url);
}

/* Steam's settings files (VDF): "key" "value" pairs and "key" { ... } blocks. */
struct vdf { const char *p; };

/* The next token: 1 for a string (in out), '{' or '}', 0 at the end. */
static int vdf_next(struct vdf *v, char *out, int n)
{
    const char *p = v->p;
    int k = 0;
    for (;;) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
        break;
    }
    if (!*p) { v->p = p; return 0; }
    if (*p == '{' || *p == '}') { v->p = p + 1; return *p; }
    if (*p == '"') {
        for (p++; *p && *p != '"'; p++) {
            char c = *p;
            if (c == '\\' && p[1]) { c = *++p; if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; }
            if (k < n - 1) out[k++] = c;
        }
        if (*p == '"') p++;
    } else {
        for (; *p && !isspace((unsigned char)*p) && *p != '{' && *p != '}' && *p != '"'; p++)
            if (k < n - 1) out[k++] = *p;
    }
    out[k] = 0;
    v->p = p;
    return 1;
}

/* The value at a path of keys, like {"AppState", "name"} (keys compared ignoring case). */
static int vdf_get(const char *text, const char *const *path, int len, char *out, int n)
{
    struct vdf v;
    char key[256];
    int depth = 0, matched = 0, t;
    v.p = text;
    while ((t = vdf_next(&v, key, sizeof key)) != 0) {
        if (t == '}') {
            if (depth > 0) { if (matched == depth) matched--; depth--; }
            continue;
        }
        if (t != 1) continue;
        t = vdf_next(&v, out, n);
        if (t == 0) break;
        if (t == '{') {
            if (matched == depth && depth < len - 1 && !strcasecmp(key, path[depth])) matched++;
            depth++;
        } else if (t == '}') {
            if (depth > 0) { if (matched == depth) matched--; depth--; }
        } else if (matched == depth && depth == len - 1 && !strcasecmp(key, path[depth])) {
            return 1;
        }
    }
    out[0] = 0;
    return 0;
}

static char *read_text(const char *path, long max)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > max || !(buf = malloc((size_t)n + 1))) { fclose(f); return NULL; }
    n = (long)fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[n > 0 ? n : 0] = 0;
    return buf;
}

int linux_steam_game_running(int appid)
{
    const char *home = getenv("HOME");
    char path[700], id[16], val[32];
    int i, seen = 0;
    if (!home) return -1;
    snprintf(id, sizeof id, "%d", appid);
    for (i = 0; i < (int)(sizeof steam_registries / sizeof steam_registries[0]); i++) {
        const char *running[] = {"Registry", "HKCU", "Software", "Valve", "Steam", "apps", id, "Running"};
        const char *current[] = {"Registry", "HKCU", "Software", "Valve", "Steam", "RunningAppID"};
        char *text;
        snprintf(path, sizeof path, "%s%s", home, steam_registries[i]);
        if (!(text = read_text(path, 4 << 20))) continue;
        if (vdf_get(text, running, 8, val, sizeof val)) {
            seen = 1;
            if (atoi(val)) { free(text); return 1; }
        }
        if (vdf_get(text, current, 6, val, sizeof val)) {
            seen = 1;
            if (atoi(val) == appid) { free(text); return 1; }
        }
        free(text);
    }
    return seen ? 0 : -1;
}

#define MAX_GAMES 96

struct game { int appid; char name[40]; char dir[600]; };

/* Steam's own tools, which also have app manifests. */
static int is_tool(int appid, const char *name)
{
    return appid == 228980 || !strncasecmp(name, "Proton", 6) || !strncasecmp(name, "Steam Linux Runtime", 19) ||
           !strncasecmp(name, "Steamworks", 10) || !strncasecmp(name, "SteamVR", 7);
}

static void add_library(char libs[][600], int *n, const char *path)
{
    /* PATH_MAX, not less: Ubuntu builds with _FORTIFY_SOURCE, and its glibc stops a program that
     * gives realpath a smaller buffer, however short the path is. */
    char real[PATH_MAX];
    int i;
    if (*n >= 16 || !realpath(path, real) || strlen(real) >= 600) return;
    for (i = 0; i < *n; i++) if (!strcmp(libs[i], real)) return;
    snprintf(libs[(*n)++], 600, "%s", real);
}

static int find_games(struct game *games, int max)
{
    static char libs[16][600];
    const char *home = getenv("HOME");
    char path[700], key[64], val[600];
    int nlib = 0, ngames = 0, i, l;
    if (!home) return 0;
    for (i = 0; i < (int)(sizeof steam_dirs / sizeof steam_dirs[0]); i++) {
        char *text;
        struct vdf v;
        int t;
        snprintf(path, sizeof path, "%s%s", home, steam_dirs[i]);
        add_library(libs, &nlib, path);
        snprintf(path, sizeof path, "%s%s/steamapps/libraryfolders.vdf", home, steam_dirs[i]);
        if (!(text = read_text(path, 1 << 20))) continue;
        v.p = text;
        while ((t = vdf_next(&v, key, sizeof key)) != 0)
            if (t == 1 && !strcasecmp(key, "path") && vdf_next(&v, val, sizeof val) == 1) add_library(libs, &nlib, val);
        free(text);
    }
    for (l = 0; l < nlib; l++) {
        DIR *d;
        struct dirent *de;
        snprintf(path, sizeof path, "%s/steamapps", libs[l]);
        if (!(d = opendir(path))) continue;
        while ((de = readdir(d)) && ngames < max) {
            static const char *const k_id[] = {"AppState", "appid"}, *const k_name[] = {"AppState", "name"},
                                     *const k_flags[] = {"AppState", "StateFlags"};
            char file[900], *text;
            int appid, dup = 0, g;
            if (strncmp(de->d_name, "appmanifest_", 12) || !strstr(de->d_name, ".acf")) continue;
            snprintf(file, sizeof file, "%s/%s", path, de->d_name);
            if (!(text = read_text(file, 1 << 20))) continue;
            if (vdf_get(text, k_id, 2, val, sizeof val) && (appid = atoi(val)) > 0 &&
                vdf_get(text, k_flags, 2, key, sizeof key) && (atoi(key) & 4) &&     /* 4: fully installed */
                vdf_get(text, k_name, 2, val, sizeof val) && !is_tool(appid, val)) {
                for (g = 0; g < ngames; g++) if (games[g].appid == appid) dup = 1;
                if (!dup) {
                    games[ngames].appid = appid;
                    ascii_copy(games[ngames].name, sizeof games[ngames].name, val);
                    snprintf(games[ngames].dir, sizeof games[ngames].dir, "%s", libs[l]);
                    if (games[ngames].name[0]) ngames++;
                }
            }
            free(text);
        }
        closedir(d);
    }
    return ngames;
}

/* Steam's picture of a game (its library header), cut to a 16:9 thumbnail. Steam has kept
 * these in a few places over the years. */
static void steam_picture(int appid, const char *dest)
{
    const char *home = getenv("HOME");
    char path[900], dir[800];
    int i;
    for (i = 0; home && i < (int)(sizeof steam_dirs / sizeof steam_dirs[0]); i++) {
        DIR *d;
        struct dirent *de;
        const char *found = NULL;
        snprintf(dir, sizeof dir, "%s%s/appcache/librarycache", home, steam_dirs[i]);
        snprintf(path, sizeof path, "%s/%d_header.jpg", dir, appid);
        if (access(path, R_OK) == 0) found = path;
        if (!found) {
            snprintf(path, sizeof path, "%s/%d/header.jpg", dir, appid);
            if (access(path, R_OK) == 0) found = path;
        }
        if (!found) {
            snprintf(dir + strlen(dir), sizeof dir - strlen(dir), "/%d", appid);
            if ((d = opendir(dir))) {
                while (!found && (de = readdir(d)))
                    if (de->d_name[0] != '.') {
                        snprintf(path, sizeof path, "%s/%s/header.jpg", dir, de->d_name);
                        if (access(path, R_OK) == 0) found = path;
                    }
                closedir(d);
            }
        }
        if (found) {
            struct img full, thumb;
            if (picture_load_file(&full, found, 0, 0)) {
                if (picture_cover(&thumb, full.px, full.w, full.h, 320, 180)) {
                    img_save_file(&thumb, dest);
                    img_free(&thumb);
                }
                img_free(&full);
            }
            return;
        }
    }
}

static int write_if_changed(const char *path, const char *text)
{
    char old[256] = "", tmp[160];
    FILE *f = fopen(path, "r");
    size_t n;
    if (f) {
        n = fread(old, 1, sizeof old - 1, f);
        old[n] = 0;
        fclose(f);
        if (!strcmp(old, text)) return 1;
    }
    snprintf(tmp, sizeof tmp, "%s.part", path);
    if (!(f = fopen(tmp, "w"))) return 0;
    fputs(text, f);
    if (fclose(f) != 0 || rename(tmp, path) != 0) { remove(tmp); return 0; }
    return 1;
}

void plat_sync_apps(void)
{
    static struct game games[MAX_GAMES];
    char path[160], text[200];
    /* With Steam gone, its games' tiles go too (their files can stay behind in its data folder). */
    int n = plat_steam_ready() ? find_games(games, MAX_GAMES) : 0, i;
    int minecraft = 0;
    DIR *d;
    struct dirent *de;

    for (i = 0; i < n; i++) {
        snprintf(path, sizeof path, "uda:/apps/steam-%d.app", games[i].appid);
        snprintf(text, sizeof text, "name=%s\nsteam=%d\n", games[i].name, games[i].appid);
        write_if_changed(path, text);
        snprintf(path, sizeof path, "uda:/apps/steam-%d.ximg", games[i].appid);
        if (access(path, F_OK) != 0) steam_picture(games[i].appid, path);
    }
    /* Games that aren't in Steam any more, and apps from Flathub removed some other way (from
     * the desktop, say): their tiles go too. */
    if (!(d = opendir("uda:/apps"))) return;
    while ((de = readdir(d))) {
        int appid, keep = 0;
        char tail[8];
        size_t len = strlen(de->d_name);
        if (len > 4 && len < 80 && !strcmp(de->d_name + len - 4, ".app") && strncmp(de->d_name, "steam-", 6) && plat_flatpak_ready()) {
            char line[200], ref[128] = "";
            FILE *f;
            snprintf(path, sizeof path, "uda:/apps/%s", de->d_name);
            if ((f = fopen(path, "r"))) {
                while (fgets(line, sizeof line, f))
                    if (!strncmp(line, "flatpak=", 8)) sscanf(line + 8, "%127s", ref);
                fclose(f);
            }
            if (ref[0] && !plat_flatpak_installed(ref)) {
                remove(path);
                snprintf(path, sizeof path, "uda:/apps/%.*s.ximg", (int)(len - 4), de->d_name);
                remove(path);
            } else if (!strcmp(ref, PRISM_REF) && strcmp(de->d_name, "prism-launcher.app")) {
                minecraft = 1;
            }
            continue;
        }
        if (sscanf(de->d_name, "steam-%d.%7s", &appid, tail) != 2 || strcmp(tail, "app")) continue;
        for (i = 0; i < n; i++) if (games[i].appid == appid) keep = 1;
        if (keep) continue;
        snprintf(path, sizeof path, "uda:/apps/steam-%d.app", appid);
        remove(path);
        snprintf(path, sizeof path, "uda:/apps/steam-%d.ximg", appid);
        remove(path);
    }
    closedir(d);
    /* Minecraft starts straight into the game; Prism Launcher's own window, for another instance
     * or version, gets a tile of its own next to it. */
    if (minecraft) write_if_changed("uda:/apps/prism-launcher.app", "name=Prism Launcher\nflatpak=" PRISM_REF "\nlauncher=1\n");
    else if (plat_flatpak_ready()) remove("uda:/apps/prism-launcher.app");
}

/* ---------------------------------------------------------------- .app files */

/* Arguments the Store may add for a Flathub app: plain words, no options for Flatpak itself. */
static int safe_word(const char *w)
{
    if (!*w) return 0;
    for (; *w; w++)
        if (!isalnum((unsigned char)*w) && !strchr("-_=.:/+", *w)) return 0;
    return 1;
}

int linux_app_read(const char *path, struct linux_app *a, char *why, int why_len)
{
    char line[400], name[40] = "", ref[128] = "", args[200] = "", exec[300] = "", prog[256], game[24] = "";
    int steam = 0, k = 0, used = 0, pointer = 0, launcher = 0;
    char *w, *save = NULL;
    FILE *f = fopen(path, "r");

    memset(a, 0, sizeof *a);
    if (!f) {
        snprintf(why, why_len, "It isn't there any more. Has it been removed?");
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        if (!strncmp(line, "name=", 5)) ascii_copy(name, sizeof name, line + 5);
        else if (!strncmp(line, "flatpak=", 8)) snprintf(ref, sizeof ref, "%s", line + 8);
        else if (!strncmp(line, "args=", 5)) snprintf(args, sizeof args, "%s", line + 5);
        else if (!strncmp(line, "steam=", 6)) steam = atoi(line + 6);
        else if (!strncmp(line, "exec=", 5)) snprintf(exec, sizeof exec, "%s", line + 5);
        else if (!strncmp(line, "pointer=", 8)) pointer = atoi(line + 8) == 1;
        else if (!strncmp(line, "game=", 5)) ascii_copy(game, sizeof game, line + 5);
        else if (!strncmp(line, "launcher=", 9)) launcher = atoi(line + 9) == 1;
    }
    fclose(f);
    /* Launchers that only take a mouse start with the controller as the pointer. */
    a->pointer = pointer || !strcmp(ref, PRISM_REF);
    snprintf(a->game_title, sizeof a->game_title, "%s", game);

    if (steam > 0) {
        char url[48];
        snprintf(url, sizeof url, "steam://rungameid/%d", steam);
        if (!linux_steam_command(url, a)) {
            snprintf(why, why_len, "Steam isn't installed any more. Get it again from the Store.");
            return 0;
        }
        a->steam_appid = steam;
        snprintf(a->name, sizeof a->name, "%s", name[0] ? name : "The game");
        return 1;
    }
#define PUT(s) do { if (!put_arg(a, &k, &used, (s))) { snprintf(why, why_len, "Its command is too long."); return 0; } } while (0)
    if (ref[0]) {
        if (!valid_ref(ref)) {
            snprintf(why, why_len, "Its Flathub name isn't valid.");
            return 0;
        }
        if (!linux_find_program("flatpak", prog, sizeof prog)) {
            snprintf(why, why_len, "Flatpak isn't on this machine any more. Run the XenithOS install command once more.");
            return 0;
        }
        /* --device=all: the controllers too. Apps that ask Flatpak for input devices with the
         * newer --device=input (Flatpak 1.15.6) get none on Debian 12's or Ubuntu 22.04's. */
        PUT(prog); PUT("run"); PUT("--device=all"); PUT(ref);
        for (w = strtok_r(args, " ", &save); w; w = strtok_r(NULL, " ", &save))
            if (safe_word(w)) PUT(w);
        a->steam = !strcmp(ref, STEAM_REF);
        if (!strcmp(ref, PRISM_REF)) {
            /* Minecraft like on a console: straight into the instance played last (Prism
             * Launcher's own window only when there's none yet), and the controller plays it
             * as a keyboard and mouse once Minecraft's window is up. The "Prism Launcher" tile
             * (launcher=1, made by plat_sync_apps) opens the launcher itself. */
            char inst[200];
            snprintf(a->game_title, sizeof a->game_title, "Minecraft");
            if (!launcher && prism_last_instance(inst, sizeof inst)) { PUT("--launch"); PUT(inst); }
        }
    } else if (exec[0]) {
        for (w = strtok_r(exec, " ", &save); w; w = strtok_r(NULL, " ", &save)) {
            if (k == 0 && w[0] != '/') {
                if (!linux_find_program(w, prog, sizeof prog)) {
                    snprintf(why, why_len, "%s isn't installed.", w);
                    return 0;
                }
                PUT(prog);
            } else {
                PUT(w);
            }
        }
    } else {
        snprintf(why, why_len, "It doesn't say what to run.");
        return 0;
    }
#undef PUT
    if (!k) {
        snprintf(why, why_len, "It doesn't say what to run.");
        return 0;
    }
    snprintf(a->name, sizeof a->name, "%s", name[0] ? name : "The app");
    return 1;
}

/* ------------------------------------------------- XenithOS's own apps (PCs) */

/* Where XenithOS's programs are: /usr/lib/xenithos (XENITHOS_LIB for tests), as install.sh and
 * update.sh leave them, with each app's name and picture next to it (<id>.txt, <id>.ximg). */
static const char *local_lib(void)
{
    const char *l = getenv("XENITHOS_LIB");
    return l && *l ? l : "/usr/lib/xenithos";
}

static int local_id_ok(const char *id)
{
    int i;
    if (!id || !id[0] || strlen(id) > 24) return 0;
    for (i = 0; id[i]; i++)
        if (!islower((unsigned char)id[i]) && !isdigit((unsigned char)id[i]) && id[i] != '-' && id[i] != '_') return 0;
    return 1;
}

int plat_local_app(const char *id)
{
    char path[PATH_MAX];
    if (!local_id_ok(id)) return 0;
    snprintf(path, sizeof path, "%s/%s", local_lib(), id);
    return access(path, X_OK) == 0;
}

int plat_local_app_file(const char *id, const char *ext, char *out, int n)
{
    /* Next to the program (update.sh keeps those current), where install.sh and XenithOS
     * Desktop's packages put them, the copy kept when it was taken off the home screen, and
     * the home screen's own. */
    static const char *const dirs[] = {NULL, "/usr/share/xenithos/apps", "optional-apps", "uda:/apps"};
    int i;
    if (!local_id_ok(id) || (strcmp(ext, "txt") && strcmp(ext, "ximg"))) return 0;
    for (i = 0; i < (int)(sizeof dirs / sizeof dirs[0]); i++) {
        snprintf(out, n, "%s/%s.%s", dirs[i] ? dirs[i] : local_lib(), id, ext);
        if (access(out, R_OK) == 0) return 1;
    }
    return 0;
}

static int copy_small_file(const char *from, const char *to)
{
    char buf[8192];
    size_t got;
    int ok = 1;
    FILE *in = fopen(from, "rb"), *out;
    if (!in) return 0;
    if (!(out = fopen(to, "wb"))) {
        fclose(in);
        return 0;
    }
    while ((got = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, got, out) != got) {
            ok = 0;
            break;
        }
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    if (!ok) unlink(to);
    return ok;
}

/* XenithOS Desktop keeps the list of console apps you chose itself (Settings > Console apps).
 * Its tool puts them on the home screen and takes them off, so the Store and Settings agree. */
#define DESKTOP_CONSOLE_APPS "/usr/lib/xenithos-desktop/console-apps"

int plat_local_app_set(const char *id, int on)
{
    static const char *const exts[] = {"txt", "ximg"};
    char link[96], prog[PATH_MAX], from[PATH_MAX], to[96];
    int i;
    if (!plat_local_app(id)) return 0;
    if (access(DESKTOP_CONSOLE_APPS, X_OK) == 0) {
        char *argv[] = {DESKTOP_CONSOLE_APPS, on ? "install" : "remove", (char *)id, NULL};
        int status;
        pid_t pid = linux_spawn(argv, NULL, NULL, 0);
        if (pid <= 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return 0;
        if (WEXITSTATUS(status) == 0) return 1;
        if (WEXITSTATUS(status) != 2) return 0;   /* 2: not one Desktop lets you choose, so it's done here */
    }
    snprintf(link, sizeof link, "uda:/apps/%s.elf32", id);
    if (!on) {
        /* Off the home screen. Its name and picture are kept, for when it comes back. */
        unlink(link);
        mkdir("optional-apps", 0755);
        for (i = 0; i < 2; i++) {
            snprintf(from, sizeof from, "uda:/apps/%s.%s", id, exts[i]);
            snprintf(to, sizeof to, "optional-apps/%s.%s", id, exts[i]);
            if (rename(from, to) != 0) unlink(from);
        }
        return access(link, F_OK) != 0;
    }
    for (i = 0; i < 2; i++) {
        snprintf(to, sizeof to, "uda:/apps/%s.%s", id, exts[i]);
        if (access(to, F_OK) != 0 && plat_local_app_file(id, exts[i], from, sizeof from)) copy_small_file(from, to);
    }
    snprintf(prog, sizeof prog, "%s/%s", local_lib(), id);
    unlink(link);                                    /* one that pointed somewhere else */
    if (symlink(prog, link) != 0) return 0;
    /* Installed again: no longer kept aside (install.sh leaves alone what it finds there). */
    for (i = 0; i < 2; i++) {
        snprintf(to, sizeof to, "optional-apps/%s.%s", id, exts[i]);
        unlink(to);
    }
    return 1;
}
