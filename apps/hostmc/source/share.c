/* What friends on XenithOS see of HostMC: that it's open and, while the server runs and people
 * on the internet can reach it (through playit.gg or the player's own VPS), the address they
 * type in Minecraft to join. It's told to XenithOS Cloud now and then, and as soon as the
 * address changes or goes (shared/social.c). Friends only see it when the player lets them see
 * what they're playing (Settings in XenithOS). */
#include <stdio.h>

#include "hostmc.h"
#include "social.h"

/* The address players elsewhere type, or "" when there isn't one right now. */
static void public_address(char *out, int n)
{
    out[0] = 0;
    if (sv.state != SV_RUNNING) return;
    if (pi.state == PI_ON && pi.address[0]) snprintf(out, n, "%s", pi.address);
    else if (vps_up() && vps.check_ok) vps_address(out, n);
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
}
