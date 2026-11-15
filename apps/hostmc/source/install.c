/* Getting the server and Java.
 *
 * Minecraft (vanilla) comes from Mojang's version manifest: each release has a JSON file with
 * the server's download (with its SHA-1 and size) and the Java version it needs, 25 for 26.x and
 * 21 for 1.20.5 to 1.21.x. Paper comes from PaperMC's download API (Fill v3), which gives each
 * build's SHA-256 and the Java it needs.
 *
 * Java: Debian 12 only packages Java 17, older than any of that. HostMC downloads Eclipse
 * Temurin's JRE for the version needed from Adoptium (about 60 MB, checked against Adoptium's
 * SHA-256) into hostmc/java/<version>, where nothing else uses it. HOSTMC_JAVA points it at
 * another java instead (tests use a stand-in).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "hostmc.h"

#define MOJANG_MANIFEST "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
#define PAPER_API       "https://fill.papermc.io/v3/projects/paper"

/* 1 if release a is later than b ("26.3" > "1.21.11" > "1.21.9"). */
int version_newer(const char *a, const char *b)
{
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x > y;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if (!*a && !*b) return 0;
        if (!*a) return 0;
        if (!*b) return 1;
        if ((*a < '0' || *a > '9') || (*b < '0' || *b > '9')) return 0;   /* "-rc" and such: equal */
    }
    return 0;
}

static int is_release_id(const char *id)
{
    return id[0] && !strpbrk(id, "-_ ") && strspn(id, "0123456789.") == strlen(id);
}

/* refresh: 1 asks again, 0 asks if the copy is over an hour old, -1 takes the copy (or nothing). */
static char *list_text(const char *url, const char *cache, int refresh, const char *title, char *why, int n)
{
    char *t;
    if (refresh >= 0) return fetch_text(url, cache, refresh ? 0 : 3600, title, why, n);
    if (!(t = file_read(cache, NULL))) snprintf(why, n, "There's no list of versions yet.");
    return t;
}

int versions_get(int paper, int refresh, struct version_list *out, char *why, int n)
{
    char *text;
    memset(out, 0, sizeof *out);
    if (!paper) {
        const char *v;
        text = list_text(MOJANG_MANIFEST, HM_CACHE "/mojang.json", refresh, "Looking for Minecraft versions", why, n);
        if (!text) return 0;
        js_str(js_key(js_key(text, "latest"), "release"), out->latest, sizeof out->latest);
        for (v = js_first(js_key(text, "versions")); v && out->n < MAX_VERSIONS; v = js_next(v)) {
            char type[16];
            js_str(js_key(v, "type"), type, sizeof type);
            if (strcmp(type, "release")) continue;
            js_str(js_key(v, "id"), out->id[out->n], sizeof out->id[0]);
            if (out->id[out->n][0]) out->n++;
        }
    } else {
        const char *families, *fam;
        int i;
        text = list_text(PAPER_API, HM_CACHE "/paper.json", refresh, "Looking for Paper versions", why, n);
        if (!text) return 0;
        families = js_key(text, "versions");
        for (i = 0; (fam = js_member(families, i, NULL, 0)) && out->n < MAX_VERSIONS; i++) {
            const char *v;
            for (v = js_first(fam); v && out->n < MAX_VERSIONS; v = js_next(v)) {
                char id[24];
                js_str(v, id, sizeof id);
                if (is_release_id(id)) snprintf(out->id[out->n++], sizeof out->id[0], "%s", id);
            }
        }
        if (out->n) snprintf(out->latest, sizeof out->latest, "%s", out->id[0]);
    }
    free(text);
    if (!out->n) snprintf(why, n, "The list of versions came back empty. Try again later.");
    return out->n > 0;
}

/* ------------------------------------------------------------- the server */

static int plan_vanilla(const char *id, struct server_plan *p, char *why, int n)
{
    char *manifest, *vjson, url[300] = "", cache[160], title[80];
    const char *v, *server;
    manifest = fetch_text(MOJANG_MANIFEST, HM_CACHE "/mojang.json", 3600, "Looking for Minecraft versions", why, n);
    if (!manifest) return 0;
    for (v = js_first(js_key(manifest, "versions")); v; v = js_next(v)) {
        char vid[32];
        js_str(js_key(v, "id"), vid, sizeof vid);
        if (!strcmp(vid, id)) { js_str(js_key(v, "url"), url, sizeof url); break; }
    }
    free(manifest);
    if (!url[0]) {
        snprintf(why, n, "Mojang doesn't list Minecraft %s.", id);
        return 0;
    }
    snprintf(cache, sizeof cache, HM_CACHE "/minecraft-%s.json", id);
    snprintf(title, sizeof title, "Getting Minecraft %s", id);
    if (!(vjson = fetch_text(url, cache, 30 * 86400, title, why, n))) return 0;
    server = js_key(js_key(vjson, "downloads"), "server");
    js_str(js_key(server, "url"), p->url, sizeof p->url);
    js_str(js_key(server, "sha1"), p->hash, sizeof p->hash);
    p->size = js_num(js_key(server, "size"));
    p->java = (int)js_num(js_key(js_key(vjson, "javaVersion"), "majorVersion"));
    free(vjson);
    if (!p->url[0]) {
        snprintf(why, n, "Minecraft %s has no server download.", id);
        return 0;
    }
    if (p->java <= 0) p->java = 8;
    snprintf(p->algo, sizeof p->algo, "sha1");
    snprintf(p->version, sizeof p->version, "%s", id);
    p->paper = 0;
    return 1;
}

