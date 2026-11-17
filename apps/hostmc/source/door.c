/* Players' real addresses through the supporter relay, and the doorway that keeps every other way
 * in working while they come.
 *
 * Through the relay every player would arrive from 127.0.0.1, the end of HostMC's tunnel. Paper
 * can be told each player's address instead: with proxies.proxy-protocol on (in
 * config/paper-global.yml) it reads a PROXY protocol header at the start of each connection, which
 * the relay's router sends for servers that asked (XenithOS Cloud passes the supporter's choice
 * on; relay.c). Minecraft's own server can't, so with it players keep showing up as 127.0.0.1.
 *
 * But Paper then wants that header on every connection, and Minecraft on this PC (localhost) and
 * friends on the home network don't send one. So, in this "door mode", a doorway takes the
 * server's port, the one everyone has been joining at: a second HostMC process (hostmc --door,
 * door_main below), started and stopped with the server and left running like it. It passes each
 * connection to Paper's own port on the loopback with a PROXY header naming who it's from, so
 * those players get in as before, with their own addresses too. The relay's tunnel goes straight
 * to Paper's port, since the relay's router sends the header itself.
 *
 * door.txt says door mode is set up: the port players use (what Settings shows, server_port()),
 * Paper's own port, and what server.properties' server-ip was before HostMC set it to 127.0.0.1.
 * It's set up as the server starts (door_prepare) and put back at a start without it.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hostmc.h"
#include "ui.h"

#define DOOR_DIR     HM_DIR "/door"
#define DOOR_FILE    DOOR_DIR "/door.txt"
#define DOOR_PID     DOOR_DIR "/door.pid"
#define DOOR_LOG     DOOR_DIR "/door.log"
#define PAPER_CONFIG HM_SERVER "/config"
#define PAPER_GLOBAL PAPER_CONFIG "/paper-global.yml"

static struct {
    int on;                  /* door.txt is there: the server was last started in door mode */
    int port;                /* the port players use: the doorway's */
    int paper;               /* Paper's own, on the loopback */
    int proxy;               /* HostMC turned Paper's proxy-protocol on (and turns it off again) */
    char ip[64];             /* server-ip before HostMC set it */
} door;
static pid_t door_pid;
static unsigned long long door_ticks;
static uint64_t door_checked;

/* ===================================================================== the doorway itself
 *
 * hostmc --door PORT TO: takes PORT on every address (IPv6 and IPv4), and passes each
 * connection on to 127.0.0.1:TO with a PROXY protocol v2 header first. It runs until it's
 * stopped (SIGTERM), and says "listening" in its log once it is. */

#define DOOR_MAX   256                       /* connections at once */
#define DOOR_BUF   16384                     /* bytes waiting each way, each connection */

struct pipe2 { char *buf; int len, off, eof, shut; };
struct link {
    int fd[2];                               /* 0: the player, 1: Paper */
    struct pipe2 dir[2];                     /* 0: player to Paper, 1: Paper to player */
};
static struct link *links[DOOR_MAX];

/* An IPv4 address that came as IPv6 (::ffff:a.b.c.d), as the 4 bytes it is. */
static int v4_of(const struct sockaddr_storage *s, unsigned char out[4], unsigned *port)
{
    if (s->ss_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)s;
        memcpy(out, &a->sin_addr, 4);
        *port = ntohs(a->sin_port);
        return 1;
    }
    if (s->ss_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)s;
        if (IN6_IS_ADDR_V4MAPPED(&a->sin6_addr)) {
            memcpy(out, a->sin6_addr.s6_addr + 12, 4);
            *port = ntohs(a->sin6_port);
            return 1;
        }
    }
    return 0;
}

/* PROXY protocol v2 (haproxy.org's spec): who the player is (from) and where they came in (to).
 * The length, or 0 if the addresses can't be put in one. */
