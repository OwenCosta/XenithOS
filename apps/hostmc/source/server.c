/* The Minecraft server process.
 *
 * It's started detached (its own session, and a double fork so init adopts it), so it keeps
 * running after HostMC quits or XenithOS closes HostMC from the guide, while the user goes off
 * to play on it. hostmc/server/hostmc.pid says which process it is: its PID and start time
 * (so a PID the system has since reused isn't mistaken for it), and HostMC checks the process
 * runs in the server folder before believing it or sending it a signal.
 *
 * Its output goes to hostmc/server/console.log, which HostMC reads as it grows: that's the
 * console, and where the state (starting, running) and the players online come from. Commands
 * go in through hostmc/server/console.in, a named pipe that is the server's standard input.
 * The server holds it open for writing too, so it never sees the end of its input when HostMC
 * goes away, and HostMC can open it again any time to type.
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "hostmc.h"
#include "icons.h"
#include "ui.h"

#define PIDFILE HM_SERVER "/hostmc.pid"
#define LOGFILE HM_SERVER "/console.log"
#define FIFO    HM_SERVER "/console.in"

struct hm_server sv;

static int replaying;                  /* reading an old log back: no sounds or toasts */
static char partial[LOG_LEN * 2];      /* a line still being written */
static int npartial;

/* ------------------------------------------------------------ the process */

/* 1 if pid is the server HostMC started: alive, started when recorded, in the server folder. */
static int ours(pid_t pid, unsigned long long ticks)
{
    char dir[720];
    snprintf(dir, sizeof dir, "%s/server", hm_root());
    return proc_ours(pid, ticks, dir);
}

static void write_pidfile(void)
{
    char buf[128];
    int n = snprintf(buf, sizeof buf, "pid=%d\nticks=%llu\nstarted=%lld\n", (int)sv.pid, sv.ticks, sv.started);
    file_write(PIDFILE, buf, n, 0644);
}

static int read_pidfile(void)
{
    char *t = file_read(PIDFILE, NULL), *p;
    if (!t) return 0;
    sv.pid = (p = strstr(t, "pid=")) ? (pid_t)atoi(p + 4) : 0;
    sv.ticks = (p = strstr(t, "ticks=")) ? strtoull(p + 6, NULL, 10) : 0;
    sv.started = (p = strstr(t, "started=")) ? atoll(p + 8) : 0;
    free(t);
    return sv.pid > 1;
}

/* ---------------------------------------------------------------- console */

static void add_line(const char *text, int kind)
{
    struct hm_line *l;
    if (sv.nlines < LOG_KEEP) l = &sv.lines[(sv.first + sv.nlines++) % LOG_KEEP];
    else { l = &sv.lines[sv.first]; sv.first = (sv.first + 1) % LOG_KEEP; }
    snprintf(l->text, sizeof l->text, "%s", text);
    l->kind = (uint8_t)kind;
    sv.seq++;
}

const struct hm_line *sv_line(int i)
{
    if (i < 0 || i >= sv.nlines) return NULL;
    return &sv.lines[(sv.first + i) % LOG_KEEP];
}

void sv_note(const char *text)
{
    char line[LOG_LEN];
    snprintf(line, sizeof line, "HostMC: %s", text);
    add_line(line, LN_HOSTMC);
}

/* Plain ASCII for XenithOS's font: colour codes (ANSI and Minecraft's section sign) go, other
 * characters become '?'. */
