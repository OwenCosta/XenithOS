#ifndef XENITH_NET_H
#define XENITH_NET_H

/* XenithOS networking. On the console this is wired Ethernet through libxenon's lwIP
 * (raw API, no DNS), so servers are addressed as "a.b.c.d:port". PCs and Raspberry Pis
 * (linux/http_posix.c) also take names ("example.com:8795") and "https://name[:port]", which
 * is HTTPS with the certificate checked. Calls are synchronous; while they wait they call the
 * idle hook so the screen and sound keep running. */

enum {
    NE_NO_NETWORK = -1,
    NE_BAD_HOST   = -2,
    NE_CONNECT    = -3,
    NE_RESET      = -4,
    NE_TIMEOUT    = -5,
    NE_TOO_BIG    = -6,
    NE_MEMORY     = -7,
    NE_BAD_REPLY  = -8,
    NE_SECURE     = -9,     /* an https:// address: no secure connection, or its certificate didn't check out */
    NE_CANCELLED  = -10,    /* net_cancel() while it waited */
};

#define NET_ADDR_LEN 128    /* room for "https://<name>:<port>" */

int net_up(void);                   /* bring the network up (DHCP) if needed; 1 with an address */
int net_is_up(void);                /* 1 if it already has an address, never blocks */
const char *net_ip(void);           /* "192.168.1.20", or "" */

void net_set_idle_hook(void (*fn)(void));   /* called about 30 times a second while waiting */

/* Called from the idle hook or a progress callback: the request waiting now stops and returns
 * NE_CANCELLED (the Store's downloads, B on the one downloading). The next request starts
 * without it. */
void net_cancel(void);
int  net_cancel_asked(void);                /* for the network stacks, as they wait */

typedef void (*net_progress_fn)(int received, int total, void *ctx);   /* total -1 if unknown */

struct http_resp {
    int status;         /* HTTP status, or a negative NE_ code */
    char *body;         /* inside the response buffer, NUL-terminated */
    int body_len;
    char *raw_;         /* owned buffer, release with http_free */
};

/* host is "a.b.c.d:port" (see the top for PCs). body may be NULL. max_bytes caps the reply size. */
int http_request(const char *host, const char *method, const char *path,
                 const char *body, int body_len, int max_bytes,
                 net_progress_fn progress, void *ctx, struct http_resp *out);
void http_free(struct http_resp *r);

/* A GET to one address that gives up connecting after connect_ms, never taking the route. */
int http_probe(const char *host, const char *path, int connect_ms, int max_bytes, struct http_resp *out);

/* The home-network route (config.c, route.txt): *active (cfg.server) holds XenithOS Cloud's
 * address on the network it runs on, which answers in milliseconds where the public address
 * takes a trip out to the relay and back. A request there that can't connect within
 * NET_LAN_CONNECT_MS goes to main_addr instead, and so does everything after it (*active
 * becomes main_addr): the console has left that network. main_addr is the address used away
 * from home (on a PC, the HTTPS one when it has it). NULL turns the route off. */
void net_set_lan_route(char *active, int active_size, const char *main_addr);
int net_lan_route_on(void);

#define NET_CONNECT_MS     10000
#define NET_LAN_CONNECT_MS 700

/* What each network stack provides: one request, giving up on connecting after connect_ms.
 * NE_CONNECT always means nothing was sent. */
int net_http_once(const char *host, const char *method, const char *path,
                  const char *body, int body_len, int max_bytes,
                  net_progress_fn progress, void *ctx, struct http_resp *out, int connect_ms);

const char *net_error(int status);  /* readable text for a negative status */
void net_escape(const char *in, char *out, int out_size);   /* form / query escaping */

/* Shared by both implementations. */
void net_idle(void);
int  net_parse_host(const char *host, unsigned char ip[4], int *port);
int  net_build_request(char **out, const char *host, const char *method, const char *path,
                       const char *body, int body_len);
int  net_finish_response(char *buf, int len, struct http_resp *out);   /* takes ownership of buf */
int  net_content_length(const char *buf, int len, int *header_end);    /* -1 if not known yet */

#endif
