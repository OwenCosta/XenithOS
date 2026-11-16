/* The player's own server on the internet (a VPS) as the way in for friends elsewhere, in place
 * of playit.gg. While the Minecraft server runs, HostMC keeps an SSH reverse tunnel (tunnel.c) to
 * the VPS: players connect to the VPS's public port and arrive here.
 *
 * The VPS needs, once: HostMC's key in authorized_keys (HostMC shows the line), forwards allowed
 * on its public address (GatewayPorts clientspecified in sshd_config, without which sshd quietly
 * listens on its own loopback only), and the port open in its firewall. Each time the tunnel comes
 * up HostMC asks the server for its status through the VPS's public address (ping.c), so whichever
 * of those is missing shows as a plain message.
 *
 * Its limits: TCP only, which Java Edition is (Bedrock isn't), and every player arrives from
 * 127.0.0.1, so bans by address don't work for them; bans by name do.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio.h"
#include "hostmc.h"
#include "icons.h"
#include "theme.h"
#include "ui.h"

#define VPS_DIR  HM_DIR "/vps"
#define VPS_CONF VPS_DIR "/vps.txt"

struct hm_vps vps;

static struct mcping ping;              /* the check, while it runs */
static int checking, state = TS_STOPPED;
static uint64_t check_at, retry_at;
static long long checked_try = -2, told_try = -2;   /* the tries in tunnel.log already checked, told */

