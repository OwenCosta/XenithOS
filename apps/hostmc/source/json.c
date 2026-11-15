/* Just enough JSON for HostMC (hostmc.h): values are found where they sit in the text, with no
 * tree built. Malformed text gives NULL, never a crash; the text has to end in a NUL. */
#include <stdlib.h>
#include <string.h>

#include "hostmc.h"

static const char *ws(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/* Past a string; p is on its opening quote. */
static const char *skip_str(const char *p)
{
    if (!p || *p != '"') return NULL;
    for (p++; *p; p++) {
        if (*p == '\\') {
            if (!p[1]) return NULL;
            p++;
        } else if (*p == '"') {
            return p + 1;
        }
    }
    return NULL;
}

/* Past one value, however deeply nested. */
static const char *skip(const char *p)
{
    int depth = 0;
    p = ws(p);
    if (!p || !*p) return NULL;
    do {
        p = ws(p);
        if (!*p) return NULL;
        if (*p == '"') {
            if (!(p = skip_str(p))) return NULL;
        } else if (*p == '{' || *p == '[') {
            depth++;
            p++;
        } else if (*p == '}' || *p == ']') {
            if (--depth < 0) return NULL;
            p++;
        } else if (*p == ',' || *p == ':') {
            if (!depth) return NULL;
            p++;
        } else {                                  /* a number, true, false or null */
            const char *s = p;
            while (*p && !strchr(" \t\r\n,:[]{}\"", *p)) p++;
            if (p == s) return NULL;
        }
    } while (depth > 0);
    return p;
}

const char *js_member(const char *obj, int i, char *key, int key_len)
{
    const char *p = ws(obj);
    int at = 0;
    if (!p || *p != '{') return NULL;
    p = ws(p + 1);
    if (*p == '}') return NULL;
    for (;;) {
        const char *k = p, *e = skip_str(p);
        if (!e) return NULL;
        p = ws(e);
        if (*p != ':') return NULL;
        p = ws(p + 1);
        if (at++ == i) {
            if (key) js_str(k, key, key_len);
            return p;
        }
        if (!(p = skip(p))) return NULL;
        p = ws(p);
        if (*p != ',') return NULL;
        p = ws(p + 1);
    }
}

const char *js_key(const char *obj, const char *key)
{
    const char *p = ws(obj);
    size_t kl = strlen(key);
    if (!p || *p != '{') return NULL;
    p = ws(p + 1);
    if (*p == '}') return NULL;
    for (;;) {
        const char *k = p, *e = skip_str(p);
        if (!e) return NULL;
        p = ws(e);
        if (*p != ':') return NULL;
        p = ws(p + 1);
        /* The keys asked for never need unescaping, so the raw text is compared. */
        if ((size_t)(e - k - 2) == kl && !memcmp(k + 1, key, kl)) return p;
        if (!(p = skip(p))) return NULL;
        p = ws(p);
        if (*p != ',') return NULL;
        p = ws(p + 1);
    }
}

const char *js_first(const char *arr)
{
    const char *p = ws(arr);
    if (!p || *p != '[') return NULL;
    p = ws(p + 1);
    return *p && *p != ']' ? p : NULL;
}

const char *js_next(const char *val)
{
    const char *p = skip(val);
    if (!p) return NULL;
    p = ws(p);
    if (*p != ',') return NULL;
    p = ws(p + 1);
    return *p && *p != ']' ? p : NULL;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int js_str(const char *val, char *out, int n)
{
    const char *p = ws(val);
    int k = 0;
    if (n > 0) out[0] = 0;
    if (!p || *p != '"' || n <= 0) return 0;
    for (p++; *p && *p != '"'; p++) {
        char c = *p;
        if (c == '\\') {
            p++;
            switch (*p) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                int v = 0, i, h;
                for (i = 1; i <= 4; i++) {
                    if ((h = hexval(p[i])) < 0) return 0;
                    v = v * 16 + h;
                }
                p += 4;
                c = v < 128 ? (char)v : '?';     /* HostMC's text is plain ASCII */
                break;
            }
            case 0: return 0;
            default: c = *p;                    /* \" \\ \/ */
            }
        }
        if (k < n - 1) out[k++] = c;
    }
    out[k] = 0;
    return *p == '"';
}

long long js_num(const char *val)
{
    const char *p = ws(val);
    if (!p || !(*p == '-' || (*p >= '0' && *p <= '9'))) return 0;
    return strtoll(p, NULL, 10);
}

int js_bool(const char *val)
{
    const char *p = ws(val);
    return p && !strncmp(p, "true", 4);
}
