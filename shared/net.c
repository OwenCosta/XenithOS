/* HTTP/1.0 client on lwIP's raw TCP API. See net.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include <network/network.h>
#include <lwip/tcp.h>
#include <lwip/ip_addr.h>
#include <ppc/timebase.h>

#include "net.h"

#define NE_NO_NETWORK  -1
#define NE_BAD_HOST    -2
#define NE_CONNECT     -3
#define NE_RESET       -4
#define NE_TIMEOUT     -5
#define NE_TOO_BIG     -6
#define NE_MEMORY      -7
#define NE_BAD_REPLY   -8

#define IDLE_TIMEOUT_MS 10000

static int inited, have_ip;
static char ip_text[20];

static void update_ip_text(void)
{
    uint32_t a = netif.ip_addr.addr;   /* network byte order == host order on PowerPC */
    snprintf(ip_text, sizeof ip_text, "%d.%d.%d.%d",
             (int)((a >> 24) & 0xff), (int)((a >> 16) & 0xff), (int)((a >> 8) & 0xff), (int)(a & 0xff));
}

int net_link(void)
{
    return inited ? (network_link_up() ? 1 : 0) : 0;
}

const char *net_ip(void)
{
    return have_ip ? ip_text : "";
}

int net_up(void)
{
    if (have_ip) return 1;

    if (!inited) {
        inited = 1;
        if (network_init() == NETWORK_INIT_SUCCESS && netif.ip_addr.addr) {
            have_ip = 1;
            update_ip_text();
        }
        return have_ip;
    }

    /* Retry: libxenon fell back to a static address last time, so ask DHCP again. */
    {
        ip_addr_t zero;
        uint64_t start = mftb();
        IP4_ADDR(&zero, 0, 0, 0, 0);
        netif_set_addr(&netif, &zero, &zero, &zero);
        dhcp_start(&netif);
        while (!netif.ip_addr.addr && tb_diff_msec(mftb(), start) < 12000)
            network_poll();
        if (netif.ip_addr.addr) {
            have_ip = 1;
            update_ip_text();
        }
    }
    return have_ip;
}

const char *net_error(int status)
{
    switch (status) {
    case NE_NO_NETWORK: return "NO NETWORK. PLUG IN ETHERNET";
    case NE_BAD_HOST:   return "SERVER ADDRESS NOT SET";
    case NE_CONNECT:    return "COULD NOT CONNECT TO SERVER";
    case NE_RESET:      return "CONNECTION DROPPED";
    case NE_TIMEOUT:    return "SERVER TIMED OUT";
    case NE_TOO_BIG:    return "REPLY TOO LARGE";
    case NE_MEMORY:     return "OUT OF MEMORY";
    case NE_BAD_REPLY:  return "BAD REPLY FROM SERVER";
    default:             return "NETWORK ERROR";
    }
}

void net_escape(const char *in, char *out, int out_size)
{
    static const char hex[] = "0123456789ABCDEF";
    int o = 0;
    for (; *in && o < out_size - 4; in++) {
        unsigned char ch = (unsigned char)*in;
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
            out[o++] = ch;
        } else {
            out[o++] = '%';
            out[o++] = hex[ch >> 4];
            out[o++] = hex[ch & 15];
        }
    }
    out[o] = 0;
}

/* ---------------------------------------------------------------- one request */
struct conn {
    struct tcp_pcb *pcb;
    char *buf;
    int len, cap, max;
    int header_end;         /* offset of body, or -1 until headers are complete */
    int content_length;     /* -1 if not sent */
    int done, err;
    uint64_t last_activity;
    const char *req;
    int req_len;
    net_progress_fn progress;
    void *ctx;
};

static void parse_headers(struct conn *c)
{
    char *end, *p;
    if (c->header_end >= 0) return;
    c->buf[c->len] = 0;
    end = strstr(c->buf, "\r\n\r\n");
    if (!end) return;
    c->header_end = (int)(end - c->buf) + 4;
    for (p = c->buf; p < end; ) {
        char *nl = strstr(p, "\r\n");
        if (!nl) break;
        if (!strncasecmp(p, "Content-Length:", 15))
            c->content_length = atoi(p + 15);
        p = nl + 2;
    }
}

