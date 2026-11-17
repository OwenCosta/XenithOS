/* Talking to XenithOS Cloud. Replies are plain text: key=value lines, or rows split by '|'. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "avatar.h"
#include "config.h"
#include "icons.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "social.h"
#include "ui.h"

void kv_get(const char *body, const char *key, char *out, int size)
{
    int klen = (int)strlen(key);
    const char *p = body;
    out[0] = 0;
    while (p && *p) {
        if (!strncmp(p, key, klen) && p[klen] == '=') {
            const char *v = p + klen + 1, *e = strchr(v, '\n');
            int n = e ? (int)(e - v) : (int)strlen(v);
            if (n >= size) n = size - 1;
            memcpy(out, v, n);
            out[n] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

int split_fields(char *line, char **f, int max)
{
    int n = 0;
    f[n++] = line;
    for (; *line && n < max; line++)
        if (*line == '|') { *line = 0; f[n++] = line + 1; }
    return n;
}

const char *cloud_error_text(struct http_resp *r, int status)
{
    static char msg[120];
    if (status < 0) return net_error(status);
    if (r && r->body) {
        kv_get(r->body, "error", msg, sizeof msg);
        if (msg[0]) return msg;
    }
    snprintf(msg, sizeof msg, "XenithOS Cloud answered with error %d.", status);
    return msg;
}

/* What XenithOS Cloud says about itself at /v1/lan: its id, and the other addresses it answers
 * at. 1 if it answered at addr. */
struct lan_info {
    char id[24], lan[32], https[80];
};

static int ask_lan(const char *addr, int connect_ms, struct lan_info *in)
{
    struct http_resp r;
    int ok = http_probe(addr, "/v1/lan", connect_ms, 1024, &r) == 200;
    memset(in, 0, sizeof *in);
    if (ok) {
        kv_get(r.body, "id", in->id, sizeof in->id);
        kv_get(r.body, "lan", in->lan, sizeof in->lan);
        kv_get(r.body, "https", in->https, sizeof in->https);
    }
    http_free(&r);
    return ok && in->id[0];
}

