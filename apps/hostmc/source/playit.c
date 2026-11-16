/* playit.gg: an address on the internet for the server, so friends elsewhere can join without
 * anyone changing the router. It's off until it's turned on in Internet.
 *
 * Turning it on downloads playit's agent (playitd) for this processor from playit's releases on
 * GitHub, checked against the SHA-256 GitHub publishes for it, into hostmc/playit. Then
 * playit.gg is asked for a claim: its link (and QR code) opens on a phone, and the user approves
 * it with their playit.gg account. HostMC trades the approved claim for the agent's secret key,
 * which stays in hostmc/playit/playit.toml, readable only by this user. It never goes on a
 * command line, where other users of the PC could see it: the agent reads it from the file, and
 * curl reads the header with it from auth.txt next to it.
 *
 * The agent runs while the server does. HostMC asks playit.gg for the agent's tunnels with the
 * key (the request the agent itself makes) to show the public address, and if there's no tunnel
 * yet it asks for a Minecraft one to this PC. The requests go one at a time in the background.
 */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "hostmc.h"
#include "ui.h"

#define PI_BIN      HM_PLAYIT "/playitd"
#define PI_SECRET   HM_PLAYIT "/playit.toml"
#define PI_AUTH     HM_PLAYIT "/auth.txt"
#define PI_PID      HM_PLAYIT "/playitd.pid"
#define PI_LOG      HM_PLAYIT "/playitd.log"
#define PI_CODE     HM_PLAYIT "/claim.txt"
#define PI_ADDR     HM_PLAYIT "/address.txt"
#define PI_VERSION  HM_PLAYIT "/version.txt"
#define PI_REQ      HM_PLAYIT "/request.json"
#define PI_RESP     HM_PLAYIT "/response.json"
#define API         "https://api.playit.gg"
#define RELEASES    "https://api.github.com/repos/playit-cloud/playit-agent/releases/latest"

enum { RQ_NONE, RQ_SETUP, RQ_EXCHANGE, RQ_RUNDATA, RQ_CREATE };

struct hm_playit pi;

static struct job req;                 /* the request on its way */
static int req_kind, next_kind;
static uint64_t next_at;
static char code[16], agent_ver[24] = "2.0.2", agent_id[48];
static int create_tried, fails;
static pid_t agent;
static unsigned long long agent_ticks;

static void dir_abs(char *out, int n)
{
    snprintf(out, n, "%s/playit", hm_root());
}

int pi_has_secret(void)
{
    return file_size(PI_SECRET) > 0;
}

/* ------------------------------------------------------------- the agent */

static int agent_alive(void)
{
    char dir[720];
    dir_abs(dir, sizeof dir);
    return agent > 0 && proc_ours(agent, agent_ticks, dir);
}

int pi_agent_running(void)
{
    return agent_alive();
}

static void agent_load(void)
{
    char *t = file_read(PI_PID, NULL), *p;
    agent = 0;
    agent_ticks = 0;
    if (!t) return;
    if ((p = strstr(t, "pid="))) agent = (pid_t)atoi(p + 4);
    if ((p = strstr(t, "ticks="))) agent_ticks = strtoull(p + 6, NULL, 10);
    free(t);
    if (!agent_alive()) {
        agent = 0;
        unlink(PI_PID);
    }
}