/* The newest stable build of a Paper version (any build when stable_only is 0). */
static int paper_build(const char *id, int stable_only, struct server_plan *p, char *why, int n)
{
    char url[200], cache[160], *text;
    const char *b, *pick = NULL;
    snprintf(url, sizeof url, PAPER_API "/versions/%s/builds", id);
    snprintf(cache, sizeof cache, HM_CACHE "/paper-%s-builds.json", id);
    if (!(text = fetch_text(url, cache, 600, "Looking for Paper builds", why, n))) return 0;
    for (b = js_first(text); b; b = js_next(b)) {
        char channel[16];
        js_str(js_key(b, "channel"), channel, sizeof channel);
        if (!strcmp(channel, "STABLE") || (!stable_only && !pick)) {
            pick = b;
            if (!strcmp(channel, "STABLE")) break;
        }
    }
    if (pick) {
        const char *dl = js_key(js_key(pick, "downloads"), "server:default");
        snprintf(p->build, sizeof p->build, "%lld", js_num(js_key(pick, "id")));
        js_str(js_key(dl, "url"), p->url, sizeof p->url);
        js_str(js_key(js_key(dl, "checksums"), "sha256"), p->hash, sizeof p->hash);
        p->size = js_num(js_key(dl, "size"));
    }
    free(text);
    return pick && p->url[0];
}

static int plan_paper(const char *want, struct server_plan *p, char *why, int n)
{
    struct version_list vl;
    char url[200], cache[160], *text;
    int i, found = 0;
    if (strcmp(want, "latest")) {
        found = paper_build(want, 0, p, why, n);
        if (found) snprintf(p->version, sizeof p->version, "%s", want);
    } else {
        if (!versions_get(1, 0, &vl, why, n)) return 0;
        /* A new Minecraft version gets experimental Paper builds for a while first. */
        for (i = 0; i < vl.n && i < 4 && !found; i++)
            if ((found = paper_build(vl.id[i], 1, p, why, n))) snprintf(p->version, sizeof p->version, "%s", vl.id[i]);
    }
    if (!found) {
        if (!why[0]) snprintf(why, n, "Paper has no build of %s to download yet.", strcmp(want, "latest") ? want : "a recent version");
        return 0;
    }
    snprintf(url, sizeof url, PAPER_API "/versions/%s", p->version);
    snprintf(cache, sizeof cache, HM_CACHE "/paper-%s.json", p->version);
    if ((text = fetch_text(url, cache, 86400, "Looking for Paper builds", why, n))) {
        p->java = (int)js_num(js_key(js_key(js_key(js_key(text, "version"), "java"), "version"), "minimum"));
        free(text);
    }
    if (p->java <= 0) p->java = version_newer(p->version, "1.21.11") ? 25 : 21;
    snprintf(p->algo, sizeof p->algo, "sha256");
    p->paper = 1;
    return 1;
}

int plan_server(const char *type, const char *want, struct server_plan *p, char *why, int n)
{
    memset(p, 0, sizeof *p);
    why[0] = 0;
    if (!strcmp(type, "paper")) return plan_paper(want, p, why, n);
    if (!strcmp(want, "latest")) {
        struct version_list vl;
        if (!versions_get(0, 0, &vl, why, n)) return 0;
        return plan_vanilla(vl.latest[0] ? vl.latest : vl.id[0], p, why, n);
    }
    return plan_vanilla(want, p, why, n);
}

int get_server(const struct server_plan *p, char *why, int n)
{
    char title[80], detail[80];
    int code;
    mkdirs(HM_SERVER);
    snprintf(title, sizeof title, "Downloading %s %s", p->paper ? "Paper" : "Minecraft", p->version);
    snprintf(detail, sizeof detail, "The server, from %s", p->paper ? "PaperMC" : "Mojang");
    code = fetch(p->url, HM_SERVER "/server.jar.new", title, detail, p->size, 1);
    if (code) {
        if (code == -2) snprintf(why, n, "Cancelled.");
        else fetch_reason(code, why, n);
        return 0;
    }
    if (!checksum_ok(p->algo, HM_SERVER "/server.jar.new", p->hash)) {
        unlink(HM_SERVER "/server.jar.new");
        snprintf(why, n, "The download didn't match its checksum, so it was thrown away. Try again.");
        return 0;
    }
    if (rename(HM_SERVER "/server.jar.new", HM_SERVER "/server.jar") != 0) {
        snprintf(why, n, "The server couldn't be put in place.");
        return 0;
    }
    snprintf(hi.type, sizeof hi.type, "%s", p->paper ? "paper" : "vanilla");
    snprintf(hi.version, sizeof hi.version, "%s", p->version);
    snprintf(hi.build, sizeof hi.build, "%s", p->build);
    hi.java = p->java;
    hi_save();
    return 1;
}

