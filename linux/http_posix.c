/* The HTTP client on POSIX sockets (net.h), for PCs and Raspberry Pis (net_posix.c) and the
 * simulator (sim/net_sim.c).
 *
 * Addresses are "host:port", where host is an IPv4 address or a name, or "https://host[:port]"
 * (port 443 when it's left out). A name's addresses are tried in turn, so one that doesn't work
 * (IPv6 on a network where IPv6 is broken, say) costs a few seconds instead of the request.
 *
 * HTTPS goes through the system's OpenSSL, opened the first time an https:// address is used
 * rather than linked. libssl is on every Debian, Ubuntu and Raspberry Pi OS (ca-certificates
 * needs it), but its headers aren't, and PCs build their own updates from the Store without
 * root: a build that needed libssl-dev would stop them updating. Certificates are checked
 * against the system's (ca-certificates, or SSL_CERT_FILE) and have to name the host, and the
 * connection is TLS 1.2 or newer. Without OpenSSL, https:// addresses fail with NE_SECURE.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "http_posix.h"
#include "net.h"
#ifdef XENITH_SIM
#include "platform.h"                       /* plat_sleep_us, for XSIM_NET_TIME */
#endif

#define IDLE_TIMEOUT_MS 10000
#define TRY_MS          3000     /* how long one of a name's addresses gets before the next is tried */
#define POLL_MS         30

/* ------------------------------------------------------------- OpenSSL, opened when needed */
/* The parts of OpenSSL's API used here, the same in 1.1.1 and 3.x, with its numbers for them. */
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_method_st SSL_METHOD;

#define OSSL_ERROR_WANT_READ          2
#define OSSL_ERROR_WANT_WRITE         3
#define OSSL_ERROR_ZERO_RETURN        6
#define OSSL_VERIFY_PEER              1
#define OSSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define OSSL_CTRL_SET_MIN_PROTO       123
#define OSSL_NAMETYPE_HOST_NAME       0
#define OSSL_TLS1_2_VERSION           0x0303
#define OSSL_X509_V_OK                0

static struct {
    int tried, ok;
    SSL_CTX *ctx;
    const SSL_METHOD *(*TLS_client_method)(void);
    SSL_CTX *(*SSL_CTX_new)(const SSL_METHOD *method);
    int (*SSL_CTX_set_default_verify_paths)(SSL_CTX *ctx);
    void (*SSL_CTX_set_verify)(SSL_CTX *ctx, int mode, int (*cb)(int, void *));
    long (*SSL_CTX_ctrl)(SSL_CTX *ctx, int cmd, long larg, void *parg);
    SSL *(*SSL_new)(SSL_CTX *ctx);
    void (*SSL_free)(SSL *s);
    int (*SSL_set_fd)(SSL *s, int fd);
    long (*SSL_ctrl)(SSL *s, int cmd, long larg, void *parg);
    int (*SSL_set1_host)(SSL *s, const char *host);
    int (*SSL_connect)(SSL *s);
    int (*SSL_read)(SSL *s, void *buf, int num);
    int (*SSL_write)(SSL *s, const void *buf, int num);
    int (*SSL_get_error)(const SSL *s, int ret);
    long (*SSL_get_verify_result)(const SSL *s);
    void (*ERR_clear_error)(void);
    const char *(*X509_verify_cert_error_string)(long n);   /* only for the log */
} ossl;

/* 1 when OpenSSL is there and set up: its library opened once, one context for every request. */
static int tls_load(void)
{
    static const char *const libs[] = {"libssl.so.3", "libssl.so.1.1"};
    void *lib = NULL;
    unsigned i;

    if (ossl.tried) return ossl.ok;
    ossl.tried = 1;
    for (i = 0; i < sizeof libs / sizeof *libs && !lib; i++) lib = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(stderr, "net: OpenSSL (libssl.so.3) isn't installed, so https:// addresses can't be used\n");
        return 0;
    }