static int proxy_header(const struct sockaddr_storage *from, const struct sockaddr_storage *to, unsigned char *out)
{
    static const unsigned char sig[12] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A};
    unsigned char a4[4], b4[4];
    unsigned pa, pb;
    memcpy(out, sig, 12);
    out[12] = 0x21;                                          /* version 2, PROXY */
    if (v4_of(from, a4, &pa) && v4_of(to, b4, &pb)) {
        out[13] = 0x11;                                      /* TCP over IPv4 */
        out[14] = 0;
        out[15] = 12;
        memcpy(out + 16, a4, 4);
        memcpy(out + 20, b4, 4);
        out[24] = (unsigned char)(pa >> 8); out[25] = (unsigned char)pa;
        out[26] = (unsigned char)(pb >> 8); out[27] = (unsigned char)pb;
        return 28;
    }
    if (from->ss_family == AF_INET6 && to->ss_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)from, *b = (const struct sockaddr_in6 *)to;
        out[13] = 0x21;                                      /* TCP over IPv6 */
        out[14] = 0;
        out[15] = 36;
        memcpy(out + 16, &a->sin6_addr, 16);
        memcpy(out + 32, &b->sin6_addr, 16);
        pa = ntohs(a->sin6_port);
        pb = ntohs(b->sin6_port);
        out[48] = (unsigned char)(pa >> 8); out[49] = (unsigned char)pa;
        out[50] = (unsigned char)(pb >> 8); out[51] = (unsigned char)pb;
        return 52;
    }
    return 0;
}

static void link_close(int i)
{
    struct link *l = links[i];
    if (!l) return;
    close(l->fd[0]);
    close(l->fd[1]);
    free(l->dir[0].buf);
    free(l->dir[1].buf);
    free(l);
    links[i] = NULL;
}

/* A player at the door: Paper's port is connected to and told who they are first. */
static void door_accept(int lfd, int to_port)
{
    struct sockaddr_storage from, here;
    socklen_t fl = sizeof from, hl = sizeof here;
    struct sockaddr_in paper;
    struct timeval tv = {3, 0};
    struct link *l;
    int c, p, i, slot = -1, n;
    c = accept(lfd, (struct sockaddr *)&from, &fl);
    if (c < 0) return;
    for (i = 0; i < DOOR_MAX && slot < 0; i++)
        if (!links[i]) slot = i;
    if (slot < 0 || getsockname(c, (struct sockaddr *)&here, &hl) != 0) { close(c); return; }
    p = socket(AF_INET, SOCK_STREAM, 0);
    if (p < 0) { close(c); return; }
    setsockopt(p, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);    /* a connect that can't go through gives up */
    memset(&paper, 0, sizeof paper);
    paper.sin_family = AF_INET;
    paper.sin_port = htons((uint16_t)to_port);
    paper.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(p, (struct sockaddr *)&paper, sizeof paper) != 0) {   /* Paper isn't taking players (yet) */
        close(p);
        close(c);
        return;
    }
    l = calloc(1, sizeof *l);
    if (!l || !(l->dir[0].buf = malloc(DOOR_BUF)) || !(l->dir[1].buf = malloc(DOOR_BUF))) {
        if (l) { free(l->dir[0].buf); free(l); }
        close(p);
        close(c);
        return;
    }
    n = proxy_header(&from, &here, (unsigned char *)l->dir[0].buf);
    if (n <= 0) {
        free(l->dir[0].buf); free(l->dir[1].buf); free(l);
        close(p);
        close(c);
        return;
    }
    l->dir[0].len = n;                                        /* goes before anything the player sends */
    l->fd[0] = c;
    l->fd[1] = p;
    fcntl(c, F_SETFL, fcntl(c, F_GETFL) | O_NONBLOCK);
    fcntl(p, F_SETFL, fcntl(p, F_GETFL) | O_NONBLOCK);
    links[slot] = l;
}

/* One direction of a link: d 0 reads the player and writes Paper, d 1 the other way. 0 when the
 * link is finished (both ways closed, or an error). */