static int body_complete(struct conn *c)
{
    return c->header_end >= 0 && c->content_length >= 0 &&
           c->len - c->header_end >= c->content_length;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    struct conn *c = arg;
    struct pbuf *q;

    if (!p) {                    /* server closed: the reply is complete */
        c->done = 1;
        return ERR_OK;
    }
    for (q = p; q; q = q->next) {
        if (c->len + q->len + 1 > c->cap) {
            int ncap = c->cap * 2;
            char *nb;
            while (ncap < c->len + q->len + 1) ncap *= 2;
            if (ncap > c->max + 1) ncap = c->max + 1;
            if (c->len + q->len + 1 > ncap) { c->err = NE_TOO_BIG; break; }
            nb = realloc(c->buf, ncap);
            if (!nb) { c->err = NE_MEMORY; break; }
            c->buf = nb;
            c->cap = ncap;
        }
        memcpy(c->buf + c->len, q->payload, q->len);
        c->len += q->len;
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    c->last_activity = mftb();

    parse_headers(c);
    if (c->progress && c->header_end >= 0)
        c->progress(c->len - c->header_end, c->content_length, c->ctx);
    if (body_complete(c)) c->done = 1;
    return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
    struct conn *c = arg;
    c->pcb = NULL;               /* lwIP already freed it */
    if (body_complete(c)) c->done = 1;
    else if (!c->done) c->err = (err == ERR_RST || err == ERR_ABRT) ? NE_RESET : NE_CONNECT;
}

static err_t on_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    struct conn *c = arg;
    if (err != ERR_OK) { c->err = NE_CONNECT; return err; }
    if (tcp_write(pcb, c->req, c->req_len, TCP_WRITE_FLAG_COPY) != ERR_OK) {
        c->err = NE_MEMORY;
        return ERR_OK;
    }
    tcp_output(pcb);
    c->last_activity = mftb();
    return ERR_OK;
}

static int parse_host(const char *host, ip_addr_t *ip, int *port)
{
    int a, b, cc, d, pt = 80;
    if (!host || !*host) return 0;
    if (sscanf(host, "%d.%d.%d.%d:%d", &a, &b, &cc, &d, &pt) < 4) return 0;
    if (a < 0 || a > 255 || b < 0 || b > 255 || cc < 0 || cc > 255 || d < 0 || d > 255) return 0;
    if (pt <= 0 || pt > 65535) return 0;
    IP4_ADDR(ip, a, b, cc, d);
    *port = pt;
    return 1;
}

int http_request(const char *host, const char *method, const char *path,
                 const char *body, int body_len, int max_bytes,
                 net_progress_fn progress, void *ctx, struct http_resp *out)
{
    struct conn c;
    ip_addr_t ip;
    int port;
    char *req;

    memset(out, 0, sizeof *out);
    if (!parse_host(host, &ip, &port)) return out->status = NE_BAD_HOST;
    if (!net_up()) return out->status = NE_NO_NETWORK;
    if (!body) body_len = 0;

    req = malloc(512 + body_len);
    if (!req) return out->status = NE_MEMORY;
    {
        int n = snprintf(req, 512,
                         "%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: XENITH/0.3\r\n"
                         "Connection: close\r\n%s"
                         "Content-Length: %d\r\n\r\n",
                         method, path, host,
                         body_len ? "Content-Type: application/x-www-form-urlencoded\r\n" : "",
                         body_len);
        if (body_len) memcpy(req + n, body, body_len);
        memset(&c, 0, sizeof c);
        c.req = req;
        c.req_len = n + body_len;
    }

    c.cap = 16 * 1024;
    c.max = max_bytes;
    c.buf = malloc(c.cap);
    c.header_end = -1;
    c.content_length = -1;
    c.progress = progress;
    c.ctx = ctx;
    if (!c.buf) { free(req); return out->status = NE_MEMORY; }

    c.pcb = tcp_new();
    if (!c.pcb) { free(req); free(c.buf); return out->status = NE_MEMORY; }
    tcp_arg(c.pcb, &c);
    tcp_recv(c.pcb, on_recv);
    tcp_err(c.pcb, on_err);
    c.last_activity = mftb();
    if (tcp_connect(c.pcb, &ip, port, on_connected) != ERR_OK) {
        tcp_abort(c.pcb);
        free(req);
        free(c.buf);
        return out->status = NE_CONNECT;
    }

    while (!c.done && !c.err) {
        network_poll();
        if (tb_diff_msec(mftb(), c.last_activity) > IDLE_TIMEOUT_MS) c.err = NE_TIMEOUT;
    }

    if (c.pcb) {                         /* detach callbacks before closing */
        tcp_arg(c.pcb, NULL);
        tcp_recv(c.pcb, NULL);
        tcp_err(c.pcb, NULL);
        if (tcp_close(c.pcb) != ERR_OK) tcp_abort(c.pcb);
    }
    free(req);

    if (c.err) { free(c.buf); return out->status = c.err; }

    c.buf[c.len] = 0;
    parse_headers(&c);
    if (c.header_end < 0 || strncmp(c.buf, "HTTP/1.", 7) || c.len < 12) {
        free(c.buf);
        return out->status = NE_BAD_REPLY;
    }
    out->status = atoi(c.buf + 9);
    out->raw_ = c.buf;
    out->body = c.buf + c.header_end;
    out->body_len = c.len - c.header_end;
    if (c.content_length >= 0 && out->body_len > c.content_length) out->body_len = c.content_length;
    out->body[out->body_len] = 0;
    return out->status;
}

void http_free(struct http_resp *r)
{
    free(r->raw_);
    memset(r, 0, sizeof *r);
}