static void conf_get(const char *text, const char *key, char *out, int n)
{
    size_t kl = strlen(key);
    const char *p = text;
    out[0] = 0;
    while (p && *p) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1;
            snprintf(out, n, "%.*s", (int)strcspn(p, "\r\n"), p);
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

void vps_load(void)
{
    char *t = file_read(VPS_CONF, NULL), v[32];
    memset(&vps.cfg, 0, sizeof vps.cfg);
    snprintf(vps.cfg.user, sizeof vps.cfg.user, "root");
    snprintf(vps.cfg.bind, sizeof vps.cfg.bind, "0.0.0.0");
    vps.cfg.ssh_port = 22;
    vps.cfg.public_port = DEFAULT_PORT;
    vps.cfg.local_port = server_port();
    if (!t) return;
    conf_get(t, "host", vps.cfg.host, sizeof vps.cfg.host);
    conf_get(t, "user", v, sizeof v);
    if (v[0]) snprintf(vps.cfg.user, sizeof vps.cfg.user, "%s", v);
    conf_get(t, "ssh_port", v, sizeof v);
    if (atoi(v) > 0) vps.cfg.ssh_port = atoi(v);
    conf_get(t, "public_port", v, sizeof v);
    if (atoi(v) > 0) vps.cfg.public_port = atoi(v);
    free(t);
}

void vps_save(void)
{
    char buf[400];
    int n = snprintf(buf, sizeof buf, "host=%s\nuser=%s\nssh_port=%d\npublic_port=%d\n", vps.cfg.host, vps.cfg.user,
                     vps.cfg.ssh_port, vps.cfg.public_port);
    mkdirs(VPS_DIR);
    file_write(VPS_CONF, buf, n, 0600);
}

void vps_init(void)
{
    char abs[740];
    snprintf(abs, sizeof abs, "%s/vps", hm_root());
    memset(&vps, 0, sizeof vps);
    tunnel_init(&vps.t, VPS_DIR, abs);
    vps_load();
    if (!hs.vps && tunnel_alive(&vps.t)) tunnel_stop(&vps.t);
}

/* What friends type in Minecraft: the VPS's address, with the port if it isn't the usual one. */
void vps_address(char *out, int n)
{
    if (vps.cfg.public_port == DEFAULT_PORT) snprintf(out, n, "%s", vps.cfg.host);
    else snprintf(out, n, "%s:%d", vps.cfg.host, vps.cfg.public_port);
}

int vps_turn_on(char *why, int n)
{
    if (!tunnel_cfg_ok(&vps.cfg, why, n)) return 0;
    if (!tunnel_key(&vps.t, why, n)) return 0;
    if (hs.internet) pi_turn_off();                  /* one way in at a time */
    hs.vps = 1;
    hs_save();
    vps.why[0] = vps.check[0] = 0;
    retry_at = 0;
    if (sv.state == SV_RUNNING) vps_server_started();
    return 1;
}

void vps_turn_off(void)
{
    mcping_cancel(&ping);
    checking = 0;
    check_at = 0;
    tunnel_stop(&vps.t);
    state = TS_STOPPED;
    hs.vps = 0;
    hs_save();
    vps.why[0] = vps.check[0] = 0;
}

static void start_tunnel(void)
{
    char why[220] = "";
    vps.cfg.local_port = server_port();
    checked_try = told_try = -2;                     /* a new tunnel.log */
    if (tunnel_start(&vps.t, &vps.cfg, why, sizeof why)) {
        sv_note("connecting to your VPS.");
    } else {
        snprintf(vps.why, sizeof vps.why, "%s", why);
        retry_at = ui_ms() + 30000;
    }
}

void vps_server_started(void)
{
    if (!hs.vps || !vps.cfg.host[0]) return;
    retry_at = 0;
    vps.why[0] = 0;
    if (!tunnel_alive(&vps.t)) start_tunnel();
}

void vps_server_stopped(void)
{
    if (tunnel_alive(&vps.t)) sv_note("the tunnel to your VPS closed with the server.");
    tunnel_stop(&vps.t);
    mcping_cancel(&ping);
    checking = 0;
    check_at = 0;
    state = TS_STOPPED;
}

void vps_check(void)
{
    mcping_cancel(&ping);
    vps.check[0] = 0;
    checking = 1;
    check_at = 0;
    mcping_start(&ping, vps.cfg.host, vps.cfg.public_port, 10000);
}

int vps_checking(void)
{
    return checking;
}

static void check_done(void)
{
    char addr[160];
    int port = vps.cfg.public_port;
    vps_address(addr, sizeof addr);
    checking = 0;
    vps.check_ok = ping.result == PING_OK;
    switch (ping.result) {
    case PING_OK:
        snprintf(vps.check, sizeof vps.check, "Checked from the internet just now: friends reach the server at %s (%d of %d on).",
                 addr, ping.online, ping.max);
        break;
    case PING_REFUSED:
        snprintf(vps.check, sizeof vps.check,
                 "The VPS turned players away on port %d although the tunnel is up, so its sshd keeps the port to itself. "
                 "Add GatewayPorts clientspecified to /etc/ssh/sshd_config, then sudo systemctl restart ssh.", port);
        break;
    case PING_TIMEOUT:
        snprintf(vps.check, sizeof vps.check, "Nothing answered on port %d of the VPS. Open it in the VPS's firewall (sudo ufw allow %d/tcp), "
                 "and in your provider's firewall if it has one.", port, port);
        break;
    case PING_NO_HOST:
        snprintf(vps.check, sizeof vps.check, "%s doesn't lead anywhere: check the address.", vps.cfg.host);
        break;
    case PING_NOT_MC:
        snprintf(vps.check, sizeof vps.check, "Something answered on port %d of the VPS, but not this Minecraft server. Is another program using that port there?", port);
        break;
    case PING_SILENT:
        snprintf(vps.check, sizeof vps.check, "The VPS took the connection on port %d, but no answer came back from here. Try the check again in a moment.", port);
        break;
    default:
        snprintf(vps.check, sizeof vps.check, "This PC couldn't reach the VPS on port %d just now.", port);
    }
    {
        /* What it found, kept for later (and for anyone helping): hostmc/vps/check.txt */
        char buf[400];
        int n = snprintf(buf, sizeof buf, "%s\n%s\n", vps.check_ok ? "ok" : "failed", vps.check);
        file_write(VPS_DIR "/check.txt", buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1, 0600);
    }
    if (vps.check_ok) {
        ui_toast_icon(ICON_NETWORK);
        ui_toast("Your VPS is passing players on", addr);
    } else {
        sfx(SFX_ERROR);
        ui_toast_icon(ICON_NETWORK);
        ui_toast("Friends can't get in through your VPS", "The Internet page says why");
        sv_note(vps.check);
    }
}

void vps_poll(void)
{
    static uint64_t next;
    uint64_t now = ui_ms();
    long long attempt;
    char why[220];
    if (checking && mcping_poll(&ping)) check_done();
    if (check_at && now >= check_at && state == TS_UP && sv.state == SV_RUNNING) vps_check();
    if (now < next) return;
    next = now + 1000;
    if (!hs.vps) {
        state = TS_STOPPED;
        return;
    }
    if (sv.state != SV_RUNNING) {
        if (sv.state == SV_STOPPED && tunnel_alive(&vps.t)) tunnel_stop(&vps.t);
        state = TS_STOPPED;
        return;
    }
    if (!tunnel_alive(&vps.t)) {                       /* not started, or the loop itself died */
        state = TS_STOPPED;
        if (now >= retry_at) start_tunnel();
        return;
    }
    state = tunnel_state(&vps.t, &vps.cfg, why, sizeof why, &attempt);
    if (state == TS_UP && attempt != checked_try) {     /* in: check the way in from outside */
        checked_try = attempt;
        vps.why[0] = 0;
        sv_note("the tunnel to your VPS is up.");
        check_at = now + 1500;
    } else if (state == TS_RETRYING && attempt != told_try) {
        char note[260];
        int first = told_try == -2 || checked_try == attempt;   /* it was up, or it's the first try */
        told_try = attempt;
        snprintf(vps.why, sizeof vps.why, "%s", why);
        snprintf(note, sizeof note, "the tunnel to your VPS stopped: %s", why);
        sv_note(note);
        if (first) {
            ui_toast_icon(ICON_NETWORK);
            ui_toast("The tunnel to your VPS stopped", "The Internet page says why");
        }
    }
}

/* For the screens: the tunnel in a few words, and a colour for it. */
const char *vps_status(uint32_t *color)
{
    *color = C_TEXT;
    if (!hs.vps) return "Off";
    if (sv.state != SV_RUNNING) {
        *color = C_SUB;
        return sv.state == SV_STARTING ? "Starts when the server is ready" : "Runs while the server does";
    }
    switch (state) {                                   /* vps_poll keeps it up to date */
    case TS_UP:
        if (vps.check[0] && !vps.check_ok && !checking) {
            *color = C_WARN;
            return "Connected, but players can't get in yet";
        }
        *color = C_OK;
        return "Connected";
    case TS_CONNECTING: *color = C_WARN; return "Connecting";
    case TS_RETRYING: *color = C_BAD; return "Not connected. It tries again by itself";
    default: *color = C_WARN; return "Starting";
    }
}

int vps_up(void)
{
    return hs.vps && state == TS_UP;
}