static int link_move(struct link *l, int d, int can_read, int can_write)
{
    struct pipe2 *q = &l->dir[d];
    int src = l->fd[d], dst = l->fd[!d];
    if (can_read && !q->eof && q->len < DOOR_BUF) {
        ssize_t r = recv(src, q->buf + q->len, (size_t)(DOOR_BUF - q->len), 0);
        if (r > 0) q->len += (int)r;
        else if (r == 0) q->eof = 1;
        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return 0;
    }
    if (can_write && q->off < q->len) {
        ssize_t w = send(dst, q->buf + q->off, (size_t)(q->len - q->off), MSG_NOSIGNAL);
        if (w > 0) q->off += (int)w;
        else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return 0;
    }
    if (q->off == q->len) {
        q->off = q->len = 0;
        if (q->eof && !q->shut) {                             /* all of it passed on: say it's done */
            shutdown(dst, SHUT_WR);
            q->shut = 1;
        }
    } else if (q->off > DOOR_BUF / 2) {                      /* room at the end again */
        memmove(q->buf, q->buf + q->off, (size_t)(q->len - q->off));
        q->len -= q->off;
        q->off = 0;
    }
    return !(l->dir[0].shut && l->dir[1].shut);
}

static int door_listen(int port)
{
    int fd = socket(AF_INET6, SOCK_STREAM, 0), on = 1, off = 0;
    if (fd >= 0) {                                           /* IPv6 and IPv4 both, as Minecraft does */
        struct sockaddr_in6 a;
        memset(&a, 0, sizeof a);
        a.sin6_family = AF_INET6;
        a.sin6_port = htons((uint16_t)port);
        a.sin6_addr = in6addr_any;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
        if (bind(fd, (struct sockaddr *)&a, sizeof a) == 0 && listen(fd, 64) == 0) return fd;
        if (errno == EADDRINUSE) { close(fd); return -1; }
        close(fd);
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);                    /* a system without IPv6 */
    if (fd >= 0) {
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)port);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        if (bind(fd, (struct sockaddr *)&a, sizeof a) == 0 && listen(fd, 64) == 0) return fd;
        close(fd);
    }
    return -1;
}

int door_main(int port, int to_port)
{
    struct pollfd pf[1 + 2 * DOOR_MAX];
    int map[1 + 2 * DOOR_MAX], lfd, i;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, SIG_DFL);
    if (port < 1 || port > 65535 || to_port < 1 || to_port > 65535 || port == to_port) {
        fprintf(stderr, "HostMC's doorway: usage: hostmc --door PORT TO\n");
        return 2;
    }
    lfd = door_listen(port);
    if (lfd < 0) {
        fprintf(stderr, "HostMC's doorway: port %d is in use (%s)\n", port, strerror(errno));
        return 3;
    }
    fcntl(lfd, F_SETFL, fcntl(lfd, F_GETFL) | O_NONBLOCK);
    fprintf(stderr, "HostMC's doorway: listening on %d, for Paper on 127.0.0.1:%d\n", port, to_port);
    for (;;) {
        int n = 1;
        pf[0].fd = lfd;
        pf[0].events = POLLIN;
        for (i = 0; i < DOOR_MAX; i++) {
            struct link *l = links[i];
            int d;
            if (!l) continue;
            for (d = 0; d < 2; d++) {
                pf[n].fd = l->fd[d];
                pf[n].events = 0;
                /* read this side while its way has room; write it while the other way has bytes for it */
                if (!l->dir[d].eof && l->dir[d].len < DOOR_BUF) pf[n].events |= POLLIN;
                if (l->dir[!d].off < l->dir[!d].len) pf[n].events |= POLLOUT;
                map[n++] = i * 2 + d;
            }
        }
        if (poll(pf, (nfds_t)n, -1) < 0) {
            if (errno == EINTR) continue;
            return 1;
        }
        if (pf[0].revents & POLLIN) door_accept(lfd, to_port);
        for (i = 1; i + 1 < n; i += 2) {
            int k = map[i] / 2;
            struct link *l = links[k];
            short e0 = pf[i].revents, e1 = pf[i + 1].revents;
            int bad = (e0 | e1) & POLLNVAL;
            if (!l) continue;
            /* POLLHUP and POLLERR come with nothing to read: a read finds the end or the error */
            if (bad || !link_move(l, 0, (e0 & (POLLIN | POLLHUP | POLLERR)) != 0, (e1 & POLLOUT) != 0) ||
                !link_move(l, 1, (e1 & (POLLIN | POLLHUP | POLLERR)) != 0, (e0 & POLLOUT) != 0))
                link_close(k);
        }
    }
}

