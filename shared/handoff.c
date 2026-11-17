/* Word passed between XenithOS programs. See handoff.h. */
#include <stdio.h>
#include <string.h>

#include "handoff.h"

#define PAIR 80

static char got[HANDOFF_MAX][PAIR], give[HANDOFF_MAX][PAIR], self[PAIR];
static int ngot, ngive, received;

void handoff_args(int argc, char **argv)
{
    int i, on = 0;
    /* Copied straight away: on the Xbox the strings sit in memory the loader set aside, and
     * nothing keeps it from being reused later. */
    if (argv && argc > 0 && argv[0]) snprintf(self, sizeof self, "%s", argv[0]);
    for (i = 1; argv && i < argc && i < 32; i++) {
        const char *a = argv[i];
        if (!a) continue;
        if (!strcmp(a, HANDOFF_MARK)) {
            on = received = 1;
            continue;
        }
        if (on && strchr(a, '=') && strlen(a) < PAIR && ngot < HANDOFF_MAX)
            snprintf(got[ngot++], PAIR, "%s", a);
    }
}

int handoff_received(void)
{
    return received;
}

const char *handoff_self(void)
{
    return self;
}

static const char *find(char list[][PAIR], int n, const char *key)
{
    size_t k = strlen(key);
    int i;
    for (i = 0; i < n; i++)
        if (!strncmp(list[i], key, k) && list[i][k] == '=') return list[i] + k + 1;
    return NULL;
}

const char *handoff_get(const char *key)
{
    return find(got, ngot, key);
}

void handoff_set(const char *key, const char *value)
{
    size_t k = strlen(key);
    int i;
    for (i = 0; i < ngive; i++)
        if (!strncmp(give[i], key, k) && give[i][k] == '=') break;
    if (!value) {                                   /* remove it */
        if (i < ngive) {
            memmove(give[i], give[i + 1], (size_t)(ngive - i - 1) * PAIR);
            ngive--;
        }
        return;
    }
    if (i == ngive) {
        if (ngive == HANDOFF_MAX) return;
        ngive++;
    }
    snprintf(give[i], PAIR, "%s=%s", key, value);
}

int handoff_argv(const char *path, char **argv, int max)
{
    int n = 0, i;
    if (max < 2) return 0;
    argv[n++] = (char *)path;
    argv[n++] = (char *)HANDOFF_MARK;
    for (i = 0; i < ngive && n < max; i++) argv[n++] = give[i];
    return n;
}
