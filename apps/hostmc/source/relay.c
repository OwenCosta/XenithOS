/* The supporter relay as the way in for friends elsewhere: for supporters on Patreon, a name of
 * their own, <name>.<domain>, for friends to type in, with nothing to set up on a router or a
 * server.
 *
 * XenithOS Cloud (/v1/relay/status, claim and release, with the signed-in XenithOS profile's
 * token) says whether the profile is a supporter's (OCAccounts knows, once Patreon and this
 * profile are linked to an OCAccount there), gives out the name, and passes HostMC's key on to the
 * relay. While the server runs, HostMC keeps the same SSH tunnel to the relay as to a VPS
 * (way.c), allowed to open one port there, and the relay passes everyone who types the name down
 * it. XenithOS Cloud's last answer (the relay's address, that port, its host key) is kept in
 * hostmc/relay/relay.txt, so the tunnel starts without asking again; the host key is the only one
 * trusted for the relay (tunnel_pin).
 *
 * Its limits are the VPS route's: Java Edition only, and every player arrives as 127.0.0.1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "hostmc.h"
#include "net.h"
#include "ui.h"

#define RELAY_DIR  HM_DIR "/relay"
#define RELAY_FILE RELAY_DIR "/relay.txt"

struct hm_relay relay;

static void field(const char *text, const char *key, char *out, int n)
{
    size_t kl = strlen(key);
    const char *p = text;
    out[0] = 0;
    while (p && *p) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            int i = 0;
            for (p += kl + 1; *p && *p != '\n' && *p != '\r' && i < n - 1; p++)
                if (*p >= ' ' && *p <= '~') out[i++] = *p;
            out[i] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

/* XenithOS Cloud's answer (key=value lines), into relay. */
static void parse(const char *text)
{
    char v[200];
    field(text, "relay", v, sizeof v);
    relay.open = !strcmp(v, "on");
    field(text, "step", relay.step, sizeof relay.step);
    field(text, "tier", relay.tier, sizeof relay.tier);
    field(text, "name", relay.name, sizeof relay.name);
    field(text, "state", relay.state, sizeof relay.state);
    field(text, "address", relay.address, sizeof relay.address);
    field(text, "domain", relay.domain, sizeof relay.domain);
    field(text, "held", relay.held, sizeof relay.held);
    field(text, "suggest", relay.suggest, sizeof relay.suggest);
    field(text, "hostkey", relay.hostkey, sizeof relay.hostkey);
    field(text, "patreon", relay.patreon, sizeof relay.patreon);
    field(text, "account", relay.account, sizeof relay.account);
    field(text, "message", relay.message, sizeof relay.message);
    field(text, "until", v, sizeof v);
    relay.until = atoll(v);
    memset(&relay.w.cfg, 0, sizeof relay.w.cfg);
    field(text, "ssh_host", relay.w.cfg.host, sizeof relay.w.cfg.host);
    field(text, "ssh_user", relay.w.cfg.user, sizeof relay.w.cfg.user);
    field(text, "ssh_port", v, sizeof v);
    relay.w.cfg.ssh_port = atoi(v);
    field(text, "port", v, sizeof v);
    relay.w.cfg.public_port = atoi(v);
    snprintf(relay.w.cfg.bind, sizeof relay.w.cfg.bind, "127.0.0.1");   /* the relay's loopback: its router takes it from there */
    relay.w.cfg.local_port = server_port();
    relay.w.cfg.relay = 1;
    relay.known = 1;
}

/* The tunnel can go: a name that's on, and the relay's address, port and identity. */
static int ready(void)
{
    char why[200];
    return relay.name[0] && relay.hostkey[0] && relay.w.cfg.host[0] && tunnel_cfg_ok(&relay.w.cfg, why, sizeof why);
}

void relay_address(char *out, int n)
{
    snprintf(out, n, "%s", relay.address[0] ? relay.address : "No name yet");
}

static void target(char *host, int n, int *port)
{
    snprintf(host, n, "%s", relay.address);
    *port = DEFAULT_PORT;
}

/* What the check from outside found, through the relay: "" when friends get in. The relay
 * answers for a server it can't reach itself, with these words where the version goes. */
static void finding(const struct mcping *p, char *out, int n)
{
    out[0] = 0;
    switch (p->result) {
    case PING_OK:
        if (!strcmp(p->version, "Offline"))
            snprintf(out, n, "The relay doesn't see the tunnel from this PC yet. Try the check again in a moment.");
        else if (!strcmp(p->version, "No server"))
            snprintf(out, n, "The relay doesn't know %s yet. It takes new names every 30 seconds: try the check again in a minute.", relay.address);
        else if (!strcmp(p->version, "Unavailable"))
            snprintf(out, n, "The relay isn't passing players to %s. Choose Check my membership to see why.", relay.name);
        else if (!strcmp(p->version, "Busy"))
            snprintf(out, n, "The relay says your server has as many connections as it takes right now.");
        break;
    case PING_NO_HOST:
        snprintf(out, n, "%s doesn't lead anywhere yet. Try the check again in a few minutes.", relay.address);
        break;
    case PING_REFUSED:
    case PING_TIMEOUT:
        snprintf(out, n, "The relay didn't answer the check. Try it again in a moment.");
        break;
    case PING_NOT_MC:
        snprintf(out, n, "Something answered at %s, but not a Minecraft server.", relay.address);
        break;
    case PING_SILENT:
        snprintf(out, n, "The relay took the connection, but no answer came back from here. Try the check again in a moment.");
        break;
    default:
        snprintf(out, n, "This PC couldn't reach the relay just now.");
    }
}

