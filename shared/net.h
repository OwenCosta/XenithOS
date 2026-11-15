#ifndef XENITH_NET_H
#define XENITH_NET_H

/* XENITH networking: wired Ethernet through libxenon's lwIP (raw API, no sockets, no DNS).
 * Everything is synchronous: calls poll the network until they finish or time out. */

/* Bring the network up (DHCP) the first time it's needed. Returns 1 with an address.
 * A failed attempt can be retried, e.g. after the cable is plugged in. */
int net_up(void);
int net_link(void);                 /* 1 if the Ethernet cable has link */
const char *net_ip(void);           /* "192.168.1.20", or "" */

typedef void (*net_progress_fn)(int received, int total, void *ctx);   /* total -1 if unknown */

struct http_resp {
    int status;         /* HTTP status code, or negative on failure (see net_error) */
    char *body;         /* points inside the response buffer, NUL-terminated */
    int body_len;
    char *raw_;         /* owned buffer, release with http_free */
};

/* host is "a.b.c.d:port". body may be NULL. max_bytes caps the response size. */
int http_request(const char *host, const char *method, const char *path,
                 const char *body, int body_len, int max_bytes,
                 net_progress_fn progress, void *ctx, struct http_resp *out);
void http_free(struct http_resp *r);
const char *net_error(int status);  /* readable text for a negative status */

/* Minimal URL-style escaping for query strings and form bodies. */
void net_escape(const char *in, char *out, int out_size);

#endif