static void agent_start(void)
{
    char dir[720], bin[740], secret[740], sock[740], buf[96];
    char *argv[7];
    int in_fd, out_fd, n, tries;
    if (agent_alive() || !pi_has_secret() || access(PI_BIN, X_OK) != 0) return;
    dir_abs(dir, sizeof dir);
    snprintf(bin, sizeof bin, "%s/playitd", dir);
    snprintf(secret, sizeof secret, "%s/playit.toml", dir);
    snprintf(sock, sizeof sock, "%s/playitd.sock", dir);
    argv[0] = bin;
    argv[1] = "--secret-path";
    argv[2] = secret;
    argv[3] = "--socket-path";                 /* its own, so a system-wide playit is left alone */
    argv[4] = sock;
    argv[5] = NULL;
    in_fd = open("/dev/null", O_RDONLY);
    out_fd = open(PI_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    agent = in_fd >= 0 && out_fd >= 0 ? spawn_detached(dir, argv, in_fd, out_fd) : 0;
    if (in_fd >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    if (agent <= 0) {
        agent = 0;
        snprintf(pi.note, sizeof pi.note, "playit.gg's agent couldn't start.");
        return;
    }
    for (tries = 0; tries < 50 && !(agent_ticks = proc_ticks(agent)); tries++) plat_sleep_us(2000);
    n = snprintf(buf, sizeof buf, "pid=%d\nticks=%llu\n", (int)agent, agent_ticks);
    file_write(PI_PID, buf, n, 0600);
    sv_note("playit.gg's agent started.");
}

static void agent_stop(void)
{
    if (agent_alive()) {
        kill(agent, SIGTERM);
        sv_note("playit.gg's agent stopped.");
    }
    agent = 0;
    unlink(PI_PID);
}

void pi_server_started(void)
{
    if (hs.internet && pi_has_secret()) agent_start();
}

void pi_server_stopped(void)
{
    agent_stop();
}

/* ---------------------------------------------------------- requests */

static int post(int kind, const char *path, const char *body, int with_key)
{
    char url[160];
    const char *with[] = {"curl", "-sS", "--max-time", "20", "-A", HM_AGENT, "-X", "POST",
                          "-H", "Content-Type: application/json", "-H", "@" PI_AUTH,
                          "--data-binary", "@" PI_REQ, "-o", PI_RESP, url, NULL};
    const char *without[] = {"curl", "-sS", "--max-time", "20", "-A", HM_AGENT, "-X", "POST",
                             "-H", "Content-Type: application/json",
                             "--data-binary", "@" PI_REQ, "-o", PI_RESP, url, NULL};
    snprintf(url, sizeof url, API "%s", path);
    if (!file_write(PI_REQ, body, (long)strlen(body), 0600)) return 0;
    unlink(PI_RESP);
    if (!job_spawn(&req, with_key ? with : without, NULL, NULL)) return 0;
    req_kind = kind;
    return 1;
}

static void new_code(void)
{
    unsigned char b[5];
    char buf[32];
    int fd = open("/dev/urandom", O_RDONLY), i, n;
    if (fd < 0 || read(fd, b, sizeof b) != (ssize_t)sizeof b) {
        for (i = 0; i < 5; i++) b[i] = (unsigned char)(rand() ^ (int)ui_ms() ^ (i * 77));
    }
    if (fd >= 0) close(fd);
    for (i = 0; i < 5; i++) snprintf(code + i * 2, 3, "%02x", b[i]);
    snprintf(pi.claim_url, sizeof pi.claim_url, "https://playit.gg/claim/%s", code);
    pi.claim_seen = 0;
    n = snprintf(buf, sizeof buf, "%s\n", code);
    file_write(PI_CODE, buf, n, 0600);
}

static int save_secret(const char *key)
{
    char buf[256];
    int n, i;
    for (i = 0; key[i]; i++)
        if (!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f') || (key[i] >= 'A' && key[i] <= 'F'))) return 0;
    if (i < 16 || i > 200) return 0;
    n = snprintf(buf, sizeof buf, "secret_key = \"%s\"\n", key);
    if (!file_write(PI_SECRET, buf, n, 0600)) return 0;
    n = snprintf(buf, sizeof buf, "Authorization: Agent-Key %s\n", key);
    return file_write(PI_AUTH, buf, n, 0600);
}

/* The secret file is the key alone or `secret_key = "..."`; auth.txt is made from it. */
static void ensure_auth(void)
{
    char *t, key[220] = "", *q;
    if (file_size(PI_AUTH) > 0 || !(t = file_read(PI_SECRET, NULL))) return;
    q = strchr(t, '"');
    snprintf(key, sizeof key, "%s", q ? q + 1 : t);
    key[strcspn(key, "\"\r\n ")] = 0;
    free(t);
    save_secret(key);
}

/* What playit.gg says when a tunnel can't be made, for people. */
static void create_problem(const char *why)
{
    if (!strcmp(why, "RequiresVerifiedAccount"))
        snprintf(pi.note, sizeof pi.note, "playit.gg wants your account's email checked before it makes a tunnel. Then add a Minecraft Java tunnel for this agent on playit.gg.");
    else
        snprintf(pi.note, sizeof pi.note, "Add a tunnel on playit.gg: Minecraft Java, to this agent, local port %d.", server_port());
}

static void handle(int kind, int curl_code)
{
    char *t = curl_code == 0 ? file_read(PI_RESP, NULL) : NULL, status[16] = "", s[220];
    const char *data = NULL;
    uint64_t now = ui_ms();
    if (t) {
        js_str(js_key(t, "status"), status, sizeof status);
        data = js_key(t, "data");
    }
    if (!status[0]) {
        fails++;
        snprintf(pi.note, sizeof pi.note, "playit.gg can't be reached right now. HostMC keeps trying.");
        next_at = now + (fails < 5 ? 5000 : 30000);
        next_kind = kind == RQ_EXCHANGE ? RQ_EXCHANGE : RQ_NONE;
        free(t);
        return;
    }
    fails = 0;
    if (!strcmp(status, "error")) {
        char type[32] = "";
        js_str(js_key(data, "type"), type, sizeof type);
        if (!strcmp(type, "auth") && kind != RQ_SETUP && kind != RQ_EXCHANGE) {
            pi.state = PI_PROBLEM;
            snprintf(pi.note, sizeof pi.note, "playit.gg doesn't accept this agent any more. Forget it below, then turn playit.gg on again.");
            agent_stop();
        } else {
            snprintf(pi.note, sizeof pi.note, "playit.gg had a problem (%s). HostMC tries again soon.", type[0] ? type : "unknown");
        }
        next_at = now + 30000;
        free(t);
        return;
    }
    switch (kind) {
    case RQ_SETUP:
        if (!strcmp(status, "success") && js_str(data, s, sizeof s)) {
            if (!strcmp(s, "WaitingForUserVisit")) {
                pi.claim_seen = 0;
                snprintf(pi.note, sizeof pi.note, "Waiting for the link to be opened.");
            } else if (!strcmp(s, "WaitingForUser")) {
                pi.claim_seen = 1;
                snprintf(pi.note, sizeof pi.note, "Now approve it on playit.gg.");
            } else if (!strcmp(s, "UserAccepted")) {
                snprintf(pi.note, sizeof pi.note, "Approved. Getting the agent ready.");
                next_kind = RQ_EXCHANGE;
            } else if (!strcmp(s, "UserRejected")) {
                pi.state = PI_PROBLEM;
                snprintf(pi.note, sizeof pi.note, "It was turned down on playit.gg. Turn playit.gg off and on to try again.");
                unlink(PI_CODE);
            }
        } else {
            new_code();                                  /* expired, or unknown: start again */
            snprintf(pi.note, sizeof pi.note, "That link expired, so here's a new one.");
        }
        next_at = now + 2000;
        break;
    case RQ_EXCHANGE:
        if (!strcmp(status, "success") && js_str(js_key(data, "secret_key"), s, sizeof s) && save_secret(s)) {
            unlink(PI_CODE);
            pi.state = PI_STARTING;
            snprintf(pi.note, sizeof pi.note, "Connected to your playit.gg account.");
            sv_note("playit.gg approved this PC's agent.");
            if (sv.state == SV_RUNNING) agent_start();
            next_kind = RQ_RUNDATA;
            next_at = now;
        } else {
            js_str(data, s, sizeof s);
            if (!strcmp(s, "NotAccepted") || !strcmp(s, "NotSetup")) {
                next_kind = RQ_EXCHANGE;
                next_at = now + 2000;
            } else {
                new_code();
                pi.state = PI_CLAIM;
                snprintf(pi.note, sizeof pi.note, "That claim didn't work out. Open the new link to try again.");
                next_at = now + 2000;
            }
        }
        break;
    case RQ_RUNDATA:
        if (!strcmp(status, "success")) {
            const char *tun, *best = NULL, *notice;
            char type[32];
            js_str(js_key(data, "agent_id"), agent_id, sizeof agent_id);
            for (tun = js_first(js_key(data, "tunnels")); tun; tun = js_next(tun)) {
                js_str(js_key(tun, "tunnel_type"), type, sizeof type);
                if (!best || !strcmp(type, "minecraft-java")) best = tun;
                if (!strcmp(type, "minecraft-java")) break;
            }
            pi.note[0] = 0;
            if (best) {
                char addr[96] = "", off[120] = "";
                js_str(js_key(best, "display_address"), addr, sizeof addr);
                js_str(js_key(best, "disabled_reason"), off, sizeof off);
                if (addr[0] && strcmp(addr, pi.address)) {
                    snprintf(pi.address, sizeof pi.address, "%s", addr);
                    file_write(PI_ADDR, addr, (long)strlen(addr), 0600);
                }
                pi.state = PI_ON;
                if (off[0]) snprintf(pi.note, sizeof pi.note, "playit.gg has switched the tunnel off: %s", off);
            } else if (js_first(js_key(data, "pending"))) {
                js_str(js_key(js_first(js_key(data, "pending")), "status_msg"), s, sizeof s);
                snprintf(pi.note, sizeof pi.note, "playit.gg is making the tunnel%s%s", s[0] ? ": " : ".", s);
            } else if (!create_tried) {
                next_kind = RQ_CREATE;
            } else {
                create_problem("");
            }
            notice = js_first(js_key(data, "notices"));
            if (notice && !pi.note[0] && js_str(js_key(notice, "message"), s, sizeof s)) snprintf(pi.note, sizeof pi.note, "%s", s);
        }
        next_at = now + (pi.state == PI_ON ? 60000 : 5000);
        break;
    case RQ_CREATE:
        create_tried = 1;
        if (!strcmp(status, "success")) {
            snprintf(pi.note, sizeof pi.note, "Made a Minecraft tunnel. Waiting for its address.");
            sv_note("playit.gg made a tunnel for the server.");
        } else {
            js_str(data, s, sizeof s);
            create_problem(s);
        }
        next_kind = RQ_RUNDATA;
        next_at = now + 3000;
        break;
    }
    free(t);
}

void pi_poll(void)
{
    static uint64_t agent_check;
    uint64_t now = ui_ms();
    char body[600];
    if (req.running) {
        if (job_done(&req)) {
            int k = req_kind;
            req_kind = RQ_NONE;
            handle(k, req.status);
        }
        return;
    }
    if (!hs.internet) return;
    /* The agent runs exactly while the server does. */
    if (now - agent_check > 3000) {
        agent_check = now;
        if (sv.state == SV_RUNNING && pi_has_secret() && pi.state != PI_PROBLEM && !agent_alive()) agent_start();
        else if (sv.state == SV_STOPPED && agent_alive()) agent_stop();
    }
    if (now < next_at) return;
    if (next_kind == RQ_EXCHANGE) {
        snprintf(body, sizeof body, "{\"code\":\"%s\"}", code);
        next_kind = RQ_NONE;
        post(RQ_EXCHANGE, "/claim/exchange", body, 0);
    } else if (next_kind == RQ_CREATE && pi_has_secret()) {
        next_kind = RQ_NONE;
        snprintf(body, sizeof body,
                 "{\"ports\":{\"type\":\"tunnel-type\",\"details\":\"minecraft-java\"},"
                 "\"origin\":{\"type\":\"agent\",\"data\":{\"agent_id\":%s%s%s,\"config\":{\"fields\":["
                 "{\"name\":\"local_ip\",\"value\":\"127.0.0.1\"},{\"name\":\"local_port\",\"value\":\"%d\"}]}}},"
                 "\"enabled\":true,\"alloc\":null,\"name\":\"HostMC\",\"firewall_id\":null}",
                 agent_id[0] ? "\"" : "", agent_id[0] ? agent_id : "null", agent_id[0] ? "\"" : "", server_port());
        ensure_auth();
        post(RQ_CREATE, "/v1/tunnels/create", body, 1);
    } else if (pi.state == PI_CLAIM) {
        snprintf(body, sizeof body, "{\"code\":\"%s\",\"agent_type\":\"assignable\",\"version\":\"playit %s\"}", code, agent_ver);
        post(RQ_SETUP, "/claim/setup", body, 0);
    } else if ((pi.state == PI_STARTING || pi.state == PI_ON) && pi_has_secret()) {
        next_kind = RQ_NONE;
        ensure_auth();
        post(RQ_RUNDATA, "/v1/agents/rundata", "{}", 1);
    }
    if (!req.running && next_at <= now) next_at = now + 5000;   /* couldn't send it: later */
}

/* ------------------------------------------------------------ on and off */

static const char *agent_arch(void)
{
    static struct utsname u;
    if (uname(&u) != 0) return "amd64";
    if (!strcmp(u.machine, "x86_64") || !strcmp(u.machine, "amd64")) return "amd64";
    if (!strcmp(u.machine, "aarch64") || !strcmp(u.machine, "arm64")) return "aarch64";
    if (!strncmp(u.machine, "armv7", 5) || !strncmp(u.machine, "armv8l", 6)) return "armv7";
    if (!strcmp(u.machine, "i686") || !strcmp(u.machine, "i386")) return "i686";
    return u.machine;
}

/* playit's agent from its latest release on GitHub, if it isn't here yet. */
static int get_agent(char *why, int n)
{
    char *text, want[64], url[300] = "", digest[100] = "", tag[24] = "";
    const char *a;
    long long size = 0;
    int code_;
    if (access(PI_BIN, X_OK) == 0) return 1;
    if (!(text = fetch_text(RELEASES, HM_CACHE "/playit-release.json", 3600, "Looking for playit.gg's agent", why, n))) return 0;
    snprintf(want, sizeof want, "playit-linux-%s", agent_arch());
    js_str(js_key(text, "tag_name"), tag, sizeof tag);
    for (a = js_first(js_key(text, "assets")); a; a = js_next(a)) {
        char name[80];
        js_str(js_key(a, "name"), name, sizeof name);
        if (strcmp(name, want)) continue;
        js_str(js_key(a, "browser_download_url"), url, sizeof url);
        js_str(js_key(a, "digest"), digest, sizeof digest);
        size = js_num(js_key(a, "size"));
        break;
    }
    free(text);
    if (!url[0]) {
        snprintf(why, n, "playit.gg's agent isn't made for this processor (%s).", agent_arch());
        return 0;
    }
    if (strncmp(digest, "sha256:", 7)) {
        snprintf(why, n, "GitHub didn't give a checksum for playit.gg's agent, so HostMC won't use it.");
        return 0;
    }
    code_ = fetch(url, PI_BIN ".new", "Downloading playit.gg's agent", "From playit's releases on GitHub", size, 1);
    if (code_) {
        if (code_ == -2) snprintf(why, n, "Cancelled.");
        else fetch_reason(code_, why, n);
        return 0;
    }
    if (!checksum_ok("sha256", PI_BIN ".new", digest + 7)) {
        unlink(PI_BIN ".new");
        snprintf(why, n, "The agent didn't match its checksum, so it was thrown away.");
        return 0;
    }
    chmod(PI_BIN ".new", 0755);
    if (rename(PI_BIN ".new", PI_BIN) != 0) {
        snprintf(why, n, "The agent couldn't be put in place.");
        return 0;
    }
    snprintf(agent_ver, sizeof agent_ver, "%s", tag[0] == 'v' ? tag + 1 : tag);
    file_write(PI_VERSION, agent_ver, (long)strlen(agent_ver), 0644);
    return 1;
}

void pi_init(void)
{
    char *t;
    memset(&pi, 0, sizeof pi);
    agent_load();
    if ((t = file_read(PI_VERSION, NULL))) {
        t[strcspn(t, "\r\n")] = 0;
        if (t[0]) snprintf(agent_ver, sizeof agent_ver, "%s", t);
        free(t);
    }
    if (!hs.internet) {
        pi.state = PI_OFF;
        if (agent_alive()) agent_stop();
        return;
    }
    if (pi_has_secret()) {
        pi.state = PI_STARTING;
        if ((t = file_read(PI_ADDR, NULL))) {
            t[strcspn(t, "\r\n")] = 0;
            snprintf(pi.address, sizeof pi.address, "%s", t);
            if (pi.address[0]) pi.state = PI_ON;
            free(t);
        }
    } else if ((t = file_read(PI_CODE, NULL))) {
        t[strcspn(t, "\r\n ")] = 0;
        snprintf(code, sizeof code, "%.10s", t);
        free(t);
        snprintf(pi.claim_url, sizeof pi.claim_url, "https://playit.gg/claim/%s", code);
        pi.state = PI_CLAIM;
    } else {
        hs.internet = 0;
        pi.state = PI_OFF;
    }
}

int pi_turn_on(char *why, int n)
{
    if (hs.vps) vps_turn_off();                     /* one way in at a time */
    mkdirs(HM_PLAYIT);
    chmod(HM_PLAYIT, 0700);                       /* the secret lives here */
    if (!get_agent(why, n)) return 0;
    hs.internet = 1;
    hs_save();
    pi.note[0] = 0;
    create_tried = 0;
    next_kind = RQ_NONE;
    next_at = 0;
    if (pi_has_secret()) {
        pi.state = pi.address[0] ? PI_ON : PI_STARTING;
        if (sv.state == SV_RUNNING) agent_start();
    } else {
        new_code();
        pi.state = PI_CLAIM;
        snprintf(pi.note, sizeof pi.note, "Waiting for the link to be opened.");
    }
    return 1;
}

void pi_turn_off(void)
{
    if (req.running) job_stop(&req);
    req_kind = next_kind = RQ_NONE;
    agent_stop();
    unlink(PI_CODE);
    hs.internet = 0;
    hs_save();
    pi.state = PI_OFF;
    pi.note[0] = 0;
}

void pi_forget(void)
{
    pi_turn_off();
    unlink(PI_SECRET);
    unlink(PI_AUTH);
    unlink(PI_ADDR);
    pi.address[0] = 0;
    agent_id[0] = 0;
}