/* ===================================================================== in HostMC */

static void door_dir(char *out, int n)
{
    snprintf(out, n, "%s/door", hm_root());
}

int door_alive(void)
{
    char dir[740];
    door_dir(dir, sizeof dir);
    return door_pid > 0 && proc_ours(door_pid, door_ticks, dir);
}

int door_on(void) { return door.on; }
int door_players_port(void) { return door.port; }
int door_paper_port(void) { return door.on ? door.paper : server_port(); }

static void door_save(void)
{
    char buf[200];
    int n;
    mkdirs(DOOR_DIR);
    n = snprintf(buf, sizeof buf, "port=%d\npaper=%d\nproxy=%d\nip=%s\n", door.port, door.paper, door.proxy, door.ip);
    file_write(DOOR_FILE, buf, n, 0644);
}

static int field_int(const char *t, const char *key)
{
    const char *p = strstr(t, key);
    return p ? atoi(p + strlen(key)) : 0;
}

void door_load(void)
{
    char *t = file_read(DOOR_FILE, NULL), *p;
    memset(&door, 0, sizeof door);
    door_pid = 0;
    door_ticks = 0;
    if (t) {
        door.port = field_int(t, "port=");
        door.paper = field_int(t, "paper=");
        door.proxy = field_int(t, "proxy=");
        if ((p = strstr(t, "ip="))) {
            p += 3;
            snprintf(door.ip, sizeof door.ip, "%.*s", (int)strcspn(p, "\r\n"), p);
        }
        door.on = door.port > 0 && door.port < 65536 && door.paper > 0 && door.paper < 65536 && door.port != door.paper;
        free(t);
    }
    if ((t = file_read(DOOR_PID, NULL))) {
        door_pid = (pid_t)field_int(t, "pid=");
        door_ticks = strtoull(strstr(t, "ticks=") ? strstr(t, "ticks=") + 6 : "0", NULL, 10);
        free(t);
    }
    if (!door_alive()) {
        door_pid = 0;
        unlink(DOOR_PID);
    }
}

void door_stop(void)
{
    int i;
    if (door_alive()) {
        kill(door_pid, SIGTERM);
        for (i = 0; i < 50 && door_alive(); i++) plat_sleep_us(10000);
        sv_note("the doorway on the server's port closed.");
    }
    door_pid = 0;
    unlink(DOOR_PID);
}

