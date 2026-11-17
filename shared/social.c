/* Being online with other people. See social.h. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "icons.h"
#include "net.h"
#include "platform.h"
#include "social.h"
#include "theme.h"
#include "ui.h"

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

/* ---------------------------------------------------------------- achievements */

/* Their names, for the notification. XenithOS Cloud has the same list with how each is earned
 * (ACHIEVEMENTS in server/xenith_cloud.py, whose self-test checks the names agree). */
static const struct { const char *id, *name; } achievements[] = {
    {"volt-100", "Charged up"},
    {"volt-500", "High voltage"},
    {"volt-1000", "Overload"},
    {"volt-board", "On the board"},
    {"doom-exit", "Way out"},
    {"doom-secret", "Off the map"},
    {"doom-sweep", "Clean sweep"},
    {"doom-episode", "The end"},
    {"tube-first", "Now playing"},
    {"tube-end", "Every second"},
    {"tube-ten", "Channel surfer"},
    {"hostmc-start", "Server's up"},
    {"hostmc-online", "Open doors"},
    {"hostmc-join", "Company"},
    {"hostmc-backup", "Safe keeping"},
};

const char *social_achievement_name(const char *id)
{
    unsigned i;
    for (i = 0; id && i < sizeof achievements / sizeof achievements[0]; i++)
        if (!strcmp(achievements[i].id, id)) return achievements[i].name;
    return NULL;
}

int social_achievement_total(void)
{
    return (int)(sizeof achievements / sizeof achievements[0]);
}

/* achievements.txt: "profile|id|1 once XenithOS Cloud has it" and "profile|#counter|value",
 * a line each, for whoever has been signed in on this machine. */
#define EARNED_PATH "uda:/xenith/achievements.txt"
#define EARNED_MAX 256

struct earned { char who[20], what[24]; int value; };

static int earned_load(struct earned *e)
{
    char line[96], *a, *b;
    int n = 0;
    FILE *f = fopen(EARNED_PATH, "r");
    if (!f) return 0;
    while (n < EARNED_MAX && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || !(a = strchr(line, '|')) || !(b = strchr(a + 1, '|'))) continue;
        *a = *b = 0;
        snprintf(e[n].who, sizeof e[n].who, "%s", line);
        snprintf(e[n].what, sizeof e[n].what, "%s", a + 1);
        e[n].value = atoi(b + 1);
        n++;
    }
    fclose(f);
    return n;
}

static void earned_store(const struct earned *e, int n)
{
    int i;
    FILE *f;
    mkdir(XENITH_DIR, 0777);
    f = fopen(EARNED_PATH, "w");
    if (!f) return;
    fprintf(f, "# Written by XenithOS: achievements earned here (1 once XenithOS Cloud has them), and counts\n");
    for (i = 0; i < n; i++) fprintf(f, "%s|%s|%d\n", e[i].who, e[i].what, e[i].value);
    fclose(f);
}

static void who(char *out, int n)
{
    int i;
    for (i = 0; prof.name[i] && i < n - 1; i++) out[i] = (char)tolower((unsigned char)prof.name[i]);
    out[i] = 0;
}

static struct earned *find_earned(struct earned *e, int n, const char *w, const char *what)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(e[i].who, w) && !strcmp(e[i].what, what)) return &e[i];
    return NULL;
}

int social_earn(const char *id)
{
    static struct earned e[EARNED_MAX];
    const char *name = social_achievement_name(id);
    char w[20];
    int n;
    if (!prof.signed_in || !name) return 0;
    who(w, sizeof w);
    n = earned_load(e);
    if (find_earned(e, n, w, id) || n == EARNED_MAX) return 0;
    snprintf(e[n].who, sizeof e[n].who, "%s", w);
    snprintf(e[n].what, sizeof e[n].what, "%s", id);
    e[n].value = 0;
    earned_store(e, n + 1);
    ui_toast_icon(ICON_TROPHY);
    ui_toast("Achievement unlocked", name);
    return 1;
}

void social_flush(void)
{
    static struct earned e[EARNED_MAX];
    char w[20], ids[400] = "", form[520];
    struct http_resp r;
    int n, i, len = 0;
    if (!can_send()) return;
    who(w, sizeof w);
    n = earned_load(e);
    for (i = 0; i < n && len < (int)sizeof ids - 32; i++)
        if (!strcmp(e[i].who, w) && e[i].what[0] != '#' && !e[i].value)
            len += snprintf(ids + len, sizeof ids - len, "%s%s", len ? "," : "", e[i].what);
    if (!len) return;
    snprintf(form, sizeof form, "token=%s&ids=%s", prof.token, ids);
    if (http_request(cfg.server, "POST", "/v1/achievements/earn", form, (int)strlen(form), 4096, NULL, NULL, &r) == 200) {
        for (i = 0; i < n; i++)
            if (!strcmp(e[i].who, w) && e[i].what[0] != '#') e[i].value = 1;
        earned_store(e, n);
    }
    http_free(&r);
}

int social_count(const char *counter, int add)
{
    static struct earned e[EARNED_MAX];
    char w[20], what[24];
    struct earned *c;
    int n;
    if (!prof.signed_in) return 0;
    who(w, sizeof w);
    snprintf(what, sizeof what, "#%s", counter);
    n = earned_load(e);
    c = find_earned(e, n, w, what);
    if (!c) {
        if (n == EARNED_MAX) return 0;
        c = &e[n++];
        snprintf(c->who, sizeof c->who, "%s", w);
        snprintf(c->what, sizeof c->what, "%s", what);
        c->value = 0;
    }
    c->value += add;
    earned_store(e, n);
    return c->value;
}
