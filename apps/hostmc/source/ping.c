/* A Minecraft server list ping, the question a player's game asks a server before it joins.
 * HostMC asks it of the server through a route (the player's own VPS) to check from the
 * internet that friends really reach it. It goes a step at a time from the frame loop, so the
 * screen keeps moving; only looking the name up waits, and that's quick. */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hostmc.h"
#include "ui.h"

static int put_varint(unsigned char *p, int v)
{
    unsigned u = (unsigned)v;
    int n = 0;
    do {
        unsigned char b = u & 0x7f;
        u >>= 7;
        if (u) b |= 0x80;
        p[n++] = b;
    } while (u);
    return n;
}

/* A VarInt at p: the bytes it takes, 0 if more are needed, -1 if it's broken. */
static int get_varint(const unsigned char *p, const unsigned char *end, int *out)
{
    unsigned v = 0;
    int i;
    for (i = 0; i < 5; i++) {
        if (p + i >= end) return 0;
        v |= (unsigned)(p[i] & 0x7f) << (7 * i);
        if (!(p[i] & 0x80)) {
            *out = (int)v;
            return i + 1;
        }
    }
    return -1;
}

static void finish(struct mcping *p, int result)
{
    if (p->fd >= 0) close(p->fd);
    p->fd = -1;
    p->result = result;
    p->state = PING_DONE;
}

int mcping_start(struct mcping *p, const char *host, int port, int timeout_ms)
{
    struct addrinfo hints, *res = NULL;
    char ports[8];
    unsigned char body[300];
    int bl = 0, hl = (int)strlen(host), fd, n;
    memset(p, 0, sizeof *p);
    p->fd = -1;
    p->state = PING_CONNECTING;
    if (hl > 255) hl = 255;
    snprintf(ports, sizeof ports, "%d", port);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, ports, &hints, &res) != 0 || !res) {
        finish(p, PING_NO_HOST);
        return 0;
    }
    fd = socket(res->ai_family, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        finish(p, PING_UNREACHABLE);
        return 0;
    }
    p->fd = fd;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0 && errno != EINPROGRESS) {
        int e = errno;
        freeaddrinfo(res);
        finish(p, e == ECONNREFUSED ? PING_REFUSED : PING_UNREACHABLE);
        return 0;
    }
    freeaddrinfo(res);
    /* The handshake (protocol -1: "just asking", next state 1: status), then the status request. */
    body[bl++] = 0x00;
    bl += put_varint(body + bl, -1);
    bl += put_varint(body + bl, hl);
    memcpy(body + bl, host, (size_t)hl);
    bl += hl;
    body[bl++] = (unsigned char)(port >> 8);
    body[bl++] = (unsigned char)port;
    bl += put_varint(body + bl, 1);
    n = put_varint(p->out, bl);
    memcpy(p->out + n, body, (size_t)bl);
    n += bl;
    p->out[n++] = 1;
    p->out[n++] = 0;
    p->out_len = n;
    p->deadline = ui_ms() + (uint64_t)timeout_ms;
    return 1;
}

/* The answer: a packet of id 0 holding JSON with the version and the players. */
static int parse(struct mcping *p)
{
    const unsigned char *s = (const unsigned char *)p->in, *end = s + p->in_len;
    int len, id, sl, k;
    const char *json, *v;
    if ((k = get_varint(s, end, &len)) <= 0) return k;
    s += k;
    if (len <= 0) return -1;
    if (end - s < len && p->in_len < (int)sizeof p->in - 1) return 0;    /* more to come */
    if ((k = get_varint(s, end, &id)) <= 0 || id != 0) return -1;
    s += k;
    if ((k = get_varint(s, end, &sl)) <= 0) return -1;
    s += k;
    if (s >= end || *s != '{') return -1;
    p->in[p->in_len] = 0;
    json = (const char *)s;          /* a long answer (a server icon) may be cut short: enough */
    v = js_key(json, "version");
    js_str(js_key(v, "name"), p->version, sizeof p->version);
    p->online = (int)js_num(js_key(js_key(json, "players"), "online"));
    p->max = (int)js_num(js_key(js_key(json, "players"), "max"));
    return 1;
}

int mcping_poll(struct mcping *p)
{
    struct pollfd pf;
    if (p->state == PING_DONE || p->state == PING_IDLE) return 1;
    if (ui_ms() > p->deadline) {
        finish(p, p->state == PING_CONNECTING ? PING_TIMEOUT : p->in_len ? PING_NOT_MC : PING_SILENT);
        return 1;
    }
    pf.fd = p->fd;
    pf.events = p->state == PING_CONNECTING ? POLLOUT : POLLIN;
    pf.revents = 0;
    if (poll(&pf, 1, 0) <= 0) return 0;
    if (p->state == PING_CONNECTING) {
        int err = 0;
        socklen_t el = sizeof err;
        getsockopt(p->fd, SOL_SOCKET, SO_ERROR, &err, &el);
        if (err) {
            finish(p, err == ECONNREFUSED ? PING_REFUSED : err == ETIMEDOUT ? PING_TIMEOUT : PING_UNREACHABLE);
            return 1;
        }
        if (send(p->fd, p->out, (size_t)p->out_len, MSG_NOSIGNAL) != p->out_len) {
            finish(p, PING_SILENT);
            return 1;
        }
        p->state = PING_READING;
        return 0;
    }
    {
        ssize_t n = recv(p->fd, p->in + p->in_len, sizeof p->in - 1 - (size_t)p->in_len, 0);
        int r;
        if (n <= 0) {
            finish(p, p->in_len ? PING_NOT_MC : PING_SILENT);
            return 1;
        }
        p->in_len += (int)n;
        r = parse(p);
        if (r == 0) return 0;
        finish(p, r > 0 ? PING_OK : PING_NOT_MC);
        return 1;
    }
}

void mcping_cancel(struct mcping *p)
{
    if (p->state != PING_DONE && p->state != PING_IDLE) finish(p, PING_CANCELLED);
}
