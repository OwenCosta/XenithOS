/* Crash reports (shared/crash.h): what XenithOS does with the crash files it finds, the next time
 * it starts and each time it's back from an app.
 *
 * Each becomes a report for XenithOS Cloud (POST /v1/crash): the program, its version, what this
 * machine is, what stopped it (the signal or exception), where (the first place in XenithOS's own
 * code, when the file names one) and the file itself, cleaned up first:
 *   paths outside the system's own folders (someone's home folder, a USB drive) are cut down to
 *   the file's own name, except XenithOS's own source files, which keep the part from apps/,
 *   source/, shared/, linux/ or sim/ on (".../src/linux/../apps/hostmc/source/main.c" is sent as
 *   "apps/hostmc/source/main.c");
 *   the gamertag goes only when the person picks "Send it with my gamertag".
 * Settings > Send crash reports: "Always" sends each one by itself when XenithOS is online (if it
 * isn't, the file waits for next time), with a toast to say so; "Ask first", as it starts out,
 * asks once a crash. A crash file goes once it's sent, or once the person says not to send it. */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "apps.h"
#include "audio.h"
#include "config.h"
#include "crash.h"
#include "icons.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

#define FILE_MAX   16384              /* what's read of a crash file */
#define REPORT_MAX 8000               /* what's sent of one; XenithOS Cloud keeps up to 12000 */
#define MAX_FILES  6                  /* handled at a time; the rest wait */

struct crash {
    char file[64];                    /* its name in CRASH_DIR */
    char program[40], version[24], what[64], where[160];
    char *report;                     /* the file, cleaned up */
};

/* ------------------------------------------------------------ cleaning up */

static int path_char(char c)
{
    return isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-' || c == '+' || c == '~' || c == '/' || c == ':';
}

/* "/a/b/../c/./d" is "/a/c/d", in place. */
static void normalize(char *p)
{
    char *out = p, *seg = p;
    while (*seg) {
        char *end;
        size_t len;
        while (*seg == '/') seg++;
        if (!*seg) break;
        end = strchr(seg, '/');
        len = end ? (size_t)(end - seg) : strlen(seg);
        if (len == 1 && seg[0] == '.') {
            /* nothing */
        } else if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            while (out > p && *--out != '/') { }
        } else {
            *out++ = '/';
            memmove(out, seg, len);
            out += len;
        }
        seg += len;
    }
    if (out == p) *out++ = '/';
    *out = 0;
}

/* One absolute path, as it's sent. */
static void clean_path(char *p, char *out, int n)
{
    static const char *const ours[] = {"/apps/", "/source/", "/shared/", "/linux/", "/sim/", "/desktop/"};
    static const char *const system[] = {"/usr/", "/lib/", "/lib32/", "/lib64/", "/libx32/", "/opt/", "/bin/",
                                         "/sbin/", "/etc/", "/proc/", "/sys/", "/dev/", "/snap/"};
    const char *best = NULL, *slash;
    int i;
    normalize(p);
    for (i = 0; i < (int)(sizeof ours / sizeof ours[0]); i++) {
        const char *at = strstr(p, ours[i]);
        if (at && (!best || at < best)) best = at;
    }
    if (best) {                                   /* XenithOS's own source or programs */
        snprintf(out, n, "%s", best + 1);
        return;
    }
    for (i = 0; i < (int)(sizeof system / sizeof system[0]); i++)
        if (!strncmp(p, system[i], strlen(system[i]))) {
            snprintf(out, n, "%s", p);
            return;
        }
    slash = strrchr(p, '/');                      /* anywhere else: only the file's own name */
    snprintf(out, n, "%s", slash && slash[1] ? slash + 1 : "?");
}

/* The whole file: printable ASCII and newlines only, every absolute path cleaned. A path is a
 * '/' with nothing path-like before it, up to the first character that can't be in one (a ':'
 * only when a '/' follows, as in "uda:/", so "main.c:12" keeps its line number apart). */
static char *clean_text(const char *in)
{
    size_t len = strlen(in);
    char *out = malloc(len + 1), *o = out;
    const char *s = in;
    if (!out) return NULL;
    while (*s) {
        if (*s == '/' && (s == in || !path_char(s[-1]))) {
            char path[512], done[512];
            int k = 0;
            for (; *s && path_char(*s) && (*s != ':' || s[1] == '/'); s++)
                if (k < (int)sizeof path - 1) path[k++] = *s;    /* a longer one loses its end */
            path[k] = 0;
            clean_path(path, done, sizeof done);
            k = (int)strlen(done);
            memcpy(o, done, (size_t)k);         /* never longer than what it replaces */
            o += k;
            continue;
        }
        if (*s == '\n' || (*s >= 32 && *s < 127)) *o++ = *s;
        else if (*s != '\r') *o++ = '?';
        s++;
    }
    *o = 0;
    return out;
}

/* ------------------------------------------------------------- what, where */

static int line_value(const char *text, const char *key, char *out, int n)
{
    kv_get(text, key, out, n);
    return out[0] != 0;
}

