/* Adding someone to the whitelist.
 *
 * The whitelist goes by UUID. With online mode on, a player's UUID is their Mojang account's:
 * while the server runs it looks the name up itself (whitelist add), and while it's stopped HostMC
 * asks Mojang. With online mode off, Minecraft gives each player an "offline" UUID made from
 * their name (an MD5 of "OfflinePlayer:<name>"), but the server's own whitelist add still stores
 * the UUID of the Mojang account with that name, which an offline player never matches. So then
 * HostMC writes the offline UUID into whitelist.json itself and has the server read it again. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "hostmc.h"

/* MD5 (RFC 1321), for short messages (under 120 bytes). */
static void md5(const unsigned char *msg, size_t len, unsigned char out[16])
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const int R[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
    };
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    unsigned char buf[128];
    size_t total = (len + 8) / 64 * 64 + 64, off, i;
    uint64_t bits = (uint64_t)len * 8;
    memset(buf, 0, sizeof buf);
    memcpy(buf, msg, len);
    buf[len] = 0x80;
    for (i = 0; i < 8; i++) buf[total - 8 + i] = (unsigned char)(bits >> (8 * i));
    for (off = 0; off < total; off += 64) {
        uint32_t w[16], a = h[0], b = h[1], c = h[2], d = h[3];
        for (i = 0; i < 16; i++)
            w[i] = (uint32_t)buf[off + i * 4] | (uint32_t)buf[off + i * 4 + 1] << 8 | (uint32_t)buf[off + i * 4 + 2] << 16 |
                   (uint32_t)buf[off + i * 4 + 3] << 24;
        for (i = 0; i < 64; i++) {
            uint32_t f, t;
            int g;
            if (i < 16) { f = (b & c) | (~b & d); g = (int)i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (int)(5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (int)(3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (int)(7 * i) % 16; }
            t = d;
            d = c;
            c = b;
            f = a + f + K[i] + w[g];
            b = b + ((f << R[i]) | (f >> (32 - R[i])));
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
    }
    for (i = 0; i < 16; i++) out[i] = (unsigned char)(h[i / 4] >> (8 * (i % 4)));
}

static void format_uuid(const unsigned char b[16], char out[37])
{
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

/* What Minecraft calls a player in offline mode (Java's UUID.nameUUIDFromBytes). */
void offline_uuid(const char *name, char out[37])
{
    unsigned char msg[64], d[16];
    int n = snprintf((char *)msg, sizeof msg, "OfflinePlayer:%s", name);
    md5(msg, (size_t)(n < (int)sizeof msg ? n : (int)sizeof msg - 1), d);
    d[6] = (d[6] & 0x0f) | 0x30;                 /* version 3 */
    d[8] = (d[8] & 0x3f) | 0x80;                 /* the IETF variant */
    format_uuid(d, out);
}

#define WHITELIST HM_SERVER "/whitelist.json"
#define MAX_WL 200

/* whitelist.json with name in it, for uuid (any entry with that name is replaced). */
static int write_entry(const char *name, const char *uuid)
{
    char names[MAX_WL][17], uuids[MAX_WL][40], *text = file_read(WHITELIST, NULL), *out;
    const char *e;
    int n = 0, i, ok;
    size_t len = 0, cap;
    for (e = js_first(text); e && n < MAX_WL - 1; e = js_next(e)) {
        if (!js_str(js_key(e, "name"), names[n], sizeof names[n]) || !js_str(js_key(e, "uuid"), uuids[n], sizeof uuids[n])) continue;
        if (strcasecmp(names[n], name)) n++;
    }
    free(text);
    snprintf(names[n], sizeof names[n], "%s", name);
    snprintf(uuids[n], sizeof uuids[n], "%s", uuid);
    n++;
    cap = (size_t)n * 120 + 8;
    if (!(out = malloc(cap))) return 0;
    len += (size_t)snprintf(out, cap, "[\n");
    for (i = 0; i < n && len < cap; i++) {
        char esc[40];
        int k = 0;
        const char *s;
        for (s = names[i]; *s && k < (int)sizeof esc - 2; s++) {      /* names are plain ASCII */
            if (*s == '"' || *s == '\\') esc[k++] = '\\';
            esc[k++] = *s;
        }
        esc[k] = 0;
        len += (size_t)snprintf(out + len, cap - len, "  {\n    \"uuid\": \"%s\",\n    \"name\": \"%s\"\n  }%s\n",
                                uuids[i], esc, i + 1 < n ? "," : "");
    }
    if (len < cap) len += (size_t)snprintf(out + len, cap - len, "]\n");
    if (len >= cap) { free(out); return 0; }
    mkdirs(HM_SERVER);
    ok = file_write(WHITELIST, out, (long)len, 0644);
    free(out);
    return ok;
}

int whitelist_add(const char *name, char *why, int n)
{
    char uuid[40] = "", url[160], cache[160], *text, *p, id[40] = "", real[20] = "";
    int online = 1;
    {
        char v[16];
        if (props_get("online-mode", v, sizeof v) && !strcmp(v, "false")) online = 0;
    }
    if (strlen(name) < 1 || strlen(name) > 16 || strpbrk(name, " \"\\/")) {
        snprintf(why, n, "That isn't a Minecraft name: they're up to 16 letters, numbers and underscores.");
        return 0;
    }
    if (online && sv.state == SV_RUNNING) {          /* the server asks Mojang itself */
        char cmd[64];
        snprintf(cmd, sizeof cmd, "whitelist add %s", name);
        if (sv_command(cmd)) return 1;
    }
    if (!online) {
        offline_uuid(name, uuid);
    } else {
        snprintf(url, sizeof url, "https://api.mojang.com/users/profiles/minecraft/%s", name);
        snprintf(cache, sizeof cache, HM_CACHE "/profile.json");
        unlink(cache);
        if (!(text = fetch_text(url, cache, 0, "Looking the name up with Mojang", why, n))) {
            if (strstr(why, "answered with an error")) snprintf(why, n, "Mojang has no player called %s. Check the spelling.", name);
            return 0;
        }
        js_str(js_key(text, "id"), id, sizeof id);
        js_str(js_key(text, "name"), real, sizeof real);
        free(text);
        unlink(cache);
        if (strlen(id) != 32) {
            snprintf(why, n, "Mojang has no player called %s. Check the spelling.", name);
            return 0;
        }
        for (p = id; *p; p++)
            if (!strchr("0123456789abcdefABCDEF", *p)) { snprintf(why, n, "Mojang's answer didn't make sense."); return 0; }
        snprintf(uuid, sizeof uuid, "%.8s-%.4s-%.4s-%.4s-%.12s", id, id + 8, id + 12, id + 16, id + 20);
        if (real[0]) name = real;                    /* as Mojang spells it */
    }
    if (!write_entry(name, uuid)) {
        snprintf(why, n, "whitelist.json couldn't be written.");
        return 0;
    }
    if (sv.state == SV_RUNNING) sv_command("whitelist reload");
    return 1;
}