void relay_init(void)
{
    char abs[740], *text;
    snprintf(abs, sizeof abs, "%s/relay", hm_root());
    memset(&relay, 0, sizeof relay);
    relay.w.name = "the relay";
    relay.w.Name = "The relay";
    relay.w.on = &hs.relay;
    relay.w.check_file = RELAY_DIR "/check.txt";
    relay.w.address = relay_address;
    relay.w.target = target;
    relay.w.finding = finding;
    way_reset(&relay.w);
    tunnel_init(&relay.w.t, RELAY_DIR, abs);
    if ((text = file_read(RELAY_FILE, NULL))) {
        parse(text);
        free(text);
        if (relay.hostkey[0]) tunnel_pin(&relay.w.t, &relay.w.cfg, relay.hostkey);
    }
    if (!hs.relay && tunnel_alive(&relay.w.t)) tunnel_stop(&relay.w.t);
}

int relay_ask(const char *what, const char *name, char *why, int n)
{
    struct http_resp r;
    struct tunnel_cfg before = relay.w.cfg;
    char form[700], tok[160], pub[240], epub[360], ename[80], path[40], key_before[100], name_before[24];
    int code;
    why[0] = 0;
    if (!prof.signed_in || !prof.token[0]) {
        snprintf(why, n, "Sign in to your XenithOS profile first, in XenithOS's Profile. The relay goes by it.");
        return 0;
    }
    if (!cfg.server[0]) {
        snprintf(why, n, "This PC doesn't have XenithOS Cloud's address. Run the XenithOS install command once more.");
        return 0;
    }
    net_escape(prof.token, tok, sizeof tok);
    if (!strcmp(what, "claim")) {
        if (!tunnel_key(&relay.w.t, why, n)) return 0;
        if (!tunnel_pubkey(&relay.w.t, pub, sizeof pub)) {
            snprintf(why, n, "HostMC's key couldn't be read.");
            return 0;
        }
        net_escape(pub, epub, sizeof epub);
        net_escape(name, ename, sizeof ename);
        snprintf(form, sizeof form, "token=%s&name=%s&key=%s", tok, ename, epub);
    } else {
        snprintf(form, sizeof form, "token=%s", tok);
    }
    snprintf(path, sizeof path, "/v1/relay/%s", what);
    ui_busy_begin("Asking XenithOS Cloud", name);
    code = http_request(cfg.server, "POST", path, form, (int)strlen(form), 16 * 1024, NULL, NULL, &r);
    ui_busy_end();
    if (code != 200) {
        if (code < 0) snprintf(why, n, "XenithOS Cloud didn't answer: %s", net_error(code));
        else if (r.body) field(r.body, "error", why, n);
        if (!why[0]) snprintf(why, n, "XenithOS Cloud answered with error %d.", code);
        http_free(&r);
        return 0;
    }
    snprintf(key_before, sizeof key_before, "%s", relay.hostkey);
    snprintf(name_before, sizeof name_before, "%s", relay.name);
    parse(r.body);
    mkdirs(RELAY_DIR);
    chmod(RELAY_DIR, 0700);                          /* HostMC's key lives here too */
    file_write(RELAY_FILE, r.body, r.body_len, 0600);
    http_free(&r);
    relay.asked = ui_ms();
    if (strcmp(name_before, relay.name)) {
        /* A new name: what the check found was for the old one. The relay learns names within
         * half a minute (its sync), so the new one is checked once it has. */
        relay.w.check[0] = 0;
        relay.w.check_ok = 0;
        if (relay.name[0] && tunnel_alive(&relay.w.t)) relay.w.check_at = ui_ms() + 35000;
    }
    if (relay.hostkey[0]) tunnel_pin(&relay.w.t, &relay.w.cfg, relay.hostkey);
    /* The relay moved, or its identity changed: the tunnel starts again with the new ones. */
    if (tunnel_alive(&relay.w.t) && (strcmp(before.host, relay.w.cfg.host) || before.ssh_port != relay.w.cfg.ssh_port ||
                                     before.public_port != relay.w.cfg.public_port || strcmp(before.user, relay.w.cfg.user) ||
                                     strcmp(key_before, relay.hostkey)))
        tunnel_stop(&relay.w.t);
    return 1;
}

int relay_turn_on(char *why, int n)
{
    if (!relay.name[0] || strcmp(relay.state, "on")) {
        snprintf(why, n, "%s", relay.message[0] ? relay.message : "Pick a name for your server first.");
        return 0;
    }
    if (!ready()) {
        snprintf(why, n, "XenithOS Cloud's answer about the relay wasn't complete. Try again in a minute.");
        return 0;
    }
    if (!tunnel_key(&relay.w.t, why, n)) return 0;
    if (!tunnel_pin(&relay.w.t, &relay.w.cfg, relay.hostkey)) {
        snprintf(why, n, "The relay's identity from XenithOS Cloud couldn't be kept.");
        return 0;
    }
    if (hs.internet) pi_turn_off();                  /* one way in at a time */
    if (hs.vps) vps_turn_off();
    hs.relay = 1;
    hs_save();
    way_reset(&relay.w);
    if (sv.state == SV_RUNNING) way_started(&relay.w);
    return 1;
}

void relay_turn_off(void)
{
    way_off(&relay.w);
    hs.relay = 0;
    hs_save();
}

void relay_server_started(void)
{
    if (ready()) way_started(&relay.w);
}

void relay_server_stopped(void) { way_stopped(&relay.w); }
void relay_poll(void) { if (!hs.relay || ready()) way_poll(&relay.w); }
void relay_check(void) { way_check(&relay.w); }
int relay_up(void) { return way_up(&relay.w); }
const char *relay_status(uint32_t *color) { return way_status(&relay.w, color); }
