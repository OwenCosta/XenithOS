#ifndef XENITH_STORE_SIG_H
#define XENITH_STORE_SIG_H

#include "net.h"

/* The Store's replies come signed by XenithOS Cloud (store_sig.c). */

int store_sig_required(void);    /* this build has a key (store_key.h), so replies must be signed */

/* 1 when the reply may be used: it carries a good signature for name ("store", "all pc",
 * "icon doom.ximg", as in xenith_cloud.py's store_sig_name) by a key this build trusts, or
 * signatures aren't required. Call it before anything changes the body. */
int store_sig_ok(const struct http_resp *r, const char *name);

#endif