static void clean(const char *in, char *out, int n)
{
    const unsigned char *s = (const unsigned char *)in;
    int k = 0;
    while (*s && k < n - 1) {
        if (*s == 0x1b) {                                   /* ESC [ ... letter */
            s++;
            if (*s == '[') { s++; while (*s && !isalpha(*s)) s++; if (*s) s++; }
            continue;
        }
        if (s[0] == 0xC2 && s[1] == 0xA7) {                 /* the section sign and its code */
            s += 2;
            if (*s) s += (*s & 0x80) ? 2 : 1;
            continue;
        }
        if (*s == '\t') { out[k++] = ' '; s++; continue; }
        if (*s < 32 || *s == 127) { s++; continue; }
        if (*s >= 0x80) {                                   /* one '?' per UTF-8 character */
            out[k++] = '?';
            s++;
            while ((*s & 0xC0) == 0x80) s++;
            continue;
        }
        out[k++] = (char)*s++;
    }
    out[k] = 0;
}

/* "[12:00:01] [Server thread/INFO]: Done (...)" (Minecraft) and "[12:00:01 INFO]: Done (...)"
 * (Paper): the message after the first "]: ". */
static const char *message_of(const char *line)
{
    const char *m = line[0] == '[' ? strstr(line, "]: ") : NULL;
    return m ? m + 3 : line;
}

/* Minecraft 26 logs what it says to everyone (joins, answers to commands) as "System chat: ...". */
static const char *said(const char *msg)
{
    return strncmp(msg, "System chat: ", 13) ? msg : msg + 13;
}

static int valid_name(const char *s, int len)
{
    int i;
    if (len < 1 || len > 16 || s[0] == '<' || s[0] == '[') return 0;
    for (i = 0; i < len; i++)
        if (s[i] <= ' ' || s[i] > '~') return 0;
    return 1;
}

static int player_index(const char *name)
{
    int i;
    for (i = 0; i < sv.nplayers; i++)
        if (!strcmp(sv.players[i], name)) return i;
    return -1;
}

static void player_add(const char *name)
{
    if (player_index(name) >= 0 || sv.nplayers >= MAX_PLAYERS) return;
    snprintf(sv.players[sv.nplayers++], sizeof sv.players[0], "%s", name);
}

static void player_remove(const char *name)
{
    int i = player_index(name);
    if (i < 0) return;
    memmove(&sv.players[i], &sv.players[i + 1], sizeof sv.players[0] * (size_t)(sv.nplayers - i - 1));
    sv.nplayers--;
}

/* "NAME joined the game": the name, when the whole message is exactly that. */
static int event_name(const char *msg, const char *tail, char *name)
{
    size_t ml = strlen(msg), tl = strlen(tail);
    int len;
    if (ml <= tl || strcmp(msg + ml - tl, tail)) return 0;
    len = (int)(ml - tl);
    if (!valid_name(msg, len)) return 0;
    memcpy(name, msg, (size_t)len);
    name[len] = 0;
    return 1;
}

/* Why the server stopped on its own, from what it said. */
static void problem_from(const char *msg)
{
    if (strstr(msg, "UnsupportedClassVersionError") || strstr(msg, "has been compiled by a more recent version of the Java"))
        snprintf(sv.why, sizeof sv.why, "This server needs a newer Java than the one it was started with.");
    else if (strstr(msg, "FAILED TO BIND TO PORT") || strstr(msg, "Address already in use"))
        snprintf(sv.why, sizeof sv.why, "Port %d is already taken. Is another Minecraft server running on this PC?", server_port());
    else if (strstr(msg, "Could not reserve enough space") || strstr(msg, "OutOfMemoryError") || strstr(msg, "Cannot allocate memory"))
        snprintf(sv.why, sizeof sv.why, "The server ran out of memory. Give it more in Settings, or less if the PC is short of it.");
    else if (strstr(msg, "You need to agree to the EULA"))
        snprintf(sv.why, sizeof sv.why, "The server says the EULA hasn't been agreed to.");
    else if (strstr(msg, "Failed to load properties") || strstr(msg, "Failed to start the minecraft server"))
        snprintf(sv.why, sizeof sv.why, "The server couldn't start. The console says why.");
    else if (strstr(msg, "Encountered an unexpected exception") || strstr(msg, "This crash report has been saved"))
        snprintf(sv.why, sizeof sv.why, "The server crashed. The console has the details.");
    else if (strstr(msg, "HostMC: the program couldn't start"))
        snprintf(sv.why, sizeof sv.why, "Java couldn't start. Try starting the server again.");
}

