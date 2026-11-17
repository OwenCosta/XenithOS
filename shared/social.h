#ifndef XENITH_SOCIAL_H
#define XENITH_SOCIAL_H

/* Being online with other people, for XenithOS and its apps: telling XenithOS Cloud that the
 * signed-in profile is here and what it's running, so friends see it (Friends, /v1/hello).
 * Nothing is sent without a profile, and nothing here brings the network up: on the Xbox an app
 * starts with it down, and XenithOS says hello for it before handing over (hold). */
#include "net.h"

/* POST /v1/hello. app: what's running (an app's id, "" for XenithOS itself). hold: XenithOS is
 * handing over to it and it can't say hello itself, so friends see it a while longer. mc: the
 * address of the profile's HostMC server ("" once it can't be reached, NULL: nothing to say).
 * out may be NULL; otherwise the caller frees it with http_free(). Returns the HTTP status, or
 * 0 when there's no profile, no XenithOS Cloud or no network. */
int  social_hello(const char *app, int hold, const char *mc, struct http_resp *out);
/* social_hello() now and then (every few minutes, or straight away when mc changes), for apps
 * that stay open a long time. */
void social_tick(const char *app, const char *mc);
/* Going offline (turning off, restarting): friends see it at once. */
void social_bye(void);

#endif
