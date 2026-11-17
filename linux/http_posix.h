#ifndef XENITH_HTTP_POSIX_H
#define XENITH_HTTP_POSIX_H

#include "net.h"

/* net_http_once on POSIX sockets, HTTPS included (http_posix.c): what net_posix.c (PCs and
 * Raspberry Pis) and the simulator's net_sim.c both use. */
int posix_http_once(const char *host, const char *method, const char *path,
                    const char *body, int body_len, int max_bytes,
                    net_progress_fn progress, void *ctx, struct http_resp *out, int connect_ms);

#endif
