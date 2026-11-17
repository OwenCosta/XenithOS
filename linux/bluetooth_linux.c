/* Bluetooth controllers on PCs and Pis (platform.h's plat_bt_*), through BlueZ.
 *
 * bluetoothctl runs in a pseudo-terminal of its own, so it talks the way it does to a person at
 * a terminal: a line for everything that happens, as it happens (into a pipe it would hold its
 * words back). What it says is read each frame without waiting, and commands go in as lines:
 *
 *   starting   power on, agent NoInputNoOutput, default-agent (so pairing needs no PIN and
 *              nothing confirmed, which is what a controller wants), devices, scan on
 *   pairing    pair <addr>, then trust <addr> (so it reconnects by itself whenever it's turned
 *              on), then connect <addr> unless it already is
 *
 * The lines that matter, once its colours, its prompt and its line redraws are taken off:
 *   [NEW] Device AA:BB:CC:DD:EE:FF Name     [CHG] Device AA:..:FF Key: value     [DEL] Device ...
 *   Device AA:..:FF Name                    (the answer to devices)
 *   Device AA:..:FF (public), then "\tKey: value" lines (the answer to info)
 *   Pairing successful, Failed to pair: org.bluez.Error.X, Changing AA:..:FF trust succeeded,
 *   Connection successful, Failed to connect: ..., Successful disconnected, Device has been removed
 * A question its agent asks anyway ("(yes/no)") is answered yes.
 *
 * XENITHOS_BLUETOOTHCTL names another program to run instead (the simulator's stand-in,
 * sim/bluetooth/bluetoothctl).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "platform.h"

#define MAXDEV 64

static struct plat_bt_device devs[MAXDEV];
static int ndev;
static struct plat_bt_status st_;
static int fd = -1;
static pid_t pid;
static char buf[1024];                /* the line being read */
static int nbuf;
static char info_for[18];             /* the device an info answer is about */
static uint64_t step_since, setup_since;
static int setup;                     /* how far starting has got (S_*) */
static int unblocked, scan_tries;
static int asked[MAXDEV];             /* info asked for */

enum { S_POWER, S_AGENT, S_DEFAULT, S_SCAN, S_UP };

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    char line[160];
    va_list ap;
    int n;
    if (fd < 0) return;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    if (write(fd, line, (size_t)n) < 0) { /* it has gone: poll finds out */ }
}

static void fail(const char *why)
{
    st_.state = BT_FAILED;
    st_.scanning = 0;
    snprintf(st_.why, sizeof st_.why, "%s", why);
}

static void step_fail(const char *why)
{
    st_.step = BT_STEP_FAILED;
    snprintf(st_.why, sizeof st_.why, "%s", why);
}

static void step_to(int step)
{
    st_.step = step;
    step_since = now_ms();
}

/* ----------------------------------------------------------------- devices */

static int is_addr(const char *s)
{
    int i;
    for (i = 0; i < 17; i++) {
        if (i % 3 == 2) { if (s[i] != ':') return 0; }
        else if (!isxdigit((unsigned char)s[i])) return 0;
    }
    return s[17] == 0 || s[17] == ' ' || s[17] == '\t';
}

static int find(const char *addr)
{
    int i;
    for (i = 0; i < ndev; i++)
        if (!strncasecmp(devs[i].addr, addr, 17)) return i;
    return -1;
}

/* A name that's only its address with dashes is no name. */
static void set_name(struct plat_bt_device *d, const char *name)
{
    char dashed[18];
    int i;
    while (*name == ' ') name++;
    snprintf(dashed, sizeof dashed, "%s", d->addr);
    for (i = 0; dashed[i]; i++) if (dashed[i] == ':') dashed[i] = '-';
    if (!*name || !strncasecmp(name, dashed, 17)) return;
    snprintf(d->name, sizeof d->name, "%s", name);
    /* the usual names, for controllers that don't say what they are (yet) */
    if (strcasestr(name, "controller") || strcasestr(name, "gamepad") || strcasestr(name, "joy-con")
            || strcasestr(name, "dualsense") || strcasestr(name, "dualshock") || strcasestr(name, "8bitdo")
            || strcasestr(name, "joystick"))
        d->gamepad = 1;
}

