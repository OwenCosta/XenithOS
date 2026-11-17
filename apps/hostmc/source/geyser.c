/* Bedrock players: Geyser and Floodgate, on a Paper server.
 *
 * Geyser (a Paper plugin) lets Minecraft: Bedrock Edition (phones, tablets, consoles, Windows) join a
 * Java Edition server, on a port of its own (19132, Bedrock's usual one), and Floodgate lets those
 * players in without a Java account. Both are made by GeyserMC and are free software under the MIT
 * licence (github.com/GeyserMC/Geyser and /Floodgate). With Floodgate on the same server, Geyser
 * signs Bedrock players in through it by itself.
 *
 * Turning Bedrock players on (Version, with Paper) asks first, then downloads both from GeyserMC's
 * download service: the newest build's details (version, build number and the SHA-256 of each
 * file) come from /v2/projects/<project>/versions/latest/builds/latest, and that very build is
 * downloaded and checked against them, into the server's plugins folder. hostmc/geyser.txt says
 * which builds they are. Update fetches newer ones the same way (Bedrock's own updates need a Geyser
 * that knows them). Turning it off takes them out of plugins again; their settings stay in
 * plugins/Geyser-Spigot and plugins/floodgate for next time. HOSTMC_GEYSER_API points HostMC at
 * another download service (the tests' stand-in).
 *
 * Bedrock players come over the home network, and from the internet through playit.gg, whose
 * Minecraft Bedrock tunnel carries what Bedrock needs: SSH tunnels (a VPS of your own, the
 * supporter relay) carry TCP only, and Bedrock needs UDP.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hostmc.h"

#define GEYSER_API_DEFAULT "https://download.geysermc.org/v2"
#define PLUGINS            HM_SERVER "/plugins"
#define GEYSER_FILE        HM_DIR "/geyser.txt"

static const struct { const char *project, *name, *jar; } parts[2] = {
    {"geyser", "Geyser", "Geyser-Spigot.jar"},
    {"floodgate", "Floodgate", "floodgate-spigot.jar"},
};

static const char *api(void)
{
    const char *env = getenv("HOSTMC_GEYSER_API");
    return env && *env ? env : GEYSER_API_DEFAULT;
}

/* Both are in the server's plugins folder. */
int geyser_installed(void)
{
    char path[200];
    int i;
    for (i = 0; i < 2; i++) {
        snprintf(path, sizeof path, PLUGINS "/%s", parts[i].jar);
        if (file_size(path) <= 0) return 0;
    }
    return 1;
}

/* Bedrock players can join: turned on, with Paper, and the plugins there. */
int geyser_on(void)
{
    return hs.bedrock && !strcmp(hs.type, "paper") && geyser_installed();
}

/* The Bedrock port, from Geyser's settings once it has made them (bedrock: port:), else its
 * usual one. */
int geyser_port(void)
{
    char *t = file_read(PLUGINS "/Geyser-Spigot/config.yml", NULL), *p;
    int port = BEDROCK_PORT, in_bedrock = 0;
    if (!t) return port;
    for (p = t; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL) {
        int k = (int)strspn(p, " ");
        if (p[k] == '#' || p[k] == '\n' || p[k] == '\r' || !p[k]) continue;
        if (k == 0) {
            in_bedrock = !strncmp(p, "bedrock:", 8);
            continue;
        }
        if (in_bedrock && !strncmp(p + k, "port:", 5)) {
            int v = atoi(p + k + 5 + strspn(p + k + 5, " "));
            if (v > 0 && v < 65536) port = v;
            break;
        }
    }
    free(t);
    return port;
}

/* "Geyser 2.12.1, build 1303" for each, from geyser.txt. */
void geyser_versions(char *geyser, char *floodgate, int n)
{
    char *t = file_read(GEYSER_FILE, NULL), *p;
    char *outs[2];
    int i;
    outs[0] = geyser;
    outs[1] = floodgate;
    for (i = 0; i < 2; i++) {
        char key[24];
        snprintf(outs[i], n, "Not known");
        snprintf(key, sizeof key, "%s=", parts[i].project);
        if (t && (p = strstr(t, key))) {
            p += strlen(key);
            snprintf(outs[i], n, "%.*s", (int)strcspn(p, "\r\n"), p);
        }
    }
    free(t);
}

/* ------------------------------------------------------------- downloading */

struct geyser_plan { char version[32], build[16], sha256[72], url[300]; };

/* The newest build of one of them: its version, build number and checksum, and where that very
 * build downloads from. */