static void take_line(const char *raw)
{
    char line[LOG_LEN], name[20];
    const char *msg;
    int kind = LN_INFO, head;
    clean(raw, line, sizeof line);
    if (!line[0]) return;
    msg = message_of(line);
    head = (int)(msg - line);
    if (head > 0) {
        char pre[64];
        snprintf(pre, sizeof pre, "%.*s", head < 63 ? head : 63, line);
        if (strstr(pre, "WARN]")) kind = LN_WARN;
        else if (strstr(pre, "ERROR]") || strstr(pre, "FATAL]")) kind = LN_ERROR;
    } else if (strstr(line, "Exception") || strstr(line, "Error:") || !strncmp(line, " at ", 4)) {   /* clean() made the tab a space */
        kind = LN_ERROR;
    }
    msg = said(msg);
    if (msg[0] == '<' || !strncmp(msg, "[Not Secure] <", 14)) kind = LN_CHAT;

    if (!strncmp(msg, "Done (", 6) && strstr(msg, ")! For help")) {
        if (sv.state == SV_STARTING) {
            sv.state = SV_RUNNING;
            if (!replaying) {
                char addr[48], body[96];
                sfx(SFX_SUCCESS);
                if (lan_address(addr, sizeof addr)) snprintf(body, sizeof body, "Join at localhost here, or %s at home", addr);
                else snprintf(body, sizeof body, "Join at localhost on this PC");
                ui_toast_icon(ICON_SERVER);
                ui_toast("Your server is running", body);
                pi_server_started();
                vps_server_started();
                relay_server_started();
            }
        }
    } else if (event_name(msg, " joined the game", name)) {
        kind = LN_JOIN;
        player_add(name);
        if (!replaying && sv.state != SV_STOPPED) {
            char t[48];
            snprintf(t, sizeof t, "%s joined", name);
            sfx(SFX_NOTIFY);
            ui_toast_icon(ICON_PROFILE);
            ui_toast(t, "They're on your server now");
        }
    } else if (event_name(msg, " left the game", name)) {
        kind = LN_JOIN;
        player_remove(name);
    } else if (!strncmp(msg, "There are ", 10) && strstr(msg, " players online:")) {
        /* The answer to "list": who's on, which also puts right anything missed. */
        const char *p = strstr(msg, " players online:") + 16;
        sv.nplayers = 0;
        for (;;) {
            const char *e, *t;
            while (*p == ' ' || *p == ',') p++;
            if (!*p) break;
            e = p + strcspn(p, ",");
            for (t = e; t > p && t[-1] == ' '; t--) { }
            if (valid_name(p, (int)(t - p))) {
                memcpy(name, p, (size_t)(t - p));
                name[t - p] = 0;
                player_add(name);
            }
            p = e;
        }
    } else if (!strncmp(msg, "Stopping server", 15) || !strncmp(msg, "Stopping the server", 19)) {
        if (sv.state == SV_RUNNING || sv.state == SV_STARTING) sv.state = SV_STOPPING;
    } else if (strstr(msg, "Saved the game")) {
        sv.saves++;
    } else if (kind == LN_ERROR || kind == LN_WARN || head == 0 || sv.state != SV_RUNNING) {
        problem_from(line);
    }
    add_line(line, kind);
}

/* Reads what the server has written since last time (a chunk at a time). */
static void read_log(void)
{
    char buf[16384];
    FILE *f = fopen(LOGFILE, "rb");
    long long size;
    size_t n, i;
    int rounds = 0;
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (size < sv.log_off) { sv.log_off = 0; npartial = 0; }      /* a new log */
    fseek(f, (long)sv.log_off, SEEK_SET);
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && rounds++ < (replaying ? 100000 : 16)) {
        for (i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                partial[npartial] = 0;
                if (npartial && partial[npartial - 1] == '\r') partial[npartial - 1] = 0;
                take_line(partial);
                npartial = 0;
            } else if (npartial < (int)sizeof partial - 1) {
                partial[npartial++] = c;
            }
        }
        sv.log_off += (long long)n;
    }
    fclose(f);
}