static struct plat_bt_device *add(const char *addr)
{
    int i = find(addr);
    if (i >= 0) return &devs[i];
    if (ndev == MAXDEV) {                      /* full: the weakest stranger makes room */
        int k, worst = -1;
        for (k = 0; k < ndev; k++)
            if (!devs[k].paired && !devs[k].gamepad && (worst < 0 || devs[k].rssi < devs[worst].rssi)) worst = k;
        if (worst < 0) return NULL;
        devs[worst] = devs[--ndev];
        asked[worst] = asked[ndev];
    }
    memset(&devs[ndev], 0, sizeof devs[ndev]);
    memcpy(devs[ndev].addr, addr, 17);
    devs[ndev].addr[17] = 0;
    for (i = 0; devs[ndev].addr[i]; i++) devs[ndev].addr[i] = (char)toupper((unsigned char)devs[ndev].addr[i]);
    asked[ndev] = 0;
    return &devs[ndev++];
}

static void drop(const char *addr)
{
    int i = find(addr);
    if (i < 0) return;
    devs[i] = devs[--ndev];
    asked[i] = asked[ndev];
}

/* Asks what kind of device it is (its icon), once. */
static void ask_info(const char *addr)
{
    int i = find(addr);
    if (i < 0 || asked[i]) return;
    asked[i] = 1;
    say("info %s", devs[i].addr);
}

static int yes(const char *v)
{
    while (*v == ' ') v++;
    return !strncmp(v, "yes", 3);
}

/* What happens to the device being worked on as its properties change. */
static void progress(struct plat_bt_device *d)
{
    if (strcasecmp(d->addr, st_.addr)) return;
    if (st_.step == BT_PAIRING && d->paired) {
        step_to(BT_TRUSTING);
        say("trust %s", d->addr);
    } else if (st_.step == BT_TRUSTING && d->trusted) {
        if (d->connected) step_to(BT_DONE);
        else {
            step_to(BT_CONNECTING);
            say("connect %s", d->addr);
        }
    } else if (st_.step == BT_CONNECTING && d->connected) {
        step_to(BT_DONE);
    } else if (st_.step == BT_DISCONNECTING && !d->connected) {
        step_to(BT_DONE);
    }
}

static void property(struct plat_bt_device *d, const char *key, const char *v)
{
    while (*v == ' ') v++;
    if (!strcmp(key, "Name") || !strcmp(key, "Alias")) set_name(d, v);
    else if (!strcmp(key, "Paired") || !strcmp(key, "Bonded")) { if (yes(v)) d->paired = 1; else if (!strcmp(key, "Paired")) d->paired = 0; }
    else if (!strcmp(key, "Trusted")) d->trusted = yes(v);
    else if (!strcmp(key, "Connected")) d->connected = yes(v);
    else if (!strcmp(key, "Icon")) { if (!strncmp(v, "input-gaming", 12)) d->gamepad = 1; }
    else if (!strcmp(key, "RSSI")) {
        const char *p = strchr(v, '(');           /* "-60", or "0xffffffc4 (-60)" */
        d->rssi = atoi(p ? p + 1 : v);
    } else if (!strcmp(key, "Class")) {
        unsigned long c = strtoul(v, NULL, 16);
        unsigned major = (unsigned)(c >> 8) & 0x1f, minor = (unsigned)(c >> 2) & 0x0f;
        if (major == 5 && (minor == 1 || minor == 2)) d->gamepad = 1;   /* a joystick or gamepad */
    } else if (!strcmp(key, "Appearance")) {
        unsigned long a = strtoul(v, NULL, 16);
        if (a == 0x03c3 || a == 0x03c4) d->gamepad = 1;
    } else return;
    progress(d);
}

/* --------------------------------------------------------------- what it says */

static const char *bluez_error(const char *s)
{
    const char *e = strstr(s, "org.bluez.Error.");
    return e ? e + 16 : s;
}

static void pair_failed(const char *line)
{
    const char *e = bluez_error(line);
    char why[200];
    if (!strncmp(e, "AlreadyExists", 13)) {          /* paired already: carry on */
        int i = find(st_.addr);
        if (i >= 0) { devs[i].paired = 1; progress(&devs[i]); }
        return;
    }
    if (!strncmp(e, "Authentication", 14))
        snprintf(why, sizeof why, "The controller said no. Put it in pairing mode again and choose it once more.");
    else if (!strncmp(e, "ConnectionAttemptFailed", 23) || !strncmp(e, "PageTimeout", 11) || !strncmp(e, "Failed", 6))
        snprintf(why, sizeof why, "It didn't answer. Keep it in pairing mode, bring it closer, and choose it again.");
    else if (!strncmp(e, "InProgress", 10))
        snprintf(why, sizeof why, "It's already pairing. Give it a moment.");
    else
        snprintf(why, sizeof why, "It didn't pair (%.60s). Put it in pairing mode and try again.", e);
    step_fail(why);
}