/* Starts the doorway (door.port to door.paper), detached like the server. 0 with why. */
static int door_start(char *why, int n)
{
    char exe[700], dir[740], a1[16], a2[16], *argv[5], *log;
    int in_fd, out_fd, i, len;
    if (door_alive()) return 1;
    len = (int)readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (len <= 0) {
        snprintf(why, n, "HostMC couldn't find itself to start the doorway.");
        return 0;
    }
    exe[len] = 0;
    if (len > 10 && !strcmp(exe + len - 10, " (deleted)")) exe[len - 10] = 0;   /* updated since: the new one */
    door_dir(dir, sizeof dir);
    mkdirs(DOOR_DIR);
    snprintf(a1, sizeof a1, "%d", door.port);
    snprintf(a2, sizeof a2, "%d", door.paper);
    argv[0] = exe;
    argv[1] = "--door";
    argv[2] = a1;
    argv[3] = a2;
    argv[4] = NULL;
    in_fd = open("/dev/null", O_RDONLY);
    out_fd = open(DOOR_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    door_pid = in_fd >= 0 && out_fd >= 0 ? spawn_detached(dir, argv, in_fd, out_fd) : 0;
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    if (door_pid <= 0) {
        door_pid = 0;
        snprintf(why, n, "HostMC's doorway for the server's port couldn't start.");
        return 0;
    }
    for (i = 0; i < 50 && !(door_ticks = proc_ticks(door_pid)); i++) plat_sleep_us(2000);
    {
        char buf[96];
        int k = snprintf(buf, sizeof buf, "pid=%d\nticks=%llu\n", (int)door_pid, door_ticks);
        file_write(DOOR_PID, buf, k, 0644);
    }
    /* It says straight away whether it has the port. */
    for (i = 0; i < 100; i++) {
        if ((log = file_read(DOOR_LOG, NULL))) {
            int listening = strstr(log, "listening") != NULL, taken = strstr(log, "in use") != NULL;
            free(log);
            if (listening) {
                sv_note("the doorway on the server's port is open, so everyone joins as before, with their own address.");
                return 1;
            }
            if (taken) break;
        }
        if (!door_alive()) break;
        plat_sleep_us(20000);
    }
    door_stop();
    snprintf(why, n, "Port %d is already taken. Is another Minecraft server running on this PC?", door.port);
    return 0;
}

/* Nothing has port p on the loopback (or on every address). */
static int port_free(int p)
{
    struct sockaddr_in a;
    int fd = socket(AF_INET, SOCK_STREAM, 0), on = 1, ok;
    if (fd < 0) return 0;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)p);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok = bind(fd, (struct sockaddr *)&a, sizeof a) == 0;
    close(fd);
    return ok;
}

/* A port on the loopback nobody has, near the players' one, for Paper. */
static int free_port(int after)
{
    int p;
    for (p = after + 1; p < after + 200 && p < 65536; p++)
        if (p != DEFAULT_PORT && port_free(p)) return p;
    return 0;
}

/* -------------------------------------------------------- Paper's own setting */

/* proxies.proxy-protocol in config/paper-global.yml, set to on (1) or off: the line changed in
 * place, added under proxies: (made if there's none), or a file made with just that (Paper fills
 * in the rest as it starts). 0 if it couldn't be written. */
int paper_proxy_protocol(int on)
{
    char *t = file_read(PAPER_GLOBAL, NULL), *out;
    const char *value = on ? "true" : "false", *p, *block_end = NULL, *found = NULL;
    size_t len, cap, vl = strlen(value);
    int child = 0, ok;
    if (!t) {
        char buf[64];
        int n;
        if (!on) return 1;                                   /* nothing to turn off */
        mkdirs(PAPER_CONFIG);
        n = snprintf(buf, sizeof buf, "proxies:\n  proxy-protocol: true\n");
        return file_write(PAPER_GLOBAL, buf, n, 0644);
    }
    len = strlen(t);
    /* "proxies:" at the top level, then "proxy-protocol:" among its own children (the first
     * level under it: velocity's settings sit deeper). */
    for (p = t; *p; ) {
        const char *eol = strchr(p, '\n'), *next = eol ? eol + 1 : p + strlen(p);
        int k = (int)strspn(p, " "), blank = p[k] == '\n' || p[k] == '\r' || !p[k] || p[k] == '#';
        if (!block_end) {
            if (k == 0 && !strncmp(p, "proxies:", 8)) {
                if (p[8 + strspn(p + 8, " \r")] != '\n' && p[8 + strspn(p + 8, " \r")]) { free(t); return 0; }   /* not a block */
                block_end = next;                            /* where a new child goes */
            }
        } else if (!blank) {
            if (k == 0) break;                               /* the next top-level key: its block is over */
            if (!child) child = k;
            if (k == child && !strncmp(p + k, "proxy-protocol:", 15)) { found = p; break; }
        }
        p = next;
    }
    if (child <= 0 || child > 8) child = 2;
    cap = len + 64;
    if (!(out = malloc(cap))) { free(t); return 0; }
    if (found) {
        const char *cur = found + child + 15, *end = strchr(found, '\n');
        cur += strspn(cur, " ");
        if (!strncmp(cur, value, vl) && strspn(cur + vl, " \r") == strcspn(cur + vl, "\n")) {
            free(out);
            free(t);
            return 1;                                        /* as it is already */
        }
        snprintf(out, cap, "%.*s%*sproxy-protocol: %s\n%s", (int)(found - t), t, child, "", value, end ? end + 1 : "");
    } else if (block_end) {
        snprintf(out, cap, "%.*s%s%*sproxy-protocol: %s\n%s", (int)(block_end - t), t,
                 block_end[-1] == '\n' ? "" : "\n", child, "", value, block_end);
    } else if (on) {
        snprintf(out, cap, "%s%sproxies:\n  proxy-protocol: true\n", t, len && t[len - 1] != '\n' ? "\n" : "");
    } else {
        free(out);
        free(t);
        return 1;                                            /* no proxies section: it's off already */
    }
    ok = file_write(PAPER_GLOBAL, out, (long)strlen(out), 0644);
    free(out);
    free(t);
    return ok;
}

