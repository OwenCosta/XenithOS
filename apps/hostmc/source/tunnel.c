/* An SSH reverse tunnel: the PC logs in to a server on the internet and has it listen on a
 * public port for players, passing each connection back down the tunnel to the Minecraft server
 * here.
 *
 *   ssh -N -R <bind>:<public port>:127.0.0.1:<server port> -l <user> <host>
 *
 * Nothing is installed on that server (sshd is all it needs) and nothing changes on the home
 * router: the PC makes the connection outward. HostMC logs in with a key of its own (ed25519,
 * made here, in the tunnel's folder), never with a password, and keeps the server's host key in a
 * known_hosts of its own: trusted the first time it's seen (accept-new), checked every time after.
 *
 * It runs detached, like playit's agent, so it outlives HostMC: a small sh loop (KEEPER) runs ssh,
 * and runs it again when it drops, waiting a little longer each time (up to a minute), whether
 * HostMC is open or not. The next HostMC finds the loop by its PID file (PID and start time,
 * checked against /proc before it's ever signalled). The loop marks each try in tunnel.log, so
 * ssh's own words there say whether it's in, still connecting, or why it stopped. The supporter
 * relay (relay.c) is the same tunnel to its address, account and port, with one difference: its
 * host key comes from XenithOS Cloud and is the only one trusted (tunnel_pin), never one first
 * seen on the way.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hostmc.h"
#include "ui.h"

#define T_KEY    "key"
#define T_HOSTS  "known_hosts"
#define T_PID    "tunnel.pid"
#define T_LOG    "tunnel.log"
#define T_TEST   "test.log"

#define MARK_START "hostmc: ssh starting"
#define MARK_STOP  "hostmc: ssh stopped"

/* Runs ssh (its command line is the loop's arguments) until it's told to stop, again and again.
 * SIGTERM stops ssh too. A try that lasted two minutes starts the waiting over. */
static const char KEEPER[] =
    "trap 'kill \"$c\" 2>/dev/null; exit 0' TERM INT HUP\n"
    "n=0\n"
    "while :; do\n"
    "  s=$(date +%s)\n"
    "  echo '" MARK_START "'\n"
    "  \"$@\" &\n"
    "  c=$!\n"
    "  wait \"$c\"\n"
    "  echo '" MARK_STOP "'\n"
    "  if [ $(( $(date +%s) - s )) -gt 120 ]; then n=0; fi\n"
    "  n=$((n + 1)); d=$((n * 5)); if [ \"$d\" -gt 60 ]; then d=60; fi\n"
    "  sleep \"$d\" &\n"
    "  c=$!\n"
    "  wait \"$c\"\n"
    "done\n";

static void rel(const struct tunnel *t, const char *name, char *out, int n)
{
    snprintf(out, n, "%s/%s", t->dir, name);
}

static void absp(const struct tunnel *t, const char *name, char *out, int n)
{
    snprintf(out, n, "%s/%s", t->abs, name);
}

int tunnel_cfg_ok(const struct tunnel_cfg *c, char *why, int n)
{
    const char *s;
    if (!c->host[0] || c->host[0] == '-' || strlen(c->host) > 120) {
        snprintf(why, n, "Type the VPS's address: its IP address or its name.");
        return 0;
    }
    for (s = c->host; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '.' || *s == '-' || *s == ':')) {
            snprintf(why, n, "The address can only have letters, numbers, dots, dashes and colons.");
            return 0;
        }
    if (!c->user[0] || c->user[0] == '-' || strlen(c->user) > 32) {
        snprintf(why, n, "Type the user name HostMC logs in to the VPS as.");
        return 0;
    }
    for (s = c->user; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '.' || *s == '_' || *s == '-')) {
            snprintf(why, n, "The user name can only have letters, numbers, dots, dashes and underscores.");
            return 0;
        }
    if (c->ssh_port < 1 || c->ssh_port > 65535 || c->public_port < 1 || c->public_port > 65535 ||
        c->local_port < 1 || c->local_port > 65535) {
        snprintf(why, n, "Ports are numbers from 1 to 65535.");
        return 0;
    }
    return 1;
}

