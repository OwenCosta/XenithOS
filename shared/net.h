#ifndef XENITH_NET_H
#define XENITH_NET_H

/* XenithOS networking. On the console this is wired Ethernet through libxenon's lwIP
 * (raw API, no DNS), so servers are addressed as "a.b.c.d:port". Calls are synchronous;
 * while they wait they call the idle hook so the screen and sound keep running. */

enum {
    NE_NO_NETWORK = -1,
    NE_BAD_HOST   = -2,
    NE_CONNECT    = -3,
    NE_RESET      = -4,
    NE_TIMEOUT    = -5,
    NE_TOO_BIG    = -6,
    NE_MEMORY     = -7,
    NE_BAD_REPLY  = -8,
};

int net_up(void);                   /* bring the network up (DHCP) if needed; 1 with an address */
int net_is_up(void);                /* 1 if it already has an address, never blocks */
const char *net_ip(void);           /* "192.168.1.20", or "" */

void net_set_idle_hook(void (*fn)(void));   /* called about 30 times a second while waiting */

typedef void (*net_progress_fn)(int received, int total, void *ctx);   /* total -1 if unknown */

struct http_resp {
    int status;         /* HTTP status, or a negative NE_ code */
    char *body;         /* inside the response buffer, NUL-terminated */
    int body_len;
    char *raw_;         /* owned buffer, release with http_free */
};

/* host is "a.b.c.d:port". body may be NULL. max_bytes caps the reply size. */
int http_request(const char *host, const char *method, const char *path,
                 const char *body, int body_len, int max_bytes,
                 net_progress_fn progress, void *ctx, struct http_resp *out);
void http_free(struct http_resp *r);

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