/* ---------------------------------------------------------------- door mode */

/* Back to the server's port as the user set it: Paper's own server-port and server-ip as they
 * were, and proxy-protocol off if HostMC turned it on. */
static void door_restore(void)
{
    char port[16];
    door_stop();
    snprintf(port, sizeof port, "%d", door.port);
    door.on = 0;                                             /* server_port() reads the file again */
    props_set("server-port", port);
    props_set("server-ip", door.ip);
    if (door.proxy) paper_proxy_protocol(0);
    unlink(DOOR_FILE);
    memset(&door, 0, sizeof door);
}

/* Paper new enough for config/paper-global.yml (and its proxies section). */
int paper_takes_proxy(const char *version)
{
    return !strcmp(version, "latest") || !version_newer("1.19.4", version);
}

int door_prepare(char *why, int n)
{
    int want = hs.relay && relay_proxy_on() && hi.ok && !strcmp(hi.type, "paper") && paper_takes_proxy(hi.version) && !geyser_on();
    char v[16];
    why[0] = 0;
    if (!want) {
        if (door.on) {
            door_restore();
            sv_note("players come in at the server's port again, without the doorway.");
        }
        return 1;
    }
    if (!door.on) {
        int players = server_port(), paper = free_port(players);
        if (!paper) {
            snprintf(why, n, "There's no free port for the server behind the doorway.");
            return 0;
        }
        props_get("server-ip", door.ip, sizeof door.ip);
        door.port = players;
        door.paper = paper;
        door.on = 1;
    } else if (!port_free(door.paper) && !(door.paper = free_port(door.port))) {   /* taken since */
        door_restore();
        snprintf(why, n, "There's no free port for the server behind the doorway.");
        return 0;
    }
    door.proxy = 1;
    door_save();
    snprintf(v, sizeof v, "%d", door.paper);
    if (!props_set("server-port", v) || !props_set("server-ip", "127.0.0.1") || !paper_proxy_protocol(1)) {
        door_restore();
        snprintf(why, n, "HostMC couldn't change the server's settings for players' addresses.");
        return 0;
    }
    if (!door_start(why, n)) {
        door_restore();
        return 0;
    }
    return 1;
}

int door_set_players_port(int port)
{
    if (!door.on || port < 1 || port > 65535) return 0;
    door.port = port;
    if (door.paper == port) door.paper = free_port(port);
    door_save();
    return 1;
}

void door_server_stopped(void)
{
    door_stop();
}

/* After sv_attach(): a doorway left running stays with the server (door_poll brings it back if
 * it stopped), and goes without it. */
void door_attach(void)
{
    if (sv.state == SV_STOPPED && door_alive()) door_stop();
}

void door_poll(void)
{
    uint64_t now = ui_ms();
    char why[200];
    if (!door.on || now - door_checked < 3000) return;
    door_checked = now;
    if (sv.state == SV_RUNNING && !door_alive()) {
        sv_note("the doorway on the server's port had stopped, so HostMC opened it again.");
        if (!door_start(why, sizeof why)) sv_note(why);
    }
}
