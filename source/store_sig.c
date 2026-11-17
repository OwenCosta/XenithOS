/* The Store's replies come signed by XenithOS Cloud, in a header older versions never read:
 *
 *     X-Xenith-Sig: 1 <key id> <signature>
 *
 * 1 is the format, the key id the first 16 hex digits of the public key that signed it, and the
 * signature (ed25519, 128 hex digits) covers this text:
 *
 *     XenithOS Store 1\n<name>\n<SHA-256 of the body, in hex>\n
 *
 * ("Store signatures" in server/xenith_cloud.py, which signs them). The lists hold the SHA-256 that
 * each download is checked against, so without this someone between here and XenithOS Cloud could
 * change a list and the files together. With a key in store_key.h, a reply without a good
 * signature by it isn't used. The ed25519 check is Monocypher's (shared/monocypher*.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "monocypher-ed25519.h"
#include "sha256.h"
#include "store_key.h"
#include "store_sig.h"

#define SIG_HEADER     "X-Xenith-Sig:"
#define SIG_HEADER_LEN 13

/* The keys this build trusts: store_key.h's. In the simulator, XSIM_STORE_KEY (one key, in hex)
 * stands in for them, so a test can run its own XenithOS Cloud with a key made for the test, and
 * XSIM_STORE_KEY=none acts as a build without one. */
static const char *key_hex(int i)
{
#ifdef XENITH_SIM
    const char *k = getenv("XSIM_STORE_KEY");
    if (k && !strcmp(k, "none")) return NULL;
    if (k && *k) return i == 0 ? k : NULL;
#endif
    return store_keys[i];
}

int store_sig_required(void)
{
    return key_hex(0) != NULL;
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;                                 /* lower case only, as XenithOS Cloud writes them */
}

/* n bytes from 2n hex digits; 0 if there aren't that many. */
static int unhex(const char *s, unsigned char *out, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        int hi = hex_digit((unsigned char)s[2 * i]);
        int lo = hi < 0 ? -1 : hex_digit((unsigned char)s[2 * i + 1]);
        if (lo < 0) return 0;
        out[i] = (unsigned char)(hi << 4 | lo);
    }
    return 1;
}

/* One header's value, "1 <key id> <signature>": 1 when a key this build trusts made it, of text. */
static int check_one(const char *value, int len, const char *text)
{
    char line[200], *tok[4], *save = NULL, *t;
    unsigned char sig[64], pub[32];
    int n = 0, i;

    if (len <= 0 || len >= (int)sizeof line) return 0;
    memcpy(line, value, len);
    line[len] = 0;
    for (t = strtok_r(line, " \t\r", &save); t && n < 4; t = strtok_r(NULL, " \t\r", &save)) tok[n++] = t;
    if (n != 3 || strcmp(tok[0], "1") || strlen(tok[1]) != 16 || strlen(tok[2]) != 128 || !unhex(tok[2], sig, 64))
        return 0;
    for (i = 0; key_hex(i); i++) {
        const char *k = key_hex(i);
        if (strlen(k) != 64 || strncmp(k, tok[1], 16) || !unhex(k, pub, 32)) continue;
        if (crypto_ed25519_check(sig, pub, (const uint8_t *)text, strlen(text)) == 0) return 1;
    }
    return 0;
}

int store_sig_ok(const struct http_resp *r, const char *name)
{
    char digest[65], text[160];
    const char *p;
    int good = 0, seen = 0;

    if (!store_sig_required()) return 1;
    if (r->raw_ && r->body && r->body >= r->raw_) {
        sha256_hex(r->body, (size_t)r->body_len, digest);
        snprintf(text, sizeof text, "XenithOS Store 1\n%s\n%s\n", name, digest);
        /* The headers: from the status line up to the body. */
        for (p = r->raw_; p < r->body && !good; ) {
            const char *eol = memchr(p, '\n', (size_t)(r->body - p));
            if (!eol) break;
            if (eol - p > SIG_HEADER_LEN && !strncasecmp(p, SIG_HEADER, SIG_HEADER_LEN)) {
                seen = 1;
                good = check_one(p + SIG_HEADER_LEN, (int)(eol - p - SIG_HEADER_LEN), text);
            }
            p = eol + 1;
        }
    }
#ifdef XENITH_SIM
    fprintf(stderr, "store: %s: %s\n", name, good ? "signed" : seen ? "signature doesn't match, refused" : "not signed, refused");
#else
    (void)seen;
#endif
    return good;
}
