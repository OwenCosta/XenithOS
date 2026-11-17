/* What friends on XenithOS see of HostMC: that it's open and, while the server runs and people
 * on the internet can reach it (through playit.gg, the player's own VPS or the supporter relay),
 * the address they type in Minecraft to join. It's told to XenithOS Cloud now and then, and as
 * soon as the address changes or goes (shared/social.c). Friends only see it when the player lets
 * them see what they're playing (Settings in XenithOS). HostMC's achievements come from here too. */
#include <stdio.h>

#include "config.h"
#include "hostmc.h"
#include "platform.h"
#include "social.h"

/* The address players elsewhere type, or "" when there isn't one right now. */
static void public_address(char *out, int n)
{
    out[0] = 0;
    if (sv.state != SV_RUNNING) return;
    if (pi.state == PI_ON && pi.address[0]) snprintf(out, n, "%s", pi.address);
    else if (vps_up() && vps.check_ok) vps_address(out, n);
    else if (relay_up() && relay.w.check_ok && relay.address[0]) snprintf(out, n, "%s", relay.address);
}

/* Achievements, from what HostMC sees: the server running, people on the internet able to reach
 * it, a player on it, a backup of the world. Each is looked for until it's earned this run. */
static void achievements(const char *addr)
{
    static int start, online, join, backup;
    static uint64_t looked_at_backups;
    uint64_t now = plat_time_us();
    int any = 0;
    if (!prof.signed_in) return;
    if (!start && sv.state == SV_RUNNING) { start = 1; any |= social_earn("hostmc-start"); }
    if (!online && addr[0]) { online = 1; any |= social_earn("hostmc-online"); }
    if (!join && sv.nplayers > 0) { join = 1; any |= social_earn("hostmc-join"); }
    if (!backup && (!looked_at_backups || now - looked_at_backups > 5000000ull)) {
        struct backup_list b;
        looked_at_backups = now;
        world_backups(&b);
        if (b.n > 0) { backup = 1; any |= social_earn("hostmc-backup"); }
    }
    if (any) social_flush();
}

void share_poll(void)
{
    static int looked;
    char addr[160];
    if (!looked) {                      /* a PC's network is simply there: this notices it */
        looked = 1;
        if (!net_is_up()) net_up();
    }
    public_address(addr, sizeof addr);
    social_tick("hostmc", addr);
    achievements(addr);
}
