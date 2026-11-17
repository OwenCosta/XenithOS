/* net.h on a PC: plain sockets, same behaviour as the console's lwIP client. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "net.h"
#include "platform.h"

#define IDLE_TIMEOUT_MS 10000

static int up;
int net_up(void) { up = 1; return 1; }
int net_is_up(void) { return up; }
const char *net_ip(void) { return up ? "127.0.0.1" : ""; }

/* XSIM_NET_TIME=1: virtual time passes while waiting on the network, as real time does on a
 * console, so a screen that draws during a download (the Store's downloads) gets its frames and
 * reads its buttons then. Off, a request takes no virtual time at all. */
static int net_time(void)
{
    static int on = -1;
    if (on < 0) on = getenv("XSIM_NET_TIME") && !strcmp(getenv("XSIM_NET_TIME"), "1");
    return on;
}

/* 1 when fd is ready, 0 not yet, -1 on an error, -2 when net_cancel() was called. */
static int wait_fd(int fd, short ev, int *idle_ms)
{
    struct pollfd p = {fd, ev, 0};
    int r = poll(&p, 1, 30);
    if (r == 0 && net_time()) plat_sleep_us(30000);
    net_idle();
    if (net_cancel_asked()) return -2;
    if (r == 0) { *idle_ms += 30; return 0; }
    if (r < 0) return -1;
    *idle_ms = 0;
    return 1;
}

int net_http_once(const char *host, const char *method, const char *path,
                  const char *body, int body_len, int max_bytes,
                  net_progress_fn progress, void *ctx, struct http_resp *out, int connect_ms)
{
    unsigned char ip[4];
    int port, fd, req_len, sent = 0, len = 0, cap = 16 * 1024, idle = 0, header_end = -1, cl = -1;
    struct sockaddr_in sa;
    char *req, *buf;

    memset(out, 0, sizeof *out);
    if (!net_parse_host(host, ip, &port)) return out->status = NE_BAD_HOST;
    req_len = net_build_request(&req, host, method, path, body, body_len);
    if (req_len < 0) return out->status = NE_MEMORY;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, ip, 4);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0 && errno != EINPROGRESS) {
        close(fd); free(req);
        return out->status = NE_CONNECT;
    }
    for (;;) {                                   /* wait for the connection */
        int r = wait_fd(fd, POLLOUT, &idle), err = 0;
        socklen_t el = sizeof err;
        if (r == -2) { close(fd); free(req); return out->status = NE_CANCELLED; }
        if (r < 0 || idle > connect_ms) { close(fd); free(req); return out->status = NE_CONNECT; }
        if (r == 0) continue;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
        if (err) { close(fd); free(req); return out->status = NE_CONNECT; }
        break;
    }
    while (sent < req_len) {
        int n = (int)send(fd, req + sent, req_len - sent, 0), w;
        if (n > 0) { sent += n; continue; }
        if (n < 0 && errno != EAGAIN) { close(fd); free(req); return out->status = NE_RESET; }
        w = wait_fd(fd, POLLOUT, &idle);
        if (w == -2) { close(fd); free(req); return out->status = NE_CANCELLED; }
        if (w < 0 || idle > IDLE_TIMEOUT_MS) { close(fd); free(req); return out->status = NE_TIMEOUT; }
    }
    free(req);

    buf = malloc(cap);
    for (;;) {
        int r = wait_fd(fd, POLLIN, &idle), n;
        if (r == -2) { close(fd); free(buf); return out->status = NE_CANCELLED; }
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