static int plan_part(int i, struct geyser_plan *g, char *why, int n)
{
    char url[300], cache[120], title[64], name[64] = "";
    const char *dl;
    char *text;
    long long build;
    memset(g, 0, sizeof *g);
    snprintf(url, sizeof url, "%s/projects/%s/versions/latest/builds/latest", api(), parts[i].project);
    snprintf(cache, sizeof cache, HM_CACHE "/%s-latest.json", parts[i].project);
    snprintf(title, sizeof title, "Looking for %s", parts[i].name);
    if (!(text = fetch_text(url, cache, 0, title, why, n))) return 0;
    js_str(js_key(text, "version"), g->version, sizeof g->version);
    build = js_num(js_key(text, "build"));
    dl = js_key(js_key(text, "downloads"), "spigot");
    js_str(js_key(dl, "name"), name, sizeof name);
    js_str(js_key(dl, "sha256"), g->sha256, sizeof g->sha256);
    free(text);
    if (!g->version[0] || build <= 0 || strspn(g->version, "0123456789.") != strlen(g->version)) {
        snprintf(why, n, "GeyserMC's download service didn't say which %s to download. Try again later.", parts[i].name);
        return 0;
    }
    if (strlen(g->sha256) != 64 || strspn(g->sha256, "0123456789abcdefABCDEF") != 64) {
        snprintf(why, n, "GeyserMC didn't give a checksum for %s, so HostMC won't use it.", parts[i].name);
        return 0;
    }
    if (strcmp(name, parts[i].jar)) {
        snprintf(why, n, "GeyserMC's newest %s isn't the file HostMC expects (%s). Try again later.", parts[i].name, name);
        return 0;
    }
    snprintf(g->build, sizeof g->build, "%lld", build);
    snprintf(g->url, sizeof g->url, "%s/projects/%s/versions/%s/builds/%s/downloads/spigot", api(), parts[i].project,
             g->version, g->build);
    return 1;
}

static struct geyser_plan planned[2];          /* what geyser_plan found, for geyser_get */
static int planned_ok;

/* What's newest, without downloading anything (a little JSON for each). 1 with the words for the
 * question filled in. */
int geyser_plan(char *text, int n, char *why, int wn)
{
    int i;
    planned_ok = 0;
    for (i = 0; i < 2; i++)
        if (!plan_part(i, &planned[i], why, wn)) return 0;
    planned_ok = 1;
    snprintf(text, n, "Geyser %s (build %s) and Floodgate %s (build %s), from GeyserMC. Free software under the MIT licence. "
                      "They go in the server's plugins folder, checked against the checksums GeyserMC publishes.",
             planned[0].version, planned[0].build, planned[1].version, planned[1].build);
    return 1;
}

/* geyser_plan found a build of either that isn't the one in plugins. */
int geyser_newer(void)
{
    char have[2][64], want[64];
    int i;
    if (!planned_ok) return 0;
    geyser_versions(have[0], have[1], sizeof have[0]);
    for (i = 0; i < 2; i++) {
        snprintf(want, sizeof want, "%s, build %s", planned[i].version, planned[i].build);
        if (strcmp(want, have[i])) return 1;
    }
    return 0;
}

/* The downloads waiting to go in (plugins/<jar>.new), thrown away. */
static void drop_new(void)
{
    char tmp[220];
    int i;
    for (i = 0; i < 2; i++) {
        snprintf(tmp, sizeof tmp, PLUGINS "/%s.new", parts[i].jar);
        unlink(tmp);
    }
}

/* Downloads the newest of both (as geyser_plan found them) into plugins, each checked against
 * its SHA-256 before it goes in, and both go in only once both are good. 0 with why
 * ("Cancelled." if cancelled). */
int geyser_get(char *why, int n)
{
    struct geyser_plan *g = planned;
    char out[200], tmp[220], title[64], detail[96], line[2][80], buf[200];
    int i, code, len;
    if (!planned_ok)
        for (i = 0; i < 2; i++)
            if (!plan_part(i, &planned[i], why, n)) return 0;
    planned_ok = 0;                                  /* each download from a plan of its own */
    mkdirs(PLUGINS);
    for (i = 0; i < 2; i++) {
        snprintf(out, sizeof out, PLUGINS "/%s", parts[i].jar);
        snprintf(tmp, sizeof tmp, "%s.new", out);
        snprintf(title, sizeof title, "Downloading %s %s", parts[i].name, g[i].version);
        snprintf(detail, sizeof detail, "Build %s, from GeyserMC", g[i].build);
        code = fetch(g[i].url, tmp, title, detail, 0, 1);
        if (code) {
            if (code == -2) snprintf(why, n, "Cancelled.");
            else fetch_reason(code, why, n);
            drop_new();
            return 0;
        }
        if (!checksum_ok("sha256", tmp, g[i].sha256)) {
            drop_new();
            snprintf(why, n, "%s didn't match its checksum, so it was thrown away. Try again.", parts[i].name);
            return 0;
        }
        snprintf(line[i], sizeof line[i], "%s=%s, build %s", parts[i].project, g[i].version, g[i].build);
    }
    for (i = 0; i < 2; i++) {                        /* both are good: in they go */
        snprintf(out, sizeof out, PLUGINS "/%s", parts[i].jar);
        snprintf(tmp, sizeof tmp, "%s.new", out);
        if (rename(tmp, out) != 0) {
            snprintf(why, n, "%s couldn't be put in the plugins folder.", parts[i].name);
            return 0;
        }
    }
    len = snprintf(buf, sizeof buf, "%s\n%s\n", line[0], line[1]);
    file_write(GEYSER_FILE, buf, len, 0644);
    return 1;
}

/* Out of the plugins folder; their settings stay. */
void geyser_remove(void)
{
    char path[200];
    int i;
    for (i = 0; i < 2; i++) {
        snprintf(path, sizeof path, PLUGINS "/%s", parts[i].jar);
        unlink(path);
    }
    unlink(GEYSER_FILE);
}