static void up_and_scanning(void)
{
    setup = S_SCAN;
    setup_since = now_ms();
    say("devices");
    say("scan on");
}

static void line_in(char *s)
{
    char *p;
    int i;
    /* the prompt it puts before some lines: "[bluetooth]# ", "[Some Device]# " */
    if (s[0] == '[' && (p = strstr(s, "]# ")) && p - s < 64 && strncmp(s, "[NEW]", 5) && strncmp(s, "[CHG]", 5)
            && strncmp(s, "[DEL]", 5))
        s = p + 3;
    if (!*s) return;

    if (!strncmp(s, "[NEW] Device ", 13) && is_addr(s + 13)) {
        struct plat_bt_device *d = add(s + 13);
        if (d) { set_name(d, s + 30); ask_info(d->addr); }
    } else if (!strncmp(s, "[DEL] Device ", 13) && is_addr(s + 13)) {
        if (st_.step == BT_FORGETTING && !strncasecmp(st_.addr, s + 13, 17)) step_to(BT_DONE);
        drop(s + 13);
    } else if (!strncmp(s, "[CHG] Device ", 13) && is_addr(s + 13)) {
        struct plat_bt_device *d = add(s + 13);
        char *colon = strchr(s + 31, ':');
        if (d && colon) {
            *colon = 0;
            property(d, s + 31, colon + 1);
        }
    } else if (!strncmp(s, "[CHG] Controller ", 17)) {
        if (strstr(s, "Discovering: yes")) st_.scanning = 1;
        else if (strstr(s, "Discovering: no")) st_.scanning = 0;
        else if (strstr(s, "Powered: yes") && setup == S_POWER) { setup = S_AGENT; say("agent off"); say("agent NoInputNoOutput"); }
    } else if (!strncmp(s, "Device ", 7) && is_addr(s + 7)) {
        if (!strncmp(s + 24, " (public)", 9) || !strncmp(s + 24, " (random)", 9)) {
            memcpy(info_for, s + 7, 17);                /* an info answer starts */
            info_for[17] = 0;
        } else if (strstr(s + 24, "not available")) {
            if (!strncasecmp(st_.addr, s + 7, 17) && st_.step >= BT_PAIRING && st_.step <= BT_FORGETTING)
                step_fail("It isn't nearby any more. Put it in pairing mode and look again.");
        } else {
            struct plat_bt_device *d = add(s + 7);     /* one it knows (devices) */
            if (d) { set_name(d, s + 25); ask_info(d->addr); }
        }
    } else if ((s[0] == '\t' || s[0] == ' ') && info_for[0]) {
        char *k = s, *colon;
        while (*k == '\t' || *k == ' ') k++;
        colon = strchr(k, ':');
        if (colon && (i = find(info_for)) >= 0) {
            *colon = 0;
            property(&devs[i], k, colon + 1);
        }
    } else if (!strncmp(s, "Pairing successful", 18)) {
        if ((i = find(st_.addr)) >= 0) { devs[i].paired = 1; progress(&devs[i]); }
    } else if (!strncmp(s, "Failed to pair", 14)) {
        if (st_.step == BT_PAIRING) pair_failed(s);
    } else if (strstr(s, "trust succeeded")) {
        if ((i = find(st_.addr)) >= 0) { devs[i].trusted = 1; progress(&devs[i]); }
    } else if (!strncmp(s, "Failed to trust", 15) || (!strncmp(s, "Changing", 8) && strstr(s, "trust failed"))) {
        if (st_.step == BT_TRUSTING) step_fail("It paired, but couldn't be set to reconnect by itself. Try once more.");
    } else if (!strncmp(s, "Connection successful", 21)) {
        if ((i = find(st_.addr)) >= 0) { devs[i].connected = 1; progress(&devs[i]); }
    } else if (!strncmp(s, "Failed to connect", 17)) {
        if (st_.step == BT_CONNECTING)
            step_fail("It's paired, but didn't connect. Press a button on it to wake it up, then choose it again.");
    } else if (!strncmp(s, "Successful disconnected", 23)) {
        if ((i = find(st_.addr)) >= 0) { devs[i].connected = 0; progress(&devs[i]); }
    } else if (!strncmp(s, "Failed to disconnect", 20)) {
        if (st_.step == BT_DISCONNECTING) step_fail("It didn't disconnect. Turn it off instead.");
    } else if (!strncmp(s, "Device has been removed", 23)) {
        if (st_.step == BT_FORGETTING) { drop(st_.addr); step_to(BT_DONE); }
    } else if (!strncmp(s, "Failed to remove", 16)) {
        if (st_.step == BT_FORGETTING) step_fail("It couldn't be forgotten just now. Try once more.");
    } else if (!strncmp(s, "Changing power on succeeded", 27)) {
        if (setup == S_POWER) { setup = S_AGENT; setup_since = now_ms(); say("agent off"); say("agent NoInputNoOutput"); }
    } else if (!strncmp(s, "Failed to set power on", 22)) {
        if (strstr(s, "Blocked") && !unblocked && !getenv("XENITHOS_BLUETOOTHCTL")) {
            /* switched off (rfkill): the console's own session may switch it back on (not with a
             * stand-in for bluetoothctl, which mustn't touch this machine's own) */
            unblocked = 1;
            if (system("rfkill unblock bluetooth >/dev/null 2>&1") == 0) { say("power on"); return; }
        }
        fail("Bluetooth is switched off on this machine, and XenithOS can't switch it on.");
    } else if (!strncmp(s, "Agent registered", 16) || !strncmp(s, "Agent is already registered", 27)) {
        if (setup == S_AGENT) { setup = S_DEFAULT; setup_since = now_ms(); say("default-agent"); }
    } else if (!strncmp(s, "Default agent request successful", 32)) {
        if (setup == S_DEFAULT) up_and_scanning();
    } else if (!strncmp(s, "Discovery started", 17)) {
        st_.scanning = 1;
        if (setup == S_SCAN) { setup = S_UP; st_.state = BT_READY; }
    } else if (!strncmp(s, "Failed to start discovery", 25)) {
        /* it was still waking up: the next poll asks again */
        if (setup == S_SCAN && scan_tries++ < 3) setup_since = now_ms() - 4000;
    } else if (!strncmp(s, "No default controller available", 31)) {
        fail("This machine has no Bluetooth, or it's turned off. A USB Bluetooth adapter works too.");
    }
}

