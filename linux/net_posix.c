/* net.h on Linux: the network is whatever the machine already has. Requests go through
 * http_posix.c, which unlike the console also takes names ("example.com:8795") and
 * "https://name" addresses. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "http_posix.h"
#include "net.h"

static char ip_text[64];

/* The first IPv4 address on an interface that's up and isn't loopback. */
static int find_address(void)
{
    struct ifaddrs *list, *a;
    ip_text[0] = 0;
    if (getifaddrs(&list) != 0) return 0;
    for (a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
        inet_ntop(AF_INET, &((struct sockaddr_in *)a->ifa_addr)->sin_addr, ip_text, sizeof ip_text);
        break;
    }
    freeifaddrs(list);
    return ip_text[0] != 0;
}

int net_up(void) { return find_address(); }
int net_is_up(void) { return find_address(); }
const char *net_ip(void) { find_address(); return ip_text; }

int net_http_once(const char *host, const char *method, const char *path,
                  const char *body, int body_len, int max_bytes,
                  net_progress_fn progress, void *ctx, struct http_resp *out, int connect_ms)
{
    return posix_http_once(host, method, path, body, body_len, max_bytes, progress, ctx, out, connect_ms);
}
