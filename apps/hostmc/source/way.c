/* A way in from the internet through an SSH tunnel (tunnel.c) that runs with the server: the
 * player's own VPS (vps.c) or the supporter relay (relay.c). This is what the two share.
 *
 * While the Minecraft server runs, the tunnel runs too: it's started when the server is ready
 * and stopped with it. Each try ssh makes is followed in tunnel.log, and the player is told when
 * the tunnel stops (once a try, not every second). Each time it comes up, HostMC asks the server
 * for its status from outside, the way a friend's game would (ping.c), so anything still in the
 * way shows as a plain message; the owner says what each outcome means for it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "hostmc.h"
#include "icons.h"
#include "theme.h"
#include "ui.h"

void way_reset(struct way *w)
{
    mcping_cancel(&w->ping);
    w->checking = 0;
    w->check_at = 0;
    w->state = TS_STOPPED;
    w->why[0] = w->check[0] = 0;
    w->retry_at = 0;
    w->checked_try = w->told_try = -2;               /* nothing in tunnel.log checked or told yet */
}

static void start_tunnel(struct way *w)
{
    char why[220] = "", note[260];
    /* The relay's tunnel goes straight to Paper's own port when there's a doorway in front of it
     * (door.c): its router sends the player's address itself. */
    w->cfg.local_port = w->direct ? door_paper_port() : server_port();
    w->checked_try = w->told_try = -2;               /* a new tunnel.log */
    if (tunnel_start(&w->t, &w->cfg, why, sizeof why)) {
        snprintf(note, sizeof note, "connecting to %s.", w->name);
        sv_note(note);
    } else {
        snprintf(w->why, sizeof w->why, "%s", why);
        w->retry_at = ui_ms() + 30000;
    }
}

void way_started(struct way *w)
{
    if (!*w->on || !w->cfg.host[0]) return;
    w->retry_at = 0;
    w->why[0] = 0;
    if (!tunnel_alive(&w->t)) start_tunnel(w);
}

void way_stopped(struct way *w)
{
    char note[200];
    if (tunnel_alive(&w->t)) {
        snprintf(note, sizeof note, "the tunnel to %s closed with the server.", w->name);
        sv_note(note);
    }
    tunnel_stop(&w->t);
    mcping_cancel(&w->ping);
    w->checking = 0;
    w->check_at = 0;
    w->state = TS_STOPPED;
}

void way_off(struct way *w)
{
    tunnel_stop(&w->t);
    way_reset(w);
}

void way_check(struct way *w)
{
    char host[160];
    int port;
    mcping_cancel(&w->ping);
    w->check[0] = 0;
    w->checking = 1;
    w->check_at = 0;
    w->target(host, sizeof host, &port);
    mcping_start(&w->ping, host, port, 10000);
}

static void check_done(struct way *w)
{
    char addr[160], buf[400], title[80];
    int n;
    w->address(addr, sizeof addr);
    w->checking = 0;
    w->finding(&w->ping, w->check, sizeof w->check);
    w->check_ok = !w->check[0];
    if (w->check_ok)
        snprintf(w->check, sizeof w->check, "Checked from the internet just now: friends reach the server at %s (%d of %d on).",
                 addr, w->ping.online, w->ping.max);
    /* What it found, kept for later (and for anyone helping) */
    n = snprintf(buf, sizeof buf, "%s\n%s\n", w->check_ok ? "ok" : "failed", w->check);
    file_write(w->check_file, buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1, 0600);
    ui_toast_icon(ICON_NETWORK);
    if (w->check_ok) {
        snprintf(title, sizeof title, "%s is passing players on", w->Name);
        ui_toast(title, addr);
    } else {
        sfx(SFX_ERROR);
        snprintf(title, sizeof title, "Friends can't get in through %s", w->name);
        ui_toast(title, "The Internet page says why");
        sv_note(w->check);
    }
}

void way_poll(struct way *w)
{
    uint64_t now = ui_ms();
    long long attempt;
    char why[220];
    if (w->checking && mcping_poll(&w->ping)) check_done(w);
    if (w->check_at && now >= w->check_at && w->state == TS_UP && sv.state == SV_RUNNING) way_check(w);
    if (now < w->next) return;
    w->next = now + 1000;
    if (!*w->on) {
        w->state = TS_STOPPED;
        return;
    }
    if (sv.state != SV_RUNNING) {
        if (sv.state == SV_STOPPED && tunnel_alive(&w->t)) tunnel_stop(&w->t);
        w->state = TS_STOPPED;
        return;
    }
    if (!tunnel_alive(&w->t)) {                        /* not started, or the loop itself died */
        w->state = TS_STOPPED;
        if (now >= w->retry_at) start_tunnel(w);
        return;
    }
    w->state = tunnel_state(&w->t, &w->cfg, why, sizeof why, &attempt);
    if (w->state == TS_UP && attempt != w->checked_try) {     /* in: check the way in from outside */
        char note[200];
        w->checked_try = attempt;
        w->why[0] = 0;
        snprintf(note, sizeof note, "the tunnel to %s is up.", w->name);
        sv_note(note);
        w->check_at = now + 1500;
        if (w->check_at < w->check_after) w->check_at = w->check_after;   /* until the relay knows what changed */
    } else if (w->state == TS_RETRYING && attempt != w->told_try) {
        char note[300], title[80];
        int first = w->told_try == -2 || w->checked_try == attempt;   /* it was up, or it's the first try */
        w->told_try = attempt;
        snprintf(w->why, sizeof w->why, "%s", why);
        snprintf(note, sizeof note, "the tunnel to %s stopped: %s", w->name, why);
        sv_note(note);
        if (first) {
            snprintf(title, sizeof title, "The tunnel to %s stopped", w->name);
            ui_toast_icon(ICON_NETWORK);
            ui_toast(title, "The Internet page says why");
        }
    }
}

/* For the screens: the tunnel in a few words, and a colour for it. */
const char *way_status(const struct way *w, uint32_t *color)
{
    *color = C_TEXT;
    if (!*w->on) return "Off";
    if (sv.state != SV_RUNNING) {
        *color = C_SUB;
        return sv.state == SV_STARTING ? "Starts when the server is ready" : "Runs while the server does";
    }
    switch (w->state) {                                /* way_poll keeps it up to date */
    case TS_UP:
        if (w->check[0] && !w->check_ok && !w->checking) {
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

int way_up(const struct way *w)
{
    return *w->on && w->state == TS_UP;
}