/* Bytes from the terminal: colours and other escapes go. A carriage return followed by a newline
 * ends the line (the terminal's own "\r\n"); followed by anything else it starts the line again,
 * which is how it redraws its prompt around what it prints. */
static int cr, esc;

static void bytes_in(const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)b[i];
        if (esc) {                                 /* inside ESC [ ... final (it can span reads) */
            if (esc == 1) esc = c == '[' ? 2 : 0;
            else if (c >= 0x40 && c <= 0x7e) esc = 0;
            continue;
        }
        if (cr) {
            cr = 0;
            if (c == '\n') {
                buf[nbuf] = 0;
                line_in(buf);
                nbuf = 0;
                continue;
            }
            nbuf = 0;
        }
        if (c == 0x1b) { esc = 1; continue; }
        if (c == '\r') { cr = 1; continue; }
        if (c == '\n') {
            buf[nbuf] = 0;
            line_in(buf);
            nbuf = 0;
            continue;
        }
        if (c < 32 && c != '\t') continue;          /* readline's markers and the like */
        if (nbuf < (int)sizeof buf - 1) buf[nbuf++] = (char)c;
    }
    /* a question its agent asks, waiting on the same line for an answer */
    buf[nbuf] = 0;
    if (nbuf && strstr(buf, "(yes/no)")) {
        say("yes");
        nbuf = 0;
    }
}

/* ------------------------------------------------------------------ platform */

static int on_path(const char *prog)
{
    char path[512];
    const char *p = getenv("PATH"), *e;
    if (strchr(prog, '/')) return access(prog, X_OK) == 0;
    for (; p && *p; p = *e ? e + 1 : e) {
        e = strchr(p, ':');
        if (!e) e = p + strlen(p);
        snprintf(path, sizeof path, "%.*s/%s", (int)(e - p), p, prog);
        if (access(path, X_OK) == 0) return 1;
    }
    return 0;
}