#define FIND(f) \
    if (!(ossl.f = (__typeof__(ossl.f))dlsym(lib, #f))) { \
        fprintf(stderr, "net: this OpenSSL has no %s, so https:// addresses can't be used\n", #f); \
        return 0; \
    }
    FIND(TLS_client_method) FIND(SSL_CTX_new) FIND(SSL_CTX_set_default_verify_paths) FIND(SSL_CTX_set_verify)
    FIND(SSL_CTX_ctrl) FIND(SSL_new) FIND(SSL_free) FIND(SSL_set_fd) FIND(SSL_ctrl) FIND(SSL_set1_host)
    FIND(SSL_connect) FIND(SSL_read) FIND(SSL_write) FIND(SSL_get_error) FIND(SSL_get_verify_result)
    FIND(ERR_clear_error)
#undef FIND
    ossl.X509_verify_cert_error_string = (__typeof__(ossl.X509_verify_cert_error_string))
                                         dlsym(lib, "X509_verify_cert_error_string");

    ossl.ctx = ossl.SSL_CTX_new(ossl.TLS_client_method());
    if (!ossl.ctx || ossl.SSL_CTX_set_default_verify_paths(ossl.ctx) != 1 ||
            ossl.SSL_CTX_ctrl(ossl.ctx, OSSL_CTRL_SET_MIN_PROTO, OSSL_TLS1_2_VERSION, NULL) != 1) {
        fprintf(stderr, "net: OpenSSL wouldn't set up, so https:// addresses can't be used\n");
        return 0;
    }
    ossl.SSL_CTX_set_verify(ossl.ctx, OSSL_VERIFY_PEER, NULL);   /* a certificate that fails ends the handshake */
    ossl.ok = 1;
    return 1;
}

/* ---------------------------------------------------------------- addresses */
struct target {
    char name[NET_ADDR_LEN];        /* what to look up, and what the certificate has to name */
    char port[8];
    char host_header[NET_ADDR_LEN];
    int tls;
};

/* "https://name[:port]" or "host:port". */
static int parse_target(const char *addr, struct target *t)
{
    const char *rest = addr, *colon;
    int n;

    memset(t, 0, sizeof *t);
    if (!strncmp(addr, "https://", 8)) {
        t->tls = 1;
        rest = addr + 8;
    }
    colon = strrchr(rest, ':');
    n = colon ? (int)(colon - rest) : (int)strlen(rest);
    if (n <= 0 || n >= (int)sizeof t->name || (int)strcspn(rest, " /\\@?#\t\r\n") < (int)strlen(rest)) return 0;
    memcpy(t->name, rest, n);
    if (colon) {
        int port = atoi(colon + 1);
        if (!colon[1] || strspn(colon + 1, "0123456789") != strlen(colon + 1) || port <= 0 || port > 65535) return 0;
        snprintf(t->port, sizeof t->port, "%d", port);
    } else if (t->tls) {
        strcpy(t->port, "443");
    } else {
        return 0;                                       /* plain addresses always say their port */
    }
    /* The Host header is the address as written, without https:// (or the port when it's 443). */
    snprintf(t->host_header, sizeof t->host_header, "%s", t->tls && !strcmp(t->port, "443") ? t->name : rest);
    return 1;
}

static int is_ip(const char *name)
{
    unsigned char a[16];
    return inet_pton(AF_INET, name, a) == 1 || inet_pton(AF_INET6, name, a) == 1;
}

/* ---------------------------------------------------------------- one connection */
struct conn {
    int fd;
    SSL *tls;                       /* NULL for plain HTTP */
};

#define IO_WAIT  -1                 /* nothing moved: wait for *want (POLLIN or POLLOUT), then try again */
#define IO_ERROR -2

#define WAIT_CANCELLED -2

#ifdef XENITH_SIM
/* XSIM_NET_TIME=1: virtual time passes while waiting on the network, as real time does on a
 * console, so a screen that draws during a download (the Store's downloads) gets its frames and
 * reads its buttons then. Off, a request takes no virtual time at all. */
static int net_time(void)
{
    static int on = -1;
    if (on < 0) on = getenv("XSIM_NET_TIME") && !strcmp(getenv("XSIM_NET_TIME"), "1");
    return on;
}
#endif

/* Waits up to POLL_MS for fd, keeping the screen and sound going: 1 ready, 0 not yet, -1 broken,
 * WAIT_CANCELLED when net_cancel() was called (the Store's downloads queue stopping one). */
static int wait_fd(int fd, short ev)
{
    struct pollfd p = {fd, ev, 0};
    int r = poll(&p, 1, POLL_MS);
#ifdef XENITH_SIM
    if (r == 0 && net_time()) plat_sleep_us(POLL_MS * 1000);
#endif
    net_idle();
    if (net_cancel_asked()) return WAIT_CANCELLED;
    if (r < 0) return errno == EINTR ? 0 : -1;
    return r > 0;
}

/* Connects to one of t's addresses, trying the next when one fails or is slow. */
static int connect_any(const struct target *t, int connect_ms, int *fd_out)
{
    struct addrinfo hints, *res, *ai;
    int left = connect_ms;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(t->name, t->port, &hints, &res) != 0) return NE_CONNECT;   /* no such name, or no DNS */
    for (ai = res; ai && left > 0; ai = ai->ai_next) {
        int limit = ai->ai_next && left > TRY_MS ? TRY_MS : left, waited = 0;
        int fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0) continue;
        fcntl(fd, F_SETFL, O_NONBLOCK);
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0 || errno == EINPROGRESS) {
            for (;;) {
                int r = wait_fd(fd, POLLOUT), e = 0;
                socklen_t el = sizeof e;
                if (r == WAIT_CANCELLED) {
                    close(fd);
                    freeaddrinfo(res);
                    return NE_CANCELLED;
                }
                if (r < 0) break;
                if (r == 0) {
                    if ((waited += POLL_MS) > limit) break;
                    continue;
                }
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &el);
                if (e) break;
                freeaddrinfo(res);
                *fd_out = fd;
                return 0;
            }
        }
        left -= waited + POLL_MS;
        close(fd);
    }
    freeaddrinfo(res);
    return NE_CONNECT;
}

