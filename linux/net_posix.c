/* net.h on Linux: plain sockets. Unlike the console, addresses can be names ("example.com:8795")
 * and the network is whatever the machine already has. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "net.h"
#include "platform.h"

#define IDLE_TIMEOUT_MS 10000

static char ip_text[64];

/* The first IPv4 address on an interface that's up and isn't loopback. */
static int find_address(void)
{
    struct ifaddrs *list, *a;
    ip_text[0] = 0;
    if (getifaddrs(&list) != 0) return 0;
    for (a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
        inet_ntop(AF_INET, &((struct sockaddr_in *)a->ifa_addr)->sin_addr, ip_text, sizeof ip_text);
        break;
    }
    freeifaddrs(list);
    return ip_text[0] != 0;
}

int net_up(void) { return find_address(); }
int net_is_up(void) { return find_address(); }
const char *net_ip(void) { find_address(); return ip_text; }

static int wait_fd(int fd, short ev, int *idle_ms)
{
    struct pollfd p = {fd, ev, 0};
    int r = poll(&p, 1, 30);
    net_idle();
    if (r == 0) { *idle_ms += 30; return 0; }
    if (r < 0) return -1;
    *idle_ms = 0;
    return 1;
}

/* "host:port" -> a connected (or connecting) non-blocking socket. */
static int open_socket(const char *host, int *fd_out)
{
    char name[256], port[8], *colon;
    struct addrinfo hints, *res, *ai;
    int fd = -1;
    snprintf(name, sizeof name, "%s", host);
    colon = strrchr(name, ':');
    if (!colon || colon == name) return NE_BAD_HOST;
    *colon = 0;
    snprintf(port, sizeof port, "%s", colon + 1);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(name, port, &hints, &res) != 0) return NE_BAD_HOST;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        fcntl(fd, F_SETFL, O_NONBLOCK);
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0 || errno == EINPROGRESS) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return NE_CONNECT;
    *fd_out = fd;
    return 0;
}

int net_http_once(const char *host, const char *method, const char *path,
                  const char *body, int body_len, int max_bytes,
                  net_progress_fn progress, void *ctx, struct http_resp *out, int connect_ms)
{
    int fd, req_len, sent = 0, len = 0, cap = 16 * 1024, idle = 0, header_end = -1, cl = -1, err;
    char *req, *buf;

    memset(out, 0, sizeof *out);
    if (!host || !host[0]) return out->status = NE_BAD_HOST;
    req_len = net_build_request(&req, host, method, path, body, body_len);
    if (req_len < 0) return out->status = NE_MEMORY;
    if ((err = open_socket(host, &fd)) != 0) { free(req); return out->status = err; }

    for (;;) {                                   /* wait for the connection */
        int r = wait_fd(fd, POLLOUT, &idle), e = 0;
        socklen_t el = sizeof e;
        if (r < 0 || idle > connect_ms) { close(fd); free(req); return out->status = NE_CONNECT; }
        if (r == 0) continue;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &el);
        if (e) { close(fd); free(req); return out->status = NE_CONNECT; }
        break;
    }
    while (sent < req_len) {
        int n = (int)send(fd, req + sent, req_len - sent, 0);
        if (n > 0) { sent += n; continue; }
        if (n < 0 && errno != EAGAIN) { close(fd); free(req); return out->status = NE_RESET; }
        if (wait_fd(fd, POLLOUT, &idle) < 0 || idle > IDLE_TIMEOUT_MS) { close(fd); free(req); return out->status = NE_TIMEOUT; }
    }
    free(req);

    buf = malloc(cap);
    if (!buf) { close(fd); return out->status = NE_MEMORY; }
    for (;;) {
        int r = wait_fd(fd, POLLIN, &idle), n;
        if (r < 0) { close(fd); free(buf); return out->status = NE_RESET; }
        if (r == 0) {
            if (idle > IDLE_TIMEOUT_MS) { close(fd); free(buf); return out->status = NE_TIMEOUT; }
            continue;
        }
        if (len + 4096 + 1 > cap) {
            char *nb;
            cap *= 2;
            if (cap > max_bytes + 4096 + 1) cap = max_bytes + 4096 + 1;
            if (len + 1 >= cap) { close(fd); free(buf); return out->status = NE_TOO_BIG; }
            nb = realloc(buf, cap);
            if (!nb) { close(fd); free(buf); return out->status = NE_MEMORY; }
            buf = nb;
        }
        n = (int)recv(fd, buf + len, cap - len - 1, 0);
        if (n == 0) break;
        if (n < 0) { if (errno == EAGAIN) continue; close(fd); free(buf); return out->status = NE_RESET; }
        len += n;
        if (len > max_bytes + 4096) { close(fd); free(buf); return out->status = NE_TOO_BIG; }
        if (header_end < 0) cl = net_content_length(buf, len, &header_end);
        if (progress && header_end >= 0) progress(len - header_end, cl, ctx);
        if (header_end >= 0 && cl >= 0 && len - header_end >= cl) break;
    }
    close(fd);
    return net_finish_response(buf, len, out);
}
