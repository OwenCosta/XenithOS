/* net.h in the simulator: the PC's client (linux/http_posix.c), held to what the console's
 * lwIP client can do while the simulator plays an Xbox: "a.b.c.d:port" only, so no names and
 * no HTTPS. As a PC (XSIM_KIND=pc) it takes everything a PC does. */
#include <stdlib.h>
#include <string.h>

#include "http_posix.h"
#include "net.h"
#include "platform.h"

/* The network is down until something brings it up (net_up), as on the Xbox. XSIM_NET_UP=1 starts
 * with it up, as a PC's simply is: an app that never asks then shows Online too. */
static int up = -1;
static int is_up(void)
{
    if (up < 0) up = getenv("XSIM_NET_UP") && !strcmp(getenv("XSIM_NET_UP"), "1");
    return up;
}
int net_up(void) { up = 1; return 1; }
int net_is_up(void) { return is_up(); }
const char *net_ip(void) { return is_up() ? "127.0.0.1" : ""; }

int net_http_once(const char *host, const char *method, const char *path,
                  const char *body, int body_len, int max_bytes,
                  net_progress_fn progress, void *ctx, struct http_resp *out, int connect_ms)
{
    unsigned char ip[4];
    int port;
    if (plat_kind() == PLAT_XBOX && !net_parse_host(host, ip, &port)) {
        memset(out, 0, sizeof *out);
        return out->status = NE_BAD_HOST;
    }
    return posix_http_once(host, method, path, body, body_len, max_bytes, progress, ctx, out, connect_ms);
}
