/* HTTP/1.0 client on lwIP's raw TCP API (Xbox 360). See net.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <network/network.h>
#include <lwip/tcp.h>
#include <lwip/ip_addr.h>
#include <ppc/timebase.h>

#include "net.h"
#include "platform.h"

#define IDLE_TIMEOUT_MS 10000

static int inited, have_ip;
static char ip_text[20];

static void update_ip_text(void)
{
    uint32_t a = netif.ip_addr.addr;   /* network byte order == host order on PowerPC */
    snprintf(ip_text, sizeof ip_text, "%d.%d.%d.%d",
             (int)((a >> 24) & 0xff), (int)((a >> 16) & 0xff), (int)((a >> 8) & 0xff), (int)(a & 0xff));
}

int net_is_up(void)
{
    return have_ip;
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
        net_idle();
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
        while (!netif.ip_addr.addr && tb_diff_msec(mftb(), start) < 12000) {
            network_poll();
            net_idle();
        }
        if (netif.ip_addr.addr) {
            have_ip = 1;
            update_ip_text();
        }
    }
    return have_ip;
}

/* ---------------------------------------------------------------- one request */
struct conn {
    struct tcp_pcb *pcb;
    char *buf;
    int len, cap, max;
    int header_end, content_length;
    int done, err;
    uint64_t last_activity;
    const char *req;
    int req_len;
    net_progress_fn progress;
    void *ctx;
};

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

    if (c->header_end < 0)
        c->content_length = net_content_length(c->buf, c->len, &c->header_end);
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

int http_request(const char *host, const char *method, const char *path,
                 const char *body, int body_len, int max_bytes,
                 net_progress_fn progress, void *ctx, struct http_resp *out)
{
    struct conn c;
    unsigned char ipb[4];
    ip_addr_t ip;
    int port;
    char *req;
    int req_len;

    memset(out, 0, sizeof *out);
    if (!net_parse_host(host, ipb, &port)) return out->status = NE_BAD_HOST;
    if (!net_up()) return out->status = NE_NO_NETWORK;
    IP4_ADDR(&ip, ipb[0], ipb[1], ipb[2], ipb[3]);

    req_len = net_build_request(&req, host, method, path, body, body_len);
    if (req_len < 0) return out->status = NE_MEMORY;

    memset(&c, 0, sizeof c);
    c.req = req;
    c.req_len = req_len;
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
        net_idle();
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
    return net_finish_response(c.buf, c.len, out);
}
