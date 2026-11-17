/* The player's own server on the internet (a VPS) as the way in for friends elsewhere, in place
 * of playit.gg. While the Minecraft server runs, HostMC keeps an SSH reverse tunnel (tunnel.c,
 * run by way.c) to the VPS: players connect to the VPS's public port and arrive here.
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

#include "hostmc.h"
#include "ui.h"

#define VPS_DIR  HM_DIR "/vps"
#define VPS_CONF VPS_DIR "/vps.txt"

struct way vps;

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

/* What friends type in Minecraft: the VPS's address, with the port if it isn't the usual one. */
void vps_address(char *out, int n)
{
    if (vps.cfg.public_port == DEFAULT_PORT) snprintf(out, n, "%s", vps.cfg.host);
    else snprintf(out, n, "%s:%d", vps.cfg.host, vps.cfg.public_port);
}

static void target(char *host, int n, int *port)
{
    snprintf(host, n, "%s", vps.cfg.host);
    *port = vps.cfg.public_port;
}

/* What the check from outside found, for a VPS: "" when friends get in. */
static void finding(const struct mcping *p, char *out, int n)
{
    int port = vps.cfg.public_port;
    out[0] = 0;
    switch (p->result) {
    case PING_OK:
        break;
    case PING_REFUSED:
        snprintf(out, n, "The VPS turned players away on port %d although the tunnel is up, so its sshd keeps the port to itself. "
                 "Add GatewayPorts clientspecified to /etc/ssh/sshd_config, then sudo systemctl restart ssh.", port);
        break;
    case PING_TIMEOUT:
        snprintf(out, n, "Nothing answered on port %d of the VPS. Open it in the VPS's firewall (sudo ufw allow %d/tcp), "
                 "and in your provider's firewall if it has one.", port, port);
        break;
    case PING_NO_HOST:
        snprintf(out, n, "%s doesn't lead anywhere: check the address.", vps.cfg.host);
        break;
    case PING_NOT_MC:
        snprintf(out, n, "Something answered on port %d of the VPS, but not this Minecraft server. Is another program using that port there?", port);
        break;
    case PING_SILENT:
        snprintf(out, n, "The VPS took the connection on port %d, but no answer came back from here. Try the check again in a moment.", port);
        break;
    default:
        snprintf(out, n, "This PC couldn't reach the VPS on port %d just now.", port);
    }
}

void vps_init(void)
{
    char abs[740];
    snprintf(abs, sizeof abs, "%s/vps", hm_root());
    memset(&vps, 0, sizeof vps);
    vps.name = "your VPS";
    vps.Name = "Your VPS";
    vps.on = &hs.vps;
    vps.check_file = VPS_DIR "/check.txt";
    vps.address = vps_address;
    vps.target = target;
    vps.finding = finding;
    way_reset(&vps);
    tunnel_init(&vps.t, VPS_DIR, abs);
    vps_load();
    if (!hs.vps && tunnel_alive(&vps.t)) tunnel_stop(&vps.t);
}

int vps_turn_on(char *why, int n)
{
    if (!tunnel_cfg_ok(&vps.cfg, why, n)) return 0;
    if (!tunnel_key(&vps.t, why, n)) return 0;
    if (hs.internet) pi_turn_off();                  /* one way in at a time */
    if (hs.relay) relay_turn_off();
    hs.vps = 1;
    hs_save();
    way_reset(&vps);
    if (sv.state == SV_RUNNING) way_started(&vps);
    return 1;
}

void vps_turn_off(void)
{
    way_off(&vps);
    hs.vps = 0;
    hs_save();
}

void vps_server_started(void) { way_started(&vps); }
void vps_server_stopped(void) { way_stopped(&vps); }
void vps_poll(void) { way_poll(&vps); }
void vps_check(void) { way_check(&vps); }
int vps_checking(void) { return vps.checking; }
int vps_up(void) { return way_up(&vps); }
const char *vps_status(uint32_t *color) { return way_status(&vps, color); }