/* "name" or "name:port": letters, digits, dots and dashes. */
static int https_name_ok(const char *s)
{
    int n = (int)strspn(s, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-"), port;
    if (n == 0 || n > 64 || s[0] == '.' || s[0] == '-') return 0;
    if (!s[n]) return 1;
    port = atoi(s + n + 1);
    return s[n] == ':' && s[n + 1] && strspn(s + n + 1, "0123456789") == strlen(s + n + 1) && port > 0 && port < 65536;
}

/* PCs and Raspberry Pis talk to XenithOS Cloud over HTTPS when it has an HTTPS address (it says
 * which at /v1/lan), once it answers there as the same server with a certificate that checks
 * out. Checked at each start, so it's dropped again when the server stops offering it or it
 * doesn't answer: plain HTTP to the address in config.txt keeps working, as on the Xbox, which
 * has no TLS. */
static void https_check(const struct lan_info *from)
{
    struct lan_info got;
    char addr[NET_ADDR_LEN];
    if (plat_kind() != PLAT_PC) return;
    if (!https_name_ok(from->https)) { https_forget(); return; }
    snprintf(addr, sizeof addr, "https://%s", from->https);
    if (!strcmp(addr, cfg.server_main)) { https_forget(); return; }     /* config.txt says it already */
    if (ask_lan(addr, NET_CONNECT_MS, &got) && !strcmp(got.id, from->id)) https_save(addr);
    else https_forget();
}

/* Consoles on the network XenithOS Cloud runs on talk to it there: a few milliseconds a request
 * instead of a trip out to the relay and back (most of a second each). The server says what that
 * address is (/v1/lan), and it's only used once it answers there as the same server. Once a
 * start-up; apps take it from route.txt (config_load). The server is asked at the address in
 * config.txt, so a PC that can't get there keeps the HTTPS address it had. */
void cloud_route_check(void)
{
    static int done;
    struct lan_info here, back;
    unsigned char ip[4];
    int port, home;

    if (done || !cfg.server_main[0] || !net_is_up()) return;
    done = 1;
    /* route.txt: still on that network? */
    home = cfg.route_id[0] && ask_lan(cfg.server, NET_LAN_CONNECT_MS, &here) && !strcmp(here.id, cfg.route_id);
    if (!home) {
        if (cfg.route_id[0]) route_forget();
        if (!ask_lan(cfg.server_main, NET_CONNECT_MS, &here)) return;      /* not reachable, or an older server */
    }
    https_check(&here);
    if (home || !here.lan[0] || !strcmp(here.lan, cfg.server_main) || !net_parse_host(here.lan, ip, &port)) return;
    if (ask_lan(here.lan, NET_LAN_CONNECT_MS, &back) && !strcmp(back.id, here.id)) route_save(here.lan, here.id);
}

int cloud_ready(const char *title)
{
    (void)title;
    if (!cfg.server[0]) {
        sfx(SFX_ERROR);
        ui_message("XenithOS Cloud isn't set up", "This copy of XenithOS doesn't have the XenithOS Cloud address. Download XenithOS again from its website, which includes it.");
        return 0;
    }
    if (net_is_up()) { cloud_route_check(); return 1; }
    ui_loading_frame("Connecting", "Getting an address from your router");
    audio_prepare_stall();
    if (!net_up()) {
        sfx(SFX_ERROR);
        if (plat_kind() == PLAT_XBOX)
            ui_message("No network", "Plug an Ethernet cable into the back of the Xbox, then try again. XenithOS can't use Wi-Fi on the Xbox.");
        else
            ui_message("No network", "This machine isn't connected to the internet. Connect it, then try again.");
        return 0;
    }
    cloud_route_check();
    return 1;
}

int cloud_post(const char *path, const char *form, struct http_resp *r)
{
    return http_request(cfg.server, "POST", path, form, (int)strlen(form), 64 * 1024, NULL, NULL, r);
}

int cloud_get(const char *path, int max, struct http_resp *r)
{
    return http_request(cfg.server, "GET", path, NULL, 0, max, NULL, NULL, r);
}

/* "5 minutes ago" style, from a number of seconds. */
void cloud_ago(long d, char *out, int n)
{
    if (d < 0) out[0] = 0;
    else if (d < 120) snprintf(out, n, "Just now");
    else if (d < 3600) snprintf(out, n, "%ld minutes ago", d / 60);
    else if (d < 7200) snprintf(out, n, "An hour ago");
    else if (d < 86400) snprintf(out, n, "%ld hours ago", d / 3600);
    else if (d < 172800) snprintf(out, n, "Yesterday");
    else if (d < 60L * 86400) snprintf(out, n, "%ld days ago", d / 86400);
    else snprintf(out, n, "A long time ago");
}

/* Toasts from the regular check wait their turn, so news and friends don't hide each other. */
#define TOASTS 4
static struct { int icon; char title[64], body[160]; } toasts[TOASTS];
static int ntoasts;
static uint64_t toast_shown;

static void toast_later(int icon, const char *title, const char *body)
{
    if (ntoasts == TOASTS) return;
    toasts[ntoasts].icon = icon;
    snprintf(toasts[ntoasts].title, sizeof toasts[0].title, "%s", title);
    snprintf(toasts[ntoasts].body, sizeof toasts[0].body, "%s", body);
    ntoasts++;
}

static void toast_next(void)
{
    uint64_t now = ui_ms();
    if (!ntoasts || (toast_shown && now - toast_shown < 3700)) return;
    toast_shown = now;
    ui_toast_icon(toasts[0].icon);
    ui_toast(toasts[0].title, toasts[0].body);
    sfx(SFX_NOTIFY);
    ntoasts--;
    memmove(&toasts[0], &toasts[1], ntoasts * sizeof toasts[0]);
}

static void news_seen(int id, const char *text)
{
    if (id <= st.last_inbox) return;
    st.last_inbox = id;
    settings_save();
    toast_later(ICON_MAIL, "XenithOS news", text);
}

/* Friends, from the last hello: how many are on, which ones, and the newest request, for the
 * profile named in friends_of. */
static int friends_on = -1;
static char on_names[400], ask_seen[24], friends_of[20];

int cloud_friends_online(void)
{
    return prof.signed_in ? friends_on : -1;
}

/* Is name in a comma-separated list? */
static int listed(const char *list, const char *name)
{
    int n = (int)strlen(name);
    const char *p = list;
    while (*p) {
        const char *e = strchr(p, ',');
        int len = e ? (int)(e - p) : (int)strlen(p);
        if (len == n && !strncmp(p, name, n)) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

static void read_friends(const char *body, int first)
{
    char on[400], ask[24], v[16], title[96];
    char *name, *save = NULL;
    int asks, came = 0;
    if (strcmp(friends_of, prof.name)) {                 /* someone else signed in since */
        snprintf(friends_of, sizeof friends_of, "%s", prof.name);
        on_names[0] = ask_seen[0] = 0;
        first = 1;
    }
    kv_get(body, "on", on, sizeof on);
    kv_get(body, "ask", ask, sizeof ask);
    kv_get(body, "asks", v, sizeof v);
    asks = atoi(v);
    kv_get(body, "friends_on", v, sizeof v);
    if (asks > 0 && strcmp(ask, ask_seen)) {             /* someone new wants to be friends */
        snprintf(title, sizeof title, "%s wants to be friends", ask);
        toast_later(ICON_PERSON_ADD, title, asks > 1 ? "You have friend requests waiting in Friends." : "Say yes in Friends.");
    }
    snprintf(ask_seen, sizeof ask_seen, "%s", asks > 0 ? ask : "");
    if (!first) {                                        /* friends who've just come on */
        char list[400], who[24] = "";
        snprintf(list, sizeof list, "%s", on);
        for (name = strtok_r(list, ",", &save); name; name = strtok_r(NULL, ",", &save))
            if (!listed(on_names, name)) {
                if (!came++) snprintf(who, sizeof who, "%s", name);
            }
        if (came == 1) snprintf(title, sizeof title, "%s is online", who);
        else if (came > 1) snprintf(title, sizeof title, "%s and %d more are online", who, came - 1);
        if (came) toast_later(ICON_PROFILE, title, "See what they're up to in Friends.");
    }
    snprintf(on_names, sizeof on_names, "%s", on);
    friends_on = atoi(v);
}

static void signed_out(void)
{
    profile_clear();
    friends_on = -1;
    ui_toast_icon(ICON_SIGN_OUT);
    ui_toast("Signed out", "Your sign-in expired. Sign in again from Profile.");
}

/* The regular check: at start-up, then about once a minute on the home screen. With a profile
 * it's a hello, which keeps you online for your friends and brings the news and theirs; without
 * one, or with an older XenithOS Cloud, it's the news alone. */
static int no_hello;

static void check_in(int first)
{
    struct http_resp r;
    char *line, *f[3], v[160];
    int code;
    if (prof.signed_in && !no_hello) {
        code = social_hello("", 0, NULL, &r);
        if (code == 200) {
            kv_get(r.body, "news_text", v, sizeof v);
            {
                char id[16];
                kv_get(r.body, "news_id", id, sizeof id);
                news_seen(atoi(id), v);
            }
            kv_get(r.body, "sharing", v, sizeof v);
            if (v[0] && (atoi(v) != 0) != prof.sharing) {
                prof.sharing = atoi(v) != 0;
                profile_save();
            }
            read_friends(r.body, first);
            http_free(&r);
            return;
        }
        http_free(&r);
        if (code == 401) { signed_out(); return; }
        if (code == 404) no_hello = 1;
        else return;
    }
    if (cloud_get("/v1/news", 16 * 1024, &r) == 200 && r.body_len) {
        line = r.body;
        if (strchr(line, '\n')) *strchr(line, '\n') = 0;
        if (split_fields(line, f, 3) == 3) news_seen(atoi(f[0]), f[2]);
    }
    http_free(&r);
}

void cloud_sync(int quiet)
{
    struct http_resp r;
    char form[160], v[64];
    int code;
    (void)quiet;
    if (!cfg.server[0] || !net_is_up()) return;

    if (prof.signed_in) {
        snprintf(form, sizeof form, "token=%s", prof.token);
        code = cloud_post("/v1/accounts/me", form, &r);
        if (code == 200) {
            kv_get(r.body, "name", v, sizeof v);
            if (v[0]) snprintf(prof.name, sizeof prof.name, "%s", v);
            kv_get(r.body, "avatar", v, sizeof v);
            if (avatar_valid(v)) snprintf(prof.avatar, sizeof prof.avatar, "%s", v);
            kv_get(r.body, "joined", v, sizeof v);
            snprintf(prof.joined, sizeof prof.joined, "%s", v);
            kv_get(r.body, "sharing", v, sizeof v);
            if (v[0]) prof.sharing = atoi(v) != 0;
            profile_save();
        } else if (code == 401) {
            http_free(&r);
            signed_out();
            return;
        }
        http_free(&r);
    }
    check_in(1);
    cloud_saves_sync(NULL, 0);              /* saves and settings, both ways; clashes wait for the app */
    social_flush();                         /* achievements earned while the network was down */
    toast_next();
}

void cloud_poll_inbox(void)
{
    static uint64_t last;
    uint64_t now = ui_ms();
    toast_next();
    if (!net_is_up() || !cfg.server[0]) return;
    if (!last) { last = now; return; }        /* go_online() already checked at boot */
    if (now - last < 60000) return;
    last = now;
    check_in(0);
}

/* What friends see while an app or game runs: XenithOS says hello for it on the way in, since
 * on the Xbox the app starts without the network, and again on the way back. */
void cloud_playing(const char *app_id)
{
    struct http_resp r;
    if (!prof.signed_in) return;
    if (!app_id) social_flush();            /* what the app earned, if it couldn't send it itself */
    if (no_hello) return;
    if (social_hello(app_id ? app_id : "", app_id != NULL, NULL, &r) == 200 && !app_id) read_friends(r.body, 0);
    http_free(&r);
}