int plat_bt_open(void)
{
    const char *prog = getenv("XENITHOS_BLUETOOTHCTL");
    char *slave;
    struct winsize ws;
    int s;
    if (fd >= 0 && st_.state != BT_FAILED) return 1;
    plat_bt_close();
    memset(&st_, 0, sizeof st_);
    ndev = 0;
    nbuf = cr = esc = 0;
    info_for[0] = 0;
    unblocked = scan_tries = 0;
    if (!prog || !*prog) prog = "bluetoothctl";
    if (!on_path(prog)) {
        fail("Bluetooth needs BlueZ, which isn't on this machine. Run the XenithOS install command again to add it.");
        return 0;
    }
    fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (fd < 0 || grantpt(fd) || unlockpt(fd) || !(slave = ptsname(fd))) {
        if (fd >= 0) close(fd);
        fd = -1;
        fail("Bluetooth couldn't be started here (no terminal for bluetoothctl).");
        return 0;
    }
    memset(&ws, 0, sizeof ws);
    ws.ws_col = 400;                               /* wide: no line of its gets wrapped */
    ws.ws_row = 50;
    ioctl(fd, TIOCSWINSZ, &ws);
    pid = fork();
    if (pid == 0) {
        setsid();
        s = open(slave, O_RDWR);
        if (s < 0) _exit(127);
        ioctl(s, TIOCSCTTY, 0);
        dup2(s, 0);
        dup2(s, 1);
        dup2(s, 2);
        if (s > 2) close(s);
        close(fd);
        setenv("TERM", "dumb", 1);
        execlp(prog, prog, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) {
        close(fd);
        fd = -1;
        fail("Bluetooth couldn't be started here.");
        return 0;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    st_.state = BT_STARTING;
    setup = S_POWER;
    setup_since = now_ms();
    say("power on");
    return 1;
}

void plat_bt_poll(struct plat_bt_status *out)
{
    char b[2048];
    uint64_t now = now_ms();
    int n, status;
    if (fd >= 0) {
        while ((n = (int)read(fd, b, sizeof b)) > 0) bytes_in(b, n);
        if (pid > 0 && waitpid(pid, &status, WNOHANG) == pid) {
            pid = 0;
            close(fd);
            fd = -1;
            if (st_.state != BT_FAILED) fail("bluetoothctl stopped. Leave this screen and come back to start it again.");
        }
    }
    /* starting that stalls: bluetoothd isn't running, most likely */
    if (st_.state == BT_STARTING && setup != S_SCAN && now - setup_since > 8000)
        fail("Bluetooth isn't running on this machine (its bluetooth service). Restart it, or run the XenithOS install command again.");
    if (st_.state == BT_STARTING && setup == S_SCAN && now - setup_since > 5000) {
        if (scan_tries++ < 3) { setup_since = now; say("scan on"); }
        else { setup = S_UP; st_.state = BT_READY; }     /* it can still pair what it knows */
    }
    /* steps that take too long */
    if (st_.step >= BT_PAIRING && st_.step <= BT_FORGETTING) {
        int limit = st_.step == BT_PAIRING ? 35000 : st_.step == BT_CONNECTING ? 25000 : 10000;
        if (now - step_since > (uint64_t)limit)
            step_fail(st_.step == BT_PAIRING ? "It didn't answer. Keep it in pairing mode, bring it closer, and choose it again."
                      : st_.step == BT_CONNECTING ? "It's paired, but didn't connect. Press a button on it to wake it up."
                      : "That didn't work just now. Try once more.");
    }
    if (out) *out = st_;
}

int plat_bt_devices(struct plat_bt_device *out, int max)
{
    int i;
    for (i = 0; i < ndev && i < max; i++) out[i] = devs[i];
    return i;
}

static int begin(const char *addr, int step)
{
    if (fd < 0 || !addr || strlen(addr) != 17) return 0;
    if (st_.step >= BT_PAIRING && st_.step <= BT_FORGETTING) return 0;   /* one thing at a time */
    snprintf(st_.addr, sizeof st_.addr, "%s", addr);
    st_.why[0] = 0;
    step_to(step);
    return 1;
}

void plat_bt_pair(const char *addr)
{
    int i = find(addr ? addr : "");
    if (!begin(addr, BT_PAIRING)) return;
    if (i >= 0 && devs[i].paired) progress(&devs[i]);    /* on to trusting it */
    else say("pair %s", addr);
}

void plat_bt_connect(const char *addr)
{
    if (begin(addr, BT_CONNECTING)) say("connect %s", addr);
}

void plat_bt_disconnect(const char *addr)
{
    if (begin(addr, BT_DISCONNECTING)) say("disconnect %s", addr);
}

void plat_bt_forget(const char *addr)
{
    if (begin(addr, BT_FORGETTING)) say("remove %s", addr);
}

void plat_bt_close(void)
{
    int i, status;
    if (fd >= 0) {
        say("scan off");
        say("quit");
        for (i = 0; i < 50 && pid > 0; i++) {        /* half a second to go by itself */
            if (waitpid(pid, &status, WNOHANG) == pid) pid = 0;
            else usleep(10000);
        }
        if (pid > 0) {
            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
        }
        close(fd);
    }
    fd = -1;
    pid = 0;
    st_.state = BT_CLOSED;
    st_.scanning = 0;
}