/* ------------------------------------------------------------------- Java */

static const char *adoptium_arch(void)
{
    static struct utsname u;
    if (uname(&u) != 0) return "x64";
    if (!strcmp(u.machine, "x86_64") || !strcmp(u.machine, "amd64")) return "x64";
    if (!strcmp(u.machine, "aarch64") || !strcmp(u.machine, "arm64")) return "aarch64";
    if (!strncmp(u.machine, "armv7", 5) || !strncmp(u.machine, "armv8l", 6)) return "arm";
    if (!strcmp(u.machine, "riscv64")) return "riscv64";
    return u.machine;
}

const char *java_path(int major)
{
    static char path[700];
    const char *env = getenv("HOSTMC_JAVA");
    if (env && *env) return access(env, X_OK) == 0 ? env : NULL;
    snprintf(path, sizeof path, "%s/java/%d/bin/java", hm_root(), major);
    return access(path, X_OK) == 0 ? path : NULL;
}

int plan_java(int major, struct java_plan *p, char *why, int n)
{
    char url[300], cache[160], *text;
    const char *a, *pkg;
    const char *arch = adoptium_arch();
    static const char *const kinds[] = {"jre", "jdk"};
    int k;
    memset(p, 0, sizeof *p);
    p->major = major;
    for (k = 0; k < 2 && !p->url[0]; k++) {      /* a JRE if there is one, else the JDK */
        snprintf(url, sizeof url, "https://api.adoptium.net/v3/assets/latest/%d/hotspot?architecture=%s&image_type=%s&os=linux&vendor=eclipse",
                 major, arch, kinds[k]);
        snprintf(cache, sizeof cache, HM_CACHE "/temurin-%d-%s-%s.json", major, arch, kinds[k]);
        if (!(text = fetch_text(url, cache, 86400, "Looking for Java", why, n))) return 0;
        a = js_first(text);
        pkg = js_key(js_key(a, "binary"), "package");
        js_str(js_key(pkg, "link"), p->url, sizeof p->url);
        js_str(js_key(pkg, "checksum"), p->sha256, sizeof p->sha256);
        p->size = js_num(js_key(pkg, "size"));
        js_str(js_key(a, "release_name"), p->release, sizeof p->release);
        free(text);
    }
    if (!p->url[0]) {
        if (!strcmp(arch, "arm"))
            snprintf(why, n, "Java %d isn't made for 32-bit ARM, so this Minecraft version can't run here. A 64-bit system (64-bit Raspberry Pi OS) can.", major);
        else
            snprintf(why, n, "Adoptium has no Java %d for this processor (%s).", major, arch);
        return 0;
    }
    return 1;
}

int get_java(const struct java_plan *p, char *why, int n)
{
    char tarball[160], dir[160], tmp[170], title[64], check[200];
    int code;
    const char *rm_argv[] = {"rm", "-rf", tmp, NULL};
    const char *tar_argv[] = {"tar", "-xzf", tarball, "-C", tmp, "--strip-components=1", NULL};
    mkdirs(HM_JAVA);
    snprintf(tarball, sizeof tarball, HM_JAVA "/java-%d.tar.gz", p->major);
    snprintf(dir, sizeof dir, HM_JAVA "/%d", p->major);
    snprintf(tmp, sizeof tmp, HM_JAVA "/%d.new", p->major);
    snprintf(title, sizeof title, "Downloading Java %d", p->major);
    code = fetch(p->url, tarball, title, "Eclipse Temurin, from Adoptium", p->size, 1);
    if (code) {
        if (code == -2) snprintf(why, n, "Cancelled.");
        else fetch_reason(code, why, n);
        return 0;
    }
    if (!checksum_ok("sha256", tarball, p->sha256)) {
        unlink(tarball);
        snprintf(why, n, "The Java download didn't match its checksum, so it was thrown away. Try again.");
        return 0;
    }
    job_run(rm_argv, NULL, "Unpacking Java", NULL, NULL, 0, 0);
    mkdirs(tmp);
    if (job_run(tar_argv, NULL, "Unpacking Java", NULL, NULL, 0, 0) != 0) {
        job_run(rm_argv, NULL, "Unpacking Java", NULL, NULL, 0, 0);
        snprintf(why, n, "Java didn't unpack. Is the disk full?");
        return 0;
    }
    snprintf(check, sizeof check, "%s/bin/java", tmp);
    if (access(check, X_OK) != 0) {
        job_run(rm_argv, NULL, "Unpacking Java", NULL, NULL, 0, 0);
        snprintf(why, n, "The Java download had no java program in it.");
        return 0;
    }
    {
        const char *old_argv[] = {"rm", "-rf", dir, NULL};
        job_run(old_argv, NULL, "Unpacking Java", NULL, NULL, 0, 0);
    }
    if (rename(tmp, dir) != 0) {
        snprintf(why, n, "Java couldn't be put in place.");
        return 0;
    }
    unlink(tarball);
    return 1;
}