/* What stopped it, in a few words: the signal or exception, and the C library's own message when
 * it was the one that stopped it ("*** buffer overflow detected ***: terminated" is "buffer
 * overflow detected"), or what its functions on the stack or the kernel say. */
static void find_what(struct crash *c)
{
    char sig[24] = "", code[48] = "", message[96] = "", *words, *end;
    const char *why = "";
    if (line_value(c->report, "signal", sig, sizeof sig)) {
        line_value(c->report, "code", code, sizeof code);
        words = strchr(code, ' ');
        if (line_value(c->report, "message", message, sizeof message)) {
            if ((end = strstr(message, " ***")) || (end = strstr(message, ": terminated"))) *end = 0;
            why = strncmp(message, "*** ", 4) ? message : message + 4;
        } else if (strstr(c->report, "__fortify_fail") || strstr(c->report, "__chk_fail")) why = "a buffer overflow check";
        else if (strstr(c->report, "__stack_chk_fail")) why = "stack smashing detected";
        else if (strstr(c->report, "__assert_fail")) why = "a failed assert";
        else if (strstr(c->report, "malloc_printerr")) why = "damaged heap memory";
        else if (words) why = words + 1;
        snprintf(c->what, sizeof c->what, "%s%s%s", sig, why[0] ? ", " : "", why);
    } else if (line_value(c->report, "exception", sig, sizeof sig)) {
        if (!strcmp(sig, "segfault")) snprintf(c->what, sizeof c->what, "segfault, a bad pointer");
        else snprintf(c->what, sizeof c->what, "%s exception", sig);
    } else {
        snprintf(c->what, sizeof c->what, "crash");
    }
}

/* The first line addr2line gave after "--- where" (the place it stopped, or the first place in
 * the program on the way there): "0x1a2b: main at apps/hostmc/source/main.c:1349" without its
 * address. Without one, where it stopped as an address. */
static void find_where(struct crash *c)
{
    const char *p = strstr(c->report, "--- where\n");
    char pc[40];
    c->where[0] = 0;
    if (p) {
        for (p += 10; *p && strncmp(p, "--- ", 4); ) {
            const char *e = strchr(p, '\n'), *text = p;
            int n = e ? (int)(e - p) : (int)strlen(p);
            const char *colon = memchr(p, ':', (size_t)n);
            if (!strncmp(p, "0x", 2) && colon && colon[1] == ' ') {
                n -= (int)(colon + 2 - p);
                text = colon + 2;
            }
            if (n > 0 && strncmp(text, " (inlined by)", 13) && strncmp(text, "?? ??", 5)) {
                snprintf(c->where, sizeof c->where, "%.*s", n, text);
                return;
            }
            if (!e) break;
            p = e + 1;
        }
    }
    if (line_value(c->report, "pc", pc, sizeof pc)) snprintf(c->where, sizeof c->where, "pc %s", pc);
}

/* ---------------------------------------------------------------- reading */

static int load(const char *file, struct crash *c)
{
    char path[128], *raw, *dot;
    size_t n;
    FILE *f;
    memset(c, 0, sizeof *c);
    snprintf(c->file, sizeof c->file, "%s", file);
    snprintf(path, sizeof path, CRASH_DIR "/%s", file);
    if (!(f = fopen(path, "rb"))) return 0;
    raw = malloc(FILE_MAX + 1);
    n = raw ? fread(raw, 1, FILE_MAX, f) : 0;
    fclose(f);
    if (!raw) return 0;
    raw[n] = 0;
    c->report = clean_text(raw);
    free(raw);
    if (!c->report) return 0;
    if (strlen(c->report) > REPORT_MAX) {         /* cut at a line */
        char *cut = c->report + REPORT_MAX;
        while (cut > c->report && cut[-1] != '\n') cut--;
        *cut = 0;
    }
    if (!line_value(c->report, "program", c->program, sizeof c->program)) {
        snprintf(c->program, sizeof c->program, "%s", file);
        if ((dot = strchr(c->program, '.'))) *dot = 0;
    }
    if (!line_value(c->report, "version", c->version, sizeof c->version)) snprintf(c->version, sizeof c->version, "?");
    find_what(c);
    find_where(c);
    return 1;
}

static void forget(const struct crash *c)
{
    char path[128];
    snprintf(path, sizeof path, CRASH_DIR "/%s", c->file);
    remove(path);
}

/* "XenithOS", or the app's name as the home screen shows it. */
static void display_name(const char *program, char *out, int n)
{
    static const char *const ids[] = {"xenithos", "volt", "doom", "tube", "photos", "hostmc"};
    static const char *const names[] = {"XenithOS", "VOLT", "DOOM", "Tube", "Photos", "HostMC"};
    int i;
    for (i = 0; i < napps; i++)
        if (!strcasecmp(apps[i].id, program) && apps[i].name[0]) {
            snprintf(out, n, "%s", apps[i].name);
            return;
        }
    for (i = 0; i < (int)(sizeof ids / sizeof ids[0]); i++)
        if (!strcmp(program, ids[i])) {
            snprintf(out, n, "%s", names[i]);
            return;
        }
    snprintf(out, n, "%s", program);
    if (out[0]) out[0] = (char)toupper((unsigned char)out[0]);
}

