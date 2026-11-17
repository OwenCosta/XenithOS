/* The parts of the HTTP client that don't depend on the network stack. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "audio.h"
#include "net.h"
#include "platform.h"

static void (*idle_hook)(void);
static uint64_t last_idle, last_audio;
static volatile int cancel_asked;

void net_set_idle_hook(void (*fn)(void))
{
    idle_hook = fn;
}

void net_cancel(void)
{
    cancel_asked = 1;
}

int net_cancel_asked(void)
{
    return cancel_asked;
}

void net_idle(void)
{
    uint64_t now = plat_time_us();
    if (now - last_audio >= 2000) {             /* keep sound fed while waiting on the network */
        last_audio = now;
        audio_update();
    }
    if (idle_hook && now - last_idle >= 33000) {
        last_idle = now;
        idle_hook();
    }
}

const char *net_error(int status)
{
    switch (status) {
    case NE_NO_NETWORK: return plat_kind() == PLAT_XBOX ? "No network. Plug an Ethernet cable into the Xbox." : "No network connection.";
    case NE_BAD_HOST:   return "XenithOS Cloud isn't set up on this machine.";
    case NE_CONNECT:    return "Couldn't reach XenithOS Cloud.";
    case NE_RESET:      return "The connection dropped.";
    case NE_TIMEOUT:    return "XenithOS Cloud stopped responding.";
    case NE_TOO_BIG:    return "The reply was too large.";
    case NE_MEMORY:     return "Out of memory.";
    case NE_BAD_REPLY:  return "XenithOS Cloud sent something unexpected.";
    case NE_CANCELLED:  return "Stopped.";
    default:            return "Network error.";
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

int net_parse_host(const char *host, unsigned char ip[4], int *port)
{
    int a, b, c, d, p = 80;
    if (!host || !*host) return 0;
    if (sscanf(host, "%d.%d.%d.%d:%d", &a, &b, &c, &d, &p) < 4) return 0;
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) return 0;
    if (p <= 0 || p > 65535) return 0;
    ip[0] = a; ip[1] = b; ip[2] = c; ip[3] = d;
    *port = p;
    return 1;
}

int net_build_request(char **out, const char *host, const char *method, const char *path,
                      const char *body, int body_len)
{
    char *req;
    int n;
    if (!body) body_len = 0;
    req = malloc(640 + body_len);
    if (!req) return -1;
    n = snprintf(req, 640,
                 "%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: XenithOS/0.4\r\n"
                 "Connection: close\r\n%s"
                 "Content-Length: %d\r\n\r\n",
                 method, path, host,
                 body_len ? "Content-Type: application/x-www-form-urlencoded\r\n" : "",
                 body_len);
    if (n < 0 || n >= 640) { free(req); return -1; }
    if (body_len) memcpy(req + n, body, body_len);
    *out = req;
    return n + body_len;
}

int net_content_length(const char *buf, int len, int *header_end)
{
    const char *p, *end = NULL;
    int i, cl = -1;
    for (i = 0; i + 3 < len; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            end = buf + i;
            break;
        }
    if (!end) return -1;
    *header_end = (int)(end - buf) + 4;
    for (p = buf; p < end; ) {
        const char *nl = p;
        while (nl < end && *nl != '\r') nl++;
        if (nl - p > 15 && !strncasecmp(p, "Content-Length:", 15))
            cl = atoi(p + 15);
        p = nl + 2;
    }
    return cl;
}

int net_finish_response(char *buf, int len, struct http_resp *out)
{
    int header_end = -1, cl;
    buf[len] = 0;
    cl = net_content_length(buf, len, &header_end);
    if (header_end < 0 || len < 12 || strncmp(buf, "HTTP/1.", 7)) {
        free(buf);
        return out->status = NE_BAD_REPLY;
    }
    out->status = atoi(buf + 9);
    out->raw_ = buf;
    out->body = buf + header_end;
    out->body_len = len - header_end;
    if (cl >= 0 && out->body_len > cl) out->body_len = cl;
    out->body[out->body_len] = 0;
    return out->status;
}

void http_free(struct http_resp *r)
{
    free(r->raw_);
    memset(r, 0, sizeof *r);
}

/* ---------------------------------------------------------------- the home-network route */
static char *route_active;
static int route_size;
static char route_lan[64], route_main[64];

void net_set_lan_route(char *active, int active_size, const char *main_addr)
{
    route_active = active && main_addr && main_addr[0] ? active : NULL;
    route_size = active_size;
    snprintf(route_lan, sizeof route_lan, "%s", route_active ? active : "");
    snprintf(route_main, sizeof route_main, "%s", route_active ? main_addr : "");
}

int net_lan_route_on(void)
{
    return route_active && route_lan[0];
}

int http_request(const char *host, const char *method, const char *path,
                 const char *body, int body_len, int max_bytes,
                 net_progress_fn progress, void *ctx, struct http_resp *out)
{
    char main_addr[64];
    int status;
    cancel_asked = 0;
    if (!host || !net_lan_route_on() || strcmp(host, route_lan))
        return net_http_once(host, method, path, body, body_len, max_bytes, progress, ctx, out, NET_CONNECT_MS);
    status = net_http_once(host, method, path, body, body_len, max_bytes, progress, ctx, out, NET_LAN_CONNECT_MS);
    if (status != NE_CONNECT) return status;
    /* Not on that network any more: the public address, now and from now on. */
    snprintf(main_addr, sizeof main_addr, "%s", route_main);
    if (!strcmp(route_active, route_lan)) snprintf(route_active, route_size, "%s", main_addr);
    route_lan[0] = 0;
    return net_http_once(main_addr, method, path, body, body_len, max_bytes, progress, ctx, out, NET_CONNECT_MS);
}

int http_probe(const char *host, const char *path, int connect_ms, int max_bytes, struct http_resp *out)
{
    cancel_asked = 0;
    return net_http_once(host, "GET", path, NULL, 0, max_bytes, NULL, NULL, out, connect_ms);
}