static void tls_why(SSL *s, const struct target *t)
{
    long v = ossl.SSL_get_verify_result(s);
    if (v == OSSL_X509_V_OK)
        fprintf(stderr, "net: no secure connection to %s\n", t->name);
    else if (ossl.X509_verify_cert_error_string)
        fprintf(stderr, "net: %s's certificate didn't check out: %s\n", t->name, ossl.X509_verify_cert_error_string(v));
    else
        fprintf(stderr, "net: %s's certificate didn't check out (error %ld)\n", t->name, v);
}

/* TLS over the connected socket, with t's name in the handshake (SNI) and checked against the
 * certificate. *idle counts the milliseconds spent waiting. */
static int tls_start(struct conn *c, const struct target *t, int *idle)
{
    if (!tls_load()) return NE_SECURE;
    c->tls = ossl.SSL_new(ossl.ctx);
    if (!c->tls) return NE_MEMORY;
    if (ossl.SSL_set_fd(c->tls, c->fd) != 1 || ossl.SSL_set1_host(c->tls, t->name) != 1) return NE_SECURE;
    if (!is_ip(t->name) &&
            ossl.SSL_ctrl(c->tls, OSSL_CTRL_SET_TLSEXT_HOSTNAME, OSSL_NAMETYPE_HOST_NAME, (void *)t->name) != 1)
        return NE_SECURE;
    for (;;) {
        int r, e;
        ossl.ERR_clear_error();
        r = ossl.SSL_connect(c->tls);
        if (r == 1) break;
        e = ossl.SSL_get_error(c->tls, r);
        if (e != OSSL_ERROR_WANT_READ && e != OSSL_ERROR_WANT_WRITE) {
            tls_why(c->tls, t);
            return NE_SECURE;
        }
        r = wait_fd(c->fd, e == OSSL_ERROR_WANT_READ ? POLLIN : POLLOUT);
        if (r == WAIT_CANCELLED) return NE_CANCELLED;
        if (r < 0) return NE_SECURE;
        if (r == 0 && (*idle += POLL_MS) > IDLE_TIMEOUT_MS) return NE_TIMEOUT;
    }
    if (ossl.SSL_get_verify_result(c->tls) != OSSL_X509_V_OK) {   /* the handshake checked it already */
        tls_why(c->tls, t);
        return NE_SECURE;
    }
    return 0;
}

static int io_send(struct conn *c, const char *p, int n, short *want)
{
    int r, e;
    if (!c->tls) {
        r = (int)send(c->fd, p, (size_t)n, MSG_NOSIGNAL);
        if (r >= 0) return r;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return IO_ERROR;
        *want = POLLOUT;
        return IO_WAIT;
    }
    ossl.ERR_clear_error();
    r = ossl.SSL_write(c->tls, p, n);     /* after IO_WAIT, called again with the same p and n */
    if (r > 0) return r;
    e = ossl.SSL_get_error(c->tls, r);
    if (e != OSSL_ERROR_WANT_READ && e != OSSL_ERROR_WANT_WRITE) return IO_ERROR;
    *want = e == OSSL_ERROR_WANT_READ ? POLLIN : POLLOUT;
    return IO_WAIT;
}

