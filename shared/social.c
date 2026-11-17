/* Being online with other people. See social.h. */
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "net.h"
#include "platform.h"
#include "social.h"
#include "theme.h"

#define TICK_US (4ull * 60 * 1000000)     /* social_tick(): a hello every four minutes */

static int can_send(void)
{
    return prof.signed_in && prof.token[0] && cfg.server[0] && net_is_up();
}

int social_hello(const char *app, int hold, const char *mc, struct http_resp *out)
{
    char form[400], en_app[80], en_mc[240] = "";
    struct http_resp r;
    int code;
    if (out) memset(out, 0, sizeof *out);
    if (!can_send()) return 0;
    if (!prof.sharing) {                  /* friends don't see what's playing: it isn't even sent */
        app = "";
        if (mc) mc = "";
    }
    net_escape(app ? app : "", en_app, sizeof en_app);
    if (mc) net_escape(mc, en_mc, sizeof en_mc);
    snprintf(form, sizeof form, "token=%s&ver=%s&app=%s%s%s%s", prof.token, XENITH_VERSION, en_app,
             hold ? "&hold=1" : "", mc ? "&mc=" : "", en_mc);
    code = http_request(cfg.server, "POST", "/v1/hello", form, (int)strlen(form), 16 * 1024, NULL, NULL,
                        out ? out : &r);
    if (!out) http_free(&r);
    return code;
}

void social_tick(const char *app, const char *mc)
{
    static uint64_t last;
    static char said_mc[160] = "\1";      /* nothing said yet */
    uint64_t now = plat_time_us();
    int mc_changed = mc && strcmp(mc, said_mc);
    if (!can_send()) return;
    if (last && now - last < (mc_changed ? 30000000ull : TICK_US)) return;
    last = now;
    if (social_hello(app, 0, mc, NULL) == 200 && mc) snprintf(said_mc, sizeof said_mc, "%s", mc);
}

void social_bye(void)
{
    char form[120];
    struct http_resp r;
    if (!can_send()) return;
    snprintf(form, sizeof form, "token=%s&bye=1", prof.token);
    http_request(cfg.server, "POST", "/v1/hello", form, (int)strlen(form), 4096, NULL, NULL, &r);
    http_free(&r);
}