/* ssh's command line: logging in (and the forward, when forward is set). */
struct ssh_cmd {
    char *argv[48];
    char prog[600], key[760], hosts[800], port[16], fwd[128], user[56], host[136];
};

static int ssh_cmd(struct ssh_cmd *s, const struct tunnel *t, const struct tunnel_cfg *c, int forward)
{
    int a = 0;
#define ARG(x) (s->argv[a++] = (char *)(x))
    memset(s, 0, sizeof *s);
    if (!find_program("ssh", s->prog, sizeof s->prog)) return 0;
    absp(t, T_KEY, s->key, sizeof s->key);
    snprintf(s->hosts, sizeof s->hosts, "UserKnownHostsFile=\"%s/%s\"", t->abs, T_HOSTS);   /* quoted: the path may hold spaces */
    snprintf(s->port, sizeof s->port, "%d", c->ssh_port);
    snprintf(s->user, sizeof s->user, "%s", c->user);
    snprintf(s->host, sizeof s->host, "%s", c->host);
    if (c->bind[0]) snprintf(s->fwd, sizeof s->fwd, "%s:%d:127.0.0.1:%d", c->bind, c->public_port, c->local_port);
    else snprintf(s->fwd, sizeof s->fwd, "%d:127.0.0.1:%d", c->public_port, c->local_port);
    ARG(s->prog);
    ARG("-F"); ARG("/dev/null");                     /* only these settings, not ~/.ssh/config */
    ARG("-i"); ARG(s->key);
    ARG("-o"); ARG("IdentitiesOnly=yes");
    ARG("-o"); ARG("IdentityAgent=none");
    ARG("-o"); ARG("BatchMode=yes");                 /* never a password prompt */
    ARG("-o"); ARG("ConnectTimeout=15");
    if (c->relay) {                                  /* the host key XenithOS Cloud gave, and no other */
        ARG("-o"); ARG("StrictHostKeyChecking=yes");
        ARG("-o"); ARG("HostKeyAlgorithms=ssh-ed25519");
        ARG("-o"); ARG("UpdateHostKeys=no");
        ARG("-o"); ARG("CheckHostIP=no");
    } else {
        ARG("-o"); ARG("StrictHostKeyChecking=accept-new");
    }
    ARG("-o"); ARG(s->hosts);
    ARG("-o"); ARG("GlobalKnownHostsFile=/dev/null");
    ARG("-o"); ARG("LogLevel=VERBOSE");              /* says "Authenticated to" once it's in */
    ARG("-p"); ARG(s->port);
    ARG("-l"); ARG(s->user);
    if (forward) {
        ARG("-N");
        ARG("-T");
        ARG("-o"); ARG("ExitOnForwardFailure=yes");  /* no port on the VPS, no tunnel: it says why */
        ARG("-o"); ARG("ServerAliveInterval=15");
        ARG("-o"); ARG("ServerAliveCountMax=3");     /* a dead line is noticed in 45 s */
        ARG("-R"); ARG(s->fwd);
    }
    ARG("--");
    ARG(s->host);
    if (!forward) ARG("true");
    s->argv[a] = NULL;
#undef ARG
    return 1;
}

void tunnel_init(struct tunnel *t, const char *dir, const char *abs)
{
    char p[160], *text, *s;
    memset(t, 0, sizeof *t);
    snprintf(t->dir, sizeof t->dir, "%s", dir);
    snprintf(t->abs, sizeof t->abs, "%s", abs);
    rel(t, T_PID, p, sizeof p);
    if (!(text = file_read(p, NULL))) return;
    if ((s = strstr(text, "pid="))) t->pid = (pid_t)atoi(s + 4);
    if ((s = strstr(text, "ticks="))) t->ticks = strtoull(s + 6, NULL, 10);
    free(text);
    if (!tunnel_alive(t)) {
        unlink(p);
        t->pid = 0;
    }
}

int tunnel_alive(struct tunnel *t)
{
    return t->pid > 0 && proc_ours(t->pid, t->ticks, t->abs);
}