/* ------------------------------------------------------------------ states */

static void stopped(void)
{
    int asked = sv.stop_asked;
    sv.state = SV_STOPPED;
    sv.nplayers = 0;
    sv.stop_asked = 0;
    read_log();                                   /* its last words */
    unlink(PIDFILE);
    pi_server_stopped();
    vps_server_stopped();
    relay_server_stopped();
    door_server_stopped();
    if (asked) {
        sv.why[0] = 0;
        sv_note("the server stopped.");
        if (!sv.restart) {
            ui_toast_icon(ICON_SERVER);
            ui_toast("The server stopped", "The world is saved");
        }
    } else {
        if (!sv.why[0]) snprintf(sv.why, sizeof sv.why, "The server stopped by itself. The console says why.");
        sv_note("the server stopped by itself.");
        sfx(SFX_ERROR);
        ui_toast_icon(ICON_SERVER);
        ui_toast("The server stopped", "It wasn't asked to. See the Server page");
        sv.restart = 0;
    }
}

void sv_attach(void)
{
    struct stat s;
    long long size = file_size(LOGFILE);
    memset(&sv, 0, sizeof sv);
    replaying = 1;
    if (read_pidfile() && ours(sv.pid, sv.ticks)) {
        sv.state = SV_STARTING;
        /* A long-running server's log can be big: the last 2 MB say enough. */
        if (size > 2 * 1024 * 1024) {
            sv.log_off = size - 2 * 1024 * 1024;
            sv.state = SV_RUNNING;
        }
        read_log();
        npartial = 0;
        replaying = 0;
        sv_note("this server was already running. HostMC has picked it up again.");
        if (sv.state == SV_RUNNING) sv_command("list");     /* who's on right now */
        return;
    }
    /* Not running: the last session's console is still worth showing. */
    if (stat(PIDFILE, &s) == 0) unlink(PIDFILE);
    sv.pid = 0;
    if (size > 256 * 1024) sv.log_off = size - 256 * 1024;
    read_log();
    npartial = 0;
    replaying = 0;
    sv.state = SV_STOPPED;
    sv.nplayers = 0;
    sv.why[0] = 0;
}

void sv_poll(void)
{
    static uint64_t next;
    uint64_t now = ui_ms();
    if (now < next) return;
    next = now + 250;
    if (sv.state == SV_STOPPED) return;
    read_log();
    if (!ours(sv.pid, sv.ticks)) {
        stopped();
        return;
    }
    /* Asked to stop and still going after 20 s: it's sent SIGTERM (the server saves on that). */
    if (sv.stop_asked && sv.stop_at && now - sv.stop_at > 20000 && sv.stop_asked == 1) {
        kill(sv.pid, SIGTERM);
        sv.stop_asked = 2;
    }
}