/* Bytes read, 0 when the server has closed, or IO_WAIT / IO_ERROR. */
static int io_recv(struct conn *c, char *p, int n, short *want)
{
    int r, e;
    if (!c->tls) {
        r = (int)recv(c->fd, p, (size_t)n, 0);
        if (r >= 0) return r;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return IO_ERROR;
        *want = POLLIN;
        return IO_WAIT;
    }
    ossl.ERR_clear_error();
    r = ossl.SSL_read(c->tls, p, n);      /* data already decrypted comes back without a wait */
    if (r > 0) return r;
    e = ossl.SSL_get_error(c->tls, r);
    if (e == OSSL_ERROR_ZERO_RETURN) return 0;
    if (e != OSSL_ERROR_WANT_READ && e != OSSL_ERROR_WANT_WRITE) return IO_ERROR;
    *want = e == OSSL_ERROR_WANT_READ ? POLLIN : POLLOUT;
    return IO_WAIT;
}

static void conn_close(struct conn *c)
{
    if (c->tls) ossl.SSL_free(c->tls);    /* no goodbye: the reply is in, or it went wrong */
    if (c->fd >= 0) close(c->fd);
    c->tls = NULL;
    c->fd = -1;
}

/* ---------------------------------------------------------------- one request */
int posix_http_once(const char *host, const char *method, const char *path,
                    const char *body, int body_len, int max_bytes,
                    net_progress_fn progress, void *ctx, struct http_resp *out, int connect_ms)
{
    struct target t;
    struct conn c = {-1, NULL};
    int req_len, sent = 0, len = 0, cap = 16 * 1024, idle = 0, header_end = -1, cl = -1, err, r;
    char *req = NULL, *buf = NULL;
    short want = POLLIN;

    memset(out, 0, sizeof *out);
    if (!host || !parse_target(host, &t)) return out->status = NE_BAD_HOST;
    req_len = net_build_request(&req, t.host_header, method, path, body, body_len);
    if (req_len < 0) return out->status = NE_MEMORY;
    if ((err = connect_any(&t, connect_ms, &c.fd)) != 0) goto fail;
    if (t.tls && (err = tls_start(&c, &t, &idle)) != 0) goto fail;

    idle = 0;
    while (sent < req_len) {
        int n = io_send(&c, req + sent, req_len - sent, &want);
        if (n > 0) { sent += n; idle = 0; continue; }
        if (n == IO_ERROR) { err = NE_RESET; goto fail; }
        if ((r = wait_fd(c.fd, want)) == WAIT_CANCELLED) { err = NE_CANCELLED; goto fail; }
        if (r < 0) { err = NE_RESET; goto fail; }
        if (r == 0 && (idle += POLL_MS) > IDLE_TIMEOUT_MS) { err = NE_TIMEOUT; goto fail; }
    }

    buf = malloc(cap);
    if (!buf) { err = NE_MEMORY; goto fail; }
    for (;;) {
        int n;
        if (len + 4096 + 1 > cap) {
            char *nb;
            cap *= 2;
            if (cap > max_bytes + 4096 + 1) cap = max_bytes + 4096 + 1;
            if (len + 1 >= cap) { err = NE_TOO_BIG; goto fail; }
            nb = realloc(buf, cap);
            if (!nb) { err = NE_MEMORY; goto fail; }
            buf = nb;
        }
        n = io_recv(&c, buf + len, cap - len - 1, &want);
        if (n == IO_WAIT) {
            if ((r = wait_fd(c.fd, want)) == WAIT_CANCELLED) { err = NE_CANCELLED; goto fail; }
            if (r < 0) { err = NE_RESET; goto fail; }
            if (r == 0 && (idle += POLL_MS) > IDLE_TIMEOUT_MS) { err = NE_TIMEOUT; goto fail; }
            continue;
        }
        net_idle();                           /* a reply that keeps coming mustn't stop the screen */
        if (net_cancel_asked()) { err = NE_CANCELLED; goto fail; }   /* nor a stop asked for meanwhile */
        if (n == 0) break;                    /* the server closed: that's the reply */
        if (n == IO_ERROR) {
            /* Over TLS, a server that closes without TLS's goodbye has ended its reply too, once
             * the headers are in; one cut short of its Content-Length is caught below. */
            if (c.tls && header_end >= 0) break;
            err = NE_RESET;
            goto fail;
        }
        idle = 0;
        len += n;
        if (len > max_bytes + 4096) { err = NE_TOO_BIG; goto fail; }
        if (header_end < 0) cl = net_content_length(buf, len, &header_end);
        if (progress && header_end >= 0) progress(len - header_end, cl, ctx);
        if (header_end >= 0 && cl >= 0 && len - header_end >= cl) break;
    }
    if (c.tls && header_end >= 0 && cl >= 0 && len - header_end < cl) { err = NE_RESET; goto fail; }
    conn_close(&c);
    free(req);
    return net_finish_response(buf, len, out);

fail:
    conn_close(&c);
    free(req);
    free(buf);
    return out->status = err;
}