int tunnel_key(struct tunnel *t, char *why, int n)
{
    char key[160], pub[170], prog[600];
    const char *argv[] = {"ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", "hostmc", "-f", key, NULL};
    rel(t, T_KEY, key, sizeof key);
    snprintf(pub, sizeof pub, "%s.pub", key);
    if (file_size(key) > 0 && file_size(pub) > 0) return 1;
    if (!find_program("ssh-keygen", prog, sizeof prog)) {
        snprintf(why, n, "HostMC needs ssh for this (the openssh-client package). Run the XenithOS install command once more.");
        return 0;
    }
    mkdirs(t->dir);
    chmod(t->dir, 0700);                              /* the key lives here */
    unlink(key);
    unlink(pub);
    if (job_run(argv, NULL, "Making HostMC's key", NULL, NULL, 0, 0) != 0 || file_size(pub) <= 0) {
        snprintf(why, n, "The key couldn't be made.");
        return 0;
    }
    return 1;
}

int tunnel_pubkey(struct tunnel *t, char *out, int n)
{
    char pub[170], *text;
    rel(t, T_KEY ".pub", pub, sizeof pub);
    out[0] = 0;
    if (!(text = file_read(pub, NULL))) return 0;
    text[strcspn(text, "\r\n")] = 0;
    snprintf(out, n, "%s", text);
    free(text);
    return out[0] != 0;
}

int tunnel_start(struct tunnel *t, const struct tunnel_cfg *c, char *why, int n)
{
    struct ssh_cmd s;
    char log[160], pidfile[160], buf[96], *argv[56];
    int in_fd, out_fd, len, tries, i;
    if (tunnel_alive(t)) return 1;
    if (!tunnel_cfg_ok(c, why, n)) return 0;
    if (!ssh_cmd(&s, t, c, 1)) {
        snprintf(why, n, "HostMC needs ssh for this (the openssh-client package). Run the XenithOS install command once more.");
        return 0;
    }
    /* sh -c KEEPER hostmc-tunnel ssh ...: the loop gets ssh's command line as its arguments. */
    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = (char *)KEEPER;
    argv[3] = "hostmc-tunnel";
    for (i = 0; s.argv[i] && i < 50; i++) argv[4 + i] = s.argv[i];
    argv[4 + i] = NULL;
    rel(t, T_LOG, log, sizeof log);
    rel(t, T_PID, pidfile, sizeof pidfile);
    in_fd = open("/dev/null", O_RDONLY);
    out_fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    t->pid = in_fd >= 0 && out_fd >= 0 ? spawn_detached(t->abs, argv, in_fd, out_fd) : 0;
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    if (t->pid <= 0) {
        t->pid = 0;
        snprintf(why, n, "ssh couldn't be started.");
        return 0;
    }
    for (tries = 0; tries < 50 && !(t->ticks = proc_ticks(t->pid)); tries++) plat_sleep_us(2000);
    len = snprintf(buf, sizeof buf, "pid=%d\nticks=%llu\n", (int)t->pid, t->ticks);
    file_write(pidfile, buf, len, 0600);
    t->started = ui_ms();
    return 1;
}

void tunnel_stop(struct tunnel *t)
{
    char pidfile[160];
    if (tunnel_alive(t)) kill(t->pid, SIGTERM);
    t->pid = 0;
    rel(t, T_PID, pidfile, sizeof pidfile);
    unlink(pidfile);
}

/* The last of needle in hay, or NULL. */
static const char *last(const char *hay, const char *needle)
{
    const char *at = NULL, *p = hay;
    while ((p = strstr(p, needle))) at = p++;
    return at;
}

/* The end of the log (a tunnel that keeps failing for days writes a lot), NUL-terminated, and
 * where in the file it starts. */