int sv_command(const char *cmd)
{
    char buf[300], line[LOG_LEN];
    int fd, n, ok;
    if (sv.state == SV_STOPPED) return 0;
    fd = open(FIFO, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return 0;
    n = snprintf(buf, sizeof buf, "%s\n", cmd);
    ok = write(fd, buf, (size_t)n) == n;
    close(fd);
    if (ok) {
        snprintf(line, sizeof line, "> %s", cmd);
        add_line(line, LN_SENT);
    }
    return ok;
}

void sv_stop(void)
{
    if (sv.state == SV_STOPPED) return;
    /* While it's starting it doesn't take commands yet; SIGTERM makes it stop and save. */
    if (sv.state == SV_STARTING || !sv_command("stop")) {
        if (ours(sv.pid, sv.ticks)) kill(sv.pid, SIGTERM);
    }
    sv.state = SV_STOPPING;
    sv.stop_asked = 1;
    sv.stop_at = ui_ms();
}

void sv_force_stop(void)
{
    if (sv.state == SV_STOPPED || !ours(sv.pid, sv.ticks)) return;
    kill(sv.pid, SIGKILL);
    sv_note("the server was made to stop. Anything since its last save is lost.");
}

int sv_wait_saved(unsigned before, int seconds)
{
    uint64_t until = ui_ms() + (uint64_t)seconds * 1000;
    while (ui_ms() < until) {
        sv_poll();
        if (sv.saves > before) return 1;
        if (sv.state == SV_STOPPED) return 0;
        ui_loading_frame("Saving the world", "The server writes everything down first");
        ui_poll();
    }
    return 0;
}

/* ---------------------------------------------------------------- starting */

int sv_start(char *why, int n)
{
    const char *java = java_path(hi.java);
    char xms[24], xmx[24], dir[720], fifo_abs[740], log_abs[740];
    char *argv[8];
    int log_fd, in_fd;
    pid_t server;
    struct stat s;

    if (!java) { snprintf(why, n, "Java %d isn't here yet.", hi.java); return 0; }
    snprintf(dir, sizeof dir, "%s/server", hm_root());
    snprintf(fifo_abs, sizeof fifo_abs, "%s/console.in", dir);
    snprintf(log_abs, sizeof log_abs, "%s/console.log", dir);
    if (stat(fifo_abs, &s) == 0 && !S_ISFIFO(s.st_mode)) unlink(fifo_abs);
    if (stat(fifo_abs, &s) != 0 && mkfifo(fifo_abs, 0600) != 0) {
        snprintf(why, n, "HostMC couldn't make the server's command line (%s).", strerror(errno));
        return 0;
    }
    rename(LOGFILE, HM_SERVER "/console-before.log");
    log_fd = open(log_abs, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    in_fd = open(fifo_abs, O_RDWR);                  /* a reader and a writer: it never ends */
    if (log_fd < 0 || in_fd < 0) {
        if (log_fd >= 0) close(log_fd);
        if (in_fd >= 0) close(in_fd);
        snprintf(why, n, "HostMC couldn't set the server up (%s).", strerror(errno));
        return 0;
    }
    /* It starts with up to 1 GB and grows into the amount chosen. */
    snprintf(xms, sizeof xms, "-Xms%dM", hs.ram_mb < 1024 ? hs.ram_mb : 1024);
    snprintf(xmx, sizeof xmx, "-Xmx%dM", hs.ram_mb);
    argv[0] = (char *)java;
    argv[1] = xms;
    argv[2] = xmx;
    argv[3] = "-jar";
    argv[4] = "server.jar";
    argv[5] = "nogui";
    argv[6] = NULL;
    server = spawn_detached(dir, argv, in_fd, log_fd);
    close(log_fd);
    close(in_fd);
    if (server <= 0) {
        snprintf(why, n, "The server couldn't be started (%s).", strerror(errno));
        return 0;
    }
    memset(sv.players, 0, sizeof sv.players);
    sv.nplayers = 0;
    sv.pid = server;
    sv.ticks = 0;
    {
        int tries;
        for (tries = 0; tries < 50 && !(sv.ticks = proc_ticks(server)); tries++) plat_sleep_us(2000);
    }
    sv.started = (long long)time(NULL);
    sv.log_off = 0;
    npartial = 0;
    sv.why[0] = 0;
    sv.stop_asked = 0;
    sv.state = SV_STARTING;
    write_pidfile();
    {
        char note[160];
        snprintf(note, sizeof note, "starting %s %s with %d MB of memory.", !strcmp(hi.type, "paper") ? "Paper" : "Minecraft", hi.version, hs.ram_mb);
        sv_note(note);
    }
    return 1;
}