/* ---------------------------------------------------------------- sending */

/* &key=value, form-encoded, onto form (cap bytes in all). */
static void form_add(char *form, size_t cap, const char *key, const char *val)
{
    size_t len = strlen(form);
    int used = snprintf(form + len, cap - len, "%s%s=", len ? "&" : "", key);
    if (used < 0 || (size_t)used >= cap - len) return;
    net_escape(val, form + len + used, (int)(cap - len - (size_t)used));
}

static int send_report(const struct crash *c, int with_gamertag, struct http_resp *r)
{
    char platform[40];
    size_t cap = strlen(c->report) * 3 + 1024;
    char *form = malloc(cap);
    int code;
    memset(r, 0, sizeof *r);
    if (!form) return NE_MEMORY;
    if (plat_kind() == PLAT_XBOX) snprintf(platform, sizeof platform, "Xbox 360");
    else snprintf(platform, sizeof platform, "PC, %s", plat_arch()[0] ? plat_arch() : "Linux");
    form[0] = 0;
    form_add(form, cap, "program", c->program);
    form_add(form, cap, "version", c->version);
    form_add(form, cap, "platform", platform);
    form_add(form, cap, "what", c->what);
    form_add(form, cap, "where", c->where);
    form_add(form, cap, "report", c->report);
    if (with_gamertag && prof.signed_in) form_add(form, cap, "token", prof.token);
    code = cloud_post("/v1/crash", form, r);
    free(form);
    return code;
}

/* Sent, or refused for good (anything but "too many", which passes): the file can go. */
static int done_with(int code)
{
    return code == 200 || (code >= 400 && code < 500 && code != 429);
}

static void handle(struct crash *c, int at_start)
{
    char name[48], title[96];
    struct http_resp r;
    int code;

    display_name(c->program, name, sizeof name);
    snprintf(title, sizeof title, "%s stopped working%s", name,
             at_start || !strcmp(c->program, "xenithos") ? " last time" : "");

    if (st.crash_reports) {                       /* Settings: always send */
        if (!cfg.server[0] || !net_is_up()) {
            if (!at_start) {
                ui_toast_icon(ICON_INFO);
                ui_toast(title, "Its crash report goes next time XenithOS is online.");
            }
            return;
        }
        ui_busy_begin("Sending a crash report", name);
        code = send_report(c, 0, &r);
        ui_busy_end();
        http_free(&r);
        if (done_with(code)) forget(c);
        ui_toast_icon(ICON_INFO);
        ui_toast(title, code == 200 ? "A crash report went to the people who make XenithOS."
                                    : "Its crash report didn't send. XenithOS tries again next time.");
        return;
    }

    {
        const char *opts[3];
        int n = 0, send = -1, tagged = -1, choice;
        opts[send = n++] = "Send the report";
        if (prof.signed_in) opts[tagged = n++] = "Send it with my gamertag";
        opts[n++] = "Don't send it";
        choice = ui_dialog(title,
                           "A crash report tells the people who make XenithOS where it stopped, so it can be fixed. "
                           "It has the version, the app and what it was doing, and nothing about you. Settings can "
                           "send them without asking.", opts, n);
        if (choice != send && choice != tagged) {
            forget(c);
            return;
        }
        if (!cloud_ready("Crash report")) return; /* asked again next time */
        ui_busy_begin("Sending", NULL);
        code = send_report(c, choice == tagged, &r);
        ui_busy_end();
        if (code == 200) {
            forget(c);
            sfx(SFX_SUCCESS);
            ui_toast_icon(ICON_CHECK);
            ui_toast("Sent. Thank you!", "The crash report is on its way.");
        } else {
            if (done_with(code)) forget(c);
            sfx(SFX_ERROR);
            ui_message("It didn't send", cloud_error_text(&r, code));
        }
        http_free(&r);
    }
}

void crash_check(int at_start)
{
    char files[MAX_FILES][64];
    int n = 0, i;
    DIR *d = opendir(CRASH_DIR);
    struct dirent *de;
    if (!d) return;
    while ((de = readdir(d)) && n < MAX_FILES) {
        size_t len = strlen(de->d_name);
        /* "<program>.txt"; ".<program>.tmp" is one still being written (or never finished) */
        if (de->d_name[0] == '.' || len < 5 || len >= sizeof files[0] || strcasecmp(de->d_name + len - 4, ".txt"))
            continue;
        snprintf(files[n++], sizeof files[0], "%s", de->d_name);
    }
    closedir(d);
    for (i = 0; i < n; i++) {
        struct crash c;
        if (!load(files[i], &c)) continue;
        handle(&c, at_start);
        free(c.report);
    }
}