static char *log_tail(const struct tunnel *t, long long *from)
{
    char log[160], *buf;
    FILE *f;
    long long size;
    size_t got;
    rel(t, T_LOG, log, sizeof log);
    *from = 0;
    if (!(f = fopen(log, "rb"))) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    *from = size > 65536 ? size - 65536 : 0;
    fseek(f, (long)*from, SEEK_SET);
    if (!(buf = malloc(65537))) { fclose(f); return NULL; }
    got = fread(buf, 1, 65536, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

/* The same, for the supporter relay. */
static void explain_relay(const char *text, const struct tunnel_cfg *c, char *out, int n)
{
    if (strstr(text, "Permission denied"))
        snprintf(out, n, "The relay didn't let HostMC in yet. It takes new keys every 30 seconds, and HostMC tries again by itself. "
                         "If it lasts, choose Check my membership.");
    else if (strstr(text, "remote port forwarding failed") || strstr(text, "Error: remote port forwarding"))
        snprintf(out, n, "The relay is still holding your server's port from an earlier connection. HostMC tries again by itself.");
    else if (strstr(text, "IDENTIFICATION HAS CHANGED") || strstr(text, "Host key verification failed") ||
             strstr(text, "No ED25519 host key is known"))
        snprintf(out, n, "The relay didn't prove it's the one XenithOS Cloud named, so HostMC didn't connect. Choose Check my membership "
                         "to get its current identity.");
    else if (strstr(text, "Could not resolve hostname"))
        snprintf(out, n, "%s doesn't lead anywhere right now. HostMC tries again by itself.", c->host);
    else if (strstr(text, "Connection refused") || strstr(text, "timed out") || strstr(text, "No route to host"))
        snprintf(out, n, "The relay didn't answer. HostMC tries again by itself.");
    else if (strstr(text, "Network is unreachable"))
        snprintf(out, n, "This PC isn't on the internet right now.");
    else if (strstr(text, "Timeout, server") || strstr(text, "Connection closed") || strstr(text, "Broken pipe") ||
             strstr(text, "Connection reset"))
        snprintf(out, n, "The connection to the relay dropped. HostMC connects again.");
    else if (strstr(text, "UNPROTECTED PRIVATE KEY"))
        snprintf(out, n, "HostMC's key can be read by other users, so ssh won't use it. Turn the relay off and on to make a new one.");
}

/* What ssh's own words in a log mean, for people. "" if it said nothing that's wrong. */
static void explain(const char *text, const struct tunnel_cfg *c, char *out, int n)
{
    out[0] = 0;
    if (!text) return;
    if (c->relay) {
        explain_relay(text, c, out, n);
        return;
    }
    if (strstr(text, "Permission denied"))
        snprintf(out, n, "The VPS didn't let HostMC in: add HostMC's key (in the setup steps) to ~/.ssh/authorized_keys for %s on it.", c->user);
    else if (strstr(text, "remote port forwarding failed") || strstr(text, "Error: remote port forwarding"))
        snprintf(out, n, "The VPS wouldn't take players on port %d. Is something else using that port there? Ports below 1024 need the root user.", c->public_port);
    else if (strstr(text, "IDENTIFICATION HAS CHANGED") || strstr(text, "Host key verification failed"))
        snprintf(out, n, "The VPS isn't the one HostMC knew: its identity changed. If you reinstalled it, use Forget the VPS's identity.");
    else if (strstr(text, "Could not resolve hostname"))
        snprintf(out, n, "%s doesn't lead anywhere: check the address.", c->host);
    else if (strstr(text, "Connection refused"))
        snprintf(out, n, "Nothing answered on the VPS's SSH port, %d. Check the address and the port.", c->ssh_port);
    else if (strstr(text, "timed out") || strstr(text, "No route to host"))
        snprintf(out, n, "The VPS didn't answer. Check the address, and that its firewall lets SSH in on port %d.", c->ssh_port);
    else if (strstr(text, "Network is unreachable"))
        snprintf(out, n, "This PC isn't on the internet right now.");
    else if (strstr(text, "Timeout, server") || strstr(text, "Connection closed") || strstr(text, "Broken pipe") ||
             strstr(text, "Connection reset"))
        snprintf(out, n, "The connection to the VPS dropped. HostMC connects again.");
    else if (strstr(text, "UNPROTECTED PRIVATE KEY"))
        snprintf(out, n, "HostMC's key can be read by other users, so ssh won't use it. Turn the VPS route off and on to make a new one.");
}

/* Where the tunnel is, from the latest try in the log. *attempt is where that try starts in the
 * log, so a new try can be told from the one before; why says why the last one stopped. */
int tunnel_state(struct tunnel *t, const struct tunnel_cfg *c, char *why, int n, long long *attempt)
{
    long long from;
    char *text, *start, *stop;
    int state;
    why[0] = 0;
    *attempt = -1;
    if (!tunnel_alive(t)) return TS_STOPPED;
    if (!(text = log_tail(t, &from))) return TS_CONNECTING;
    start = (char *)last(text, MARK_START);
    stop = (char *)last(text, MARK_STOP);
    if (start) *attempt = from + (start - text);
    if (stop && (!start || stop > start)) {
        state = TS_RETRYING;                        /* stopped, waiting to try again */
        *stop = 0;
        explain(start ? start : text, c, why, n);
        if (!why[0]) {                              /* ssh's last line, in plain ASCII */
            char *end = stop, *line;
            int k = 0;
            while (end > text && (end[-1] == '\n' || end[-1] == '\r')) *--end = 0;
            line = strrchr(start ? start : text, '\n');
            line = line ? line + 1 : (start ? start : text);
            for (; *line && k < n - 1; line++)
                if (*line >= ' ' && *line <= '~') why[k++] = *line;
            why[k] = 0;
            if (!strncmp(why, "hostmc:", 7) || !why[0])
                snprintf(why, n, "The connection to the %s closed.", c->relay ? "relay" : "VPS");
        }
    } else if (start && strstr(start, "Authenticated to")) {
        state = TS_UP;
    } else {
        state = TS_CONNECTING;
    }
    free(text);
    return state;
}

int tunnel_test(struct tunnel *t, const struct tunnel_cfg *c, char *why, int n)
{
    struct ssh_cmd s;
    char log[160], *text;
    int code;
    if (!tunnel_cfg_ok(c, why, n)) return 0;
    if (!ssh_cmd(&s, t, c, 0)) {
        snprintf(why, n, "HostMC needs ssh for this (the openssh-client package). Run the XenithOS install command once more.");
        return 0;
    }
    rel(t, T_TEST, log, sizeof log);
    code = job_run_err((const char *const *)s.argv, NULL, log, "Logging in to your VPS", c->host, NULL, 0, 1);
    if (code == 0) return 1;
    if (code == -2) {
        snprintf(why, n, "Cancelled.");
        return 0;
    }
    text = file_read(log, NULL);
    explain(text, c, why, n);
    free(text);
    if (!why[0]) snprintf(why, n, "HostMC couldn't log in to the VPS (ssh said %d).", code);
    return 0;
}

void tunnel_forget_host(struct tunnel *t)
{
    char p[160];
    rel(t, T_HOSTS, p, sizeof p);
    unlink(p);
}

/* Makes hostkey ("ssh-ed25519 <base64>") the only one trusted for the server in c, with
 * StrictHostKeyChecking=yes (c->relay). 0 if it isn't a key like that. */
int tunnel_pin(struct tunnel *t, const struct tunnel_cfg *c, const char *hostkey)
{
    char p[160], line[420], *old;
    const char *b64 = hostkey + 12;
    int len, ok;
    if (strncmp(hostkey, "ssh-ed25519 ", 12) || strlen(b64) != 68 || strspn(b64, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") != 68)
        return 0;
    if (c->ssh_port == 22) len = snprintf(line, sizeof line, "%s %s\n", c->host, hostkey);
    else len = snprintf(line, sizeof line, "[%s]:%d %s\n", c->host, c->ssh_port, hostkey);
    rel(t, T_HOSTS, p, sizeof p);
    old = file_read(p, NULL);
    ok = old && !strcmp(old, line);
    free(old);
    if (ok) return 1;
    mkdirs(t->dir);
    chmod(t->dir, 0700);
    return file_write(p, line, len, 0600);
}
