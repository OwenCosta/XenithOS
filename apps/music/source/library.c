/* LIBRARY: finding the music and reading what each song is.
 *
 * It looks in a "music" folder on every drive (four folders deep, so Artist/Album/CD 1 works),
 * and on a PC in ~/Music too. Each song's tags are read straight from the file: ID3 (version 2,
 * else 1) in MP3s, the Vorbis comments in FLAC and Ogg files, and the INFO list in WAVs. What it
 * has read is kept in uda:/xenith/music-library.txt by path, size and date, so next time only new
 * or changed files are opened. Songs are grouped into albums (by album and album artist, or album
 * and folder when there's no album artist, which keeps compilations together) and artists.
 * A cover.jpg (or folder.jpg, front.jpg...) next to the songs is the album's cover.
 */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "img.h"
#include "music.h"
#include "platform.h"
#include "ui.h"

#define CACHE_PATH "uda:/xenith/music-library.txt"
#define MAX_DEPTH 4

struct song *songs;
struct album *albums;
struct artist *artists;
struct folder *folders;
int nsongs, nalbums, nartists, nfolders;
int *album_songs, *artist_songs, *songs_by_title;
int *albums_by_name, *artists_by_name;

/* ------------------------------------------------------------------- text */

static void put(char *out, int n, int *o, const char *s)
{
    while (*s && *o < n - 1) out[(*o)++] = *s++;
}

/* A letter as the nearest plain one: XenithOS's fonts have ASCII only. */
static void put_cp(char *out, int n, int *o, unsigned cp)
{
    static const char *const latin1[64] = {
        "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
        "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
        "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y",
    };
    static const char ext_a[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOo"
                                "RrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
    char one[2] = {0, 0};
    if (cp < 32 || cp == 127) { put(out, n, o, " "); return; }
    if (cp < 127) { one[0] = (char)cp; put(out, n, o, one); return; }
    if (cp >= 0xC0 && cp <= 0xFF) { put(out, n, o, latin1[cp - 0xC0]); return; }
    if (cp == 0x152) { put(out, n, o, "OE"); return; }
    if (cp == 0x153) { put(out, n, o, "oe"); return; }
    if (cp >= 0x100 && cp <= 0x17F) { one[0] = ext_a[cp - 0x100]; put(out, n, o, one); return; }
    switch (cp) {
    case 0xA0: put(out, n, o, " "); return;
    case 0xA1: put(out, n, o, "!"); return;
    case 0xBF: put(out, n, o, "?"); return;
    case 0xAB: case 0xBB: case 0x201C: case 0x201D: case 0x201E: put(out, n, o, "\""); return;
    case 0xB4: case 0x2018: case 0x2019: case 0x201A: case 0x2032: put(out, n, o, "'"); return;
    case 0xA9: put(out, n, o, "(c)"); return;
    case 0xAE: put(out, n, o, "(R)"); return;
    case 0xB7: case 0x2022: case 0x30FB: put(out, n, o, "."); return;
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: put(out, n, o, "-"); return;
    case 0x2026: put(out, n, o, "..."); return;
    case 0x2122: put(out, n, o, "TM"); return;
    case 0x266A: case 0x266B: return;            /* musical notes in titles */
    case 0xFEFF: return;                         /* a byte order mark left in the text */
    }
    put(out, n, o, "?");
}

/* Bytes in an ID3 encoding (0 Latin-1, 1 UTF-16 with its byte order mark, 2 UTF-16 big-endian,
 * 3 UTF-8) as plain text, stopping at the first NUL. */
static void text_from(int enc, const unsigned char *p, long len, char *out, int n)
{
    int o = 0, be = 1;
    long i = 0;
    out[0] = 0;
    if (enc == 1 && len >= 2) {
        if (p[0] == 0xFF && p[1] == 0xFE) { be = 0; i = 2; }
        else if (p[0] == 0xFE && p[1] == 0xFF) i = 2;
    }
    while (i < len && o < n - 1) {
        unsigned cp;
        if (enc == 1 || enc == 2) {
            if (i + 1 >= len) break;
            cp = be ? (unsigned)(p[i] << 8 | p[i + 1]) : (unsigned)(p[i + 1] << 8 | p[i]);
            i += 2;
            if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < len) {     /* a surrogate pair */
                unsigned lo = be ? (unsigned)(p[i] << 8 | p[i + 1]) : (unsigned)(p[i + 1] << 8 | p[i]);
                i += 2;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
        } else if (enc == 3) {
            unsigned c = p[i++];
            int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            cp = more == 3 ? c & 7 : more == 2 ? c & 15 : more == 1 ? c & 31 : c;
            while (more-- > 0 && i < len && (p[i] & 0xC0) == 0x80) cp = cp << 6 | (p[i++] & 0x3F);
        } else {
            cp = p[i++];
        }
        if (!cp) break;
        put_cp(out, n, &o, cp);
    }
    out[o] = 0;
}

/* Tidy: no runs of spaces, none at either end. */
static void tidy(char *s)
{
    char *r = s, *w = s;
    int space = 1;
    for (; *r; r++) {
        if (*r == ' ' || *r == '\t') { if (!space) *w++ = ' '; space = 1; }
        else { *w++ = *r; space = 0; }
    }
    while (w > s && w[-1] == ' ') w--;
    *w = 0;
}

void text_ascii(char *out, int n, const char *utf8)
{
    text_from(3, (const unsigned char *)utf8, (long)strlen(utf8), out, n);
    tidy(out);
}

/* Words with something in them (not only question marks for letters it can't show). */
static int readable(const char *s)
{
    int good = 0, bad = 0;
    for (; *s; s++) {
        if (isalnum((unsigned char)*s)) good++;
        else if (*s == '?') bad++;
    }
    return good > 0 && good >= bad;
}

static void set_field(char *field, int n, const char *value)
{
    char t[128];
    snprintf(t, sizeof t, "%s", value);
    tidy(t);
    if (readable(t)) snprintf(field, n, "%s", t);
}

/* ------------------------------------------------------------------- tags */

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t be24(const unsigned char *p) { return (uint32_t)p[0] << 16 | p[1] << 8 | p[2]; }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }
static uint32_t syncsafe(const unsigned char *p) { return (uint32_t)(p[0] & 127) << 21 | (p[1] & 127) << 14 | (p[2] & 127) << 7 | (p[3] & 127); }

enum { F_TITLE, F_ARTIST, F_ALBUM, F_ALBUM_ARTIST, F_TRACK, F_DISC, F_PICTURE, F_NONE };

static void store(struct song *s, int field, const char *text)
{
    switch (field) {
    case F_TITLE: set_field(s->title, sizeof s->title, text); break;
    case F_ARTIST: set_field(s->artist, sizeof s->artist, text); break;
    case F_ALBUM: set_field(s->album, sizeof s->album, text); break;
    case F_ALBUM_ARTIST: set_field(s->album_artist, sizeof s->album_artist, text); break;
    case F_TRACK: s->track = (int16_t)atoi(text); break;
    case F_DISC: s->disc = (int16_t)atoi(text); break;
    }
}

static int id3_field(const char *id, int ver)
{
    static const char *const v2[] = {"TT2", "TP1", "TAL", "TP2", "TRK", "TPA", "PIC"};
    static const char *const v3[] = {"TIT2", "TPE1", "TALB", "TPE2", "TRCK", "TPOS", "APIC"};
    int i;
    for (i = 0; i < 7; i++)
        if (!strcmp(id, ver == 2 ? v2[i] : v3[i])) return i;
    return F_NONE;
}

/* ID3 version 2 at the start of the file. Returns where the audio starts (0 with no tag). */
static long id3v2(FILE *f, struct song *s)
{
    unsigned char h[10], fh[10], data[1024];
    long end, pos;
    int ver;
    if (fread(h, 1, 10, f) != 10 || memcmp(h, "ID3", 3)) return 0;
    ver = h[3];
    end = 10 + (long)syncsafe(h + 6) + ((h[5] & 0x10) ? 10 : 0);
    if (ver < 2 || ver > 4 || (h[5] & 0x80 && ver < 4)) return end;   /* unknown, or unsynchronised as a whole */
    pos = 10;
    if ((h[5] & 0x40) && ver >= 3) {                    /* an extended header first */
        unsigned char e[4];
        if (fread(e, 1, 4, f) != 4) return end;
        pos = ver == 4 ? 10 + (long)syncsafe(e) : 14 + (long)be32(e);
    }
    while (pos + (ver == 2 ? 6 : 10) <= end) {
        char id[5];
        long size, hl = ver == 2 ? 6 : 10;
        int field, fmt = 0;
        if (fseek(f, pos, SEEK_SET) || fread(fh, 1, (size_t)hl, f) != (size_t)hl) break;
        memcpy(id, fh, ver == 2 ? 3 : 4);
        id[ver == 2 ? 3 : 4] = 0;
        size = ver == 2 ? (long)be24(fh + 3) : ver == 4 ? (long)syncsafe(fh + 4) : (long)be32(fh + 4);
        if (!isalnum((unsigned char)id[0]) || size <= 0 || pos + hl + size > end) break;   /* padding */
        if (ver >= 3) fmt = ver == 4 ? fh[9] : (fh[9] & 0xC0 ? 4 : 0);   /* v2.4: 1 length, 2 unsynced, 4/8 packed */
        field = id3_field(id, ver);
        if (field == F_PICTURE) s->has_art = 1;
        else if (field != F_NONE && size < (long)sizeof data && !(fmt & 0x0C)) {
            long len = size, k, w;
            const unsigned char *d = data;
            if (fread(data, 1, (size_t)size, f) == (size_t)size) {
                if (fmt & 2)                                   /* undo unsynchronisation: FF 00 is FF */
                    for (k = w = 0; k < len; k++) { data[w++] = data[k]; if (data[k] == 0xFF && k + 1 < len && !data[k + 1]) k++; }
                if (fmt & 2) len = w;
                if (fmt & 1) { d += 4; len -= 4; }
                if (len > 1) {
                    char t[128];
                    text_from(d[0], d + 1, len - 1, t, sizeof t);
                    store(s, field, t);
                }
            }
        }
        pos += hl + size;
    }
    return end;
}

/* ID3 version 1: the last 128 bytes. Only fills what version 2 didn't. */
static void id3v1(FILE *f, struct song *s)
{
    unsigned char t[128];
    char v[64];
    if (fseek(f, -128, SEEK_END) || fread(t, 1, 128, f) != 128 || memcmp(t, "TAG", 3)) return;
    if (!s->title[0]) { text_from(0, t + 3, 30, v, sizeof v); store(s, F_TITLE, v); }
    if (!s->artist[0]) { text_from(0, t + 33, 30, v, sizeof v); store(s, F_ARTIST, v); }
    if (!s->album[0]) { text_from(0, t + 63, 30, v, sizeof v); store(s, F_ALBUM, v); }
    if (!s->track && !t[125] && t[126]) s->track = t[126];
}

/* Vorbis comments (FLAC and Ogg): KEY=value, UTF-8, little-endian lengths. */
static void vorbis_comments(const unsigned char *p, long len, struct song *s)
{
    static const struct { const char *key; int field; } keys[] = {
        {"TITLE", F_TITLE}, {"ARTIST", F_ARTIST}, {"ALBUM", F_ALBUM}, {"ALBUMARTIST", F_ALBUM_ARTIST},
        {"ALBUM ARTIST", F_ALBUM_ARTIST}, {"TRACKNUMBER", F_TRACK}, {"DISCNUMBER", F_DISC},
        {"METADATA_BLOCK_PICTURE", F_PICTURE},
    };
    const unsigned char *end = p + len;
    uint32_t n, i, l;
    if (len < 8) return;
    l = le32(p);
    if (l > (uint32_t)(len - 8)) return;
    p += 4 + l;
    n = le32(p);
    p += 4;
    for (i = 0; i < n && p + 4 <= end; i++) {
        const unsigned char *eq;
        unsigned k;
        l = le32(p);
        p += 4;
        if (l > (uint32_t)(end - p)) break;
        eq = memchr(p, '=', l);
        for (k = 0; eq && k < sizeof keys / sizeof keys[0]; k++)
            if ((size_t)(eq - p) == strlen(keys[k].key) && !strncasecmp((const char *)p, keys[k].key, (size_t)(eq - p))) {
                char t[128];
                if (keys[k].field == F_PICTURE) { s->has_art = 2; break; }
                text_from(3, eq + 1, (long)(p + l - eq - 1), t, sizeof t);
                if (keys[k].field == F_ALBUM_ARTIST && s->album_artist[0]) break;
                store(s, keys[k].field, t);
                break;
            }
        p += l;
    }
}

static void flac_tags(FILE *f, struct song *s)
{
    unsigned char h[4];
    long start = id3v2(f, s);                    /* some taggers put ID3 before it */
    int k;
    if (fseek(f, start, SEEK_SET) || fread(h, 1, 4, f) != 4 || memcmp(h, "fLaC", 4)) return;
    for (k = 0; k < 64; k++) {
        long len;
        int last, type;
        if (fread(h, 1, 4, f) != 4) return;
        last = h[0] & 0x80;
        type = h[0] & 0x7F;
        len = (long)be24(h + 1);
        if (type == 4 && len < 512 * 1024) {
            unsigned char *b = malloc((size_t)len);
            if (!b) return;
            if (fread(b, 1, (size_t)len, f) == (size_t)len) vorbis_comments(b, len, s);
            free(b);
        } else {
            if (type == 6) s->has_art = 1;
            if (fseek(f, len, SEEK_CUR)) return;
        }
        if (last) return;
    }
}

/* Ogg Vorbis: the comments are the second packet, which can run over a few pages. */
static void ogg_tags(FILE *f, struct song *s)
{
    unsigned char h[27], seg[255], *pkt = NULL;
    long plen = 0, cap = 0;
    int packet = 0, pages, i;
    for (pages = 0; pages < 64 && packet < 2; pages++) {
        if (fread(h, 1, 27, f) != 27 || memcmp(h, "OggS", 4)) break;
        if (fread(seg, 1, h[26], f) != h[26]) break;
        for (i = 0; i < h[26] && packet < 2; i++) {
            if (packet == 1) {
                if (plen + seg[i] > cap) {
                    unsigned char *m;
                    cap = (plen + seg[i]) * 2 + 4096;
                    if (cap > 1024 * 1024 || !(m = realloc(pkt, (size_t)cap))) { packet = 3; break; }
                    pkt = m;
                }
                if (fread(pkt + plen, 1, seg[i], f) != seg[i]) { packet = 3; break; }
                plen += seg[i];
            } else if (fseek(f, seg[i], SEEK_CUR)) { packet = 3; break; }
            if (seg[i] < 255) packet++;            /* that packet ends here */
        }
    }
    if (packet == 2 && plen > 7 && !memcmp(pkt, "\x03vorbis", 7)) vorbis_comments(pkt + 7, plen - 7, s);
    free(pkt);
}

static void wav_tags(FILE *f, struct song *s)
{
    unsigned char h[12];
    long pos = 12;
    int k;
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return;
    for (k = 0; k < 64; k++) {
        long len;
        if (fseek(f, pos, SEEK_SET) || fread(h, 1, 8, f) != 8) return;
        len = (long)le32(h + 4);
        if (!memcmp(h, "LIST", 4) && len >= 4 && len < 64 * 1024) {
            unsigned char *b = malloc((size_t)len);
            if (b && fread(b, 1, (size_t)len, f) == (size_t)len && !memcmp(b, "INFO", 4)) {
                long p = 4;
                while (p + 8 <= len) {
                    long l = (long)le32(b + p + 4);
                    char t[128];
                    int field = !memcmp(b + p, "INAM", 4) ? F_TITLE : !memcmp(b + p, "IART", 4) ? F_ARTIST
                              : !memcmp(b + p, "IPRD", 4) ? F_ALBUM : !memcmp(b + p, "ITRK", 4) || !memcmp(b + p, "IPRT", 4) ? F_TRACK : F_NONE;
                    if (l < 0 || p + 8 + l > len) break;
                    if (field != F_NONE) {
                        text_from(3, b + p + 8, l, t, sizeof t);
                        store(s, field, t);
                    }
                    p += 8 + l + (l & 1);
                }
            }
            free(b);
        }
        pos += 8 + len + (len & 1);
    }
}

static void read_tags(struct song *s)
{
    FILE *f = fopen(s->path, "rb");
    if (!f) return;
    switch (s->fmt) {
    case FMT_MP3: id3v2(f, s); id3v1(f, s); break;
    case FMT_FLAC: flac_tags(f, s); break;
    case FMT_OGG: ogg_tags(f, s); break;
    case FMT_WAV: wav_tags(f, s); break;
    }
    fclose(f);
}

/* ------------------------------------------------------------------ cache */

static struct song *cache;
static int ncache, *cache_hash, cache_slots;

static uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

/* path\tsize\tmtime\tformat\ttrack\tdisc\tart\ttitle\tartist\talbum\talbum artist */
static void cache_load(void)
{
    char line[700];
    FILE *f = fopen(CACHE_PATH, "r");
    int i;
    ncache = 0;
    if (!f) return;
    cache_slots = 16384;
    cache = calloc(MAX_SONGS, sizeof *cache);
    cache_hash = malloc(sizeof(int) * cache_slots);
    if (!cache || !cache_hash) { fclose(f); free(cache); free(cache_hash); cache = NULL; cache_hash = NULL; return; }
    for (i = 0; i < cache_slots; i++) cache_hash[i] = -1;
    while (fgets(line, sizeof line, f) && ncache < MAX_SONGS) {
        char *fld[11], *p = line;
        int k;
        struct song *c = &cache[ncache];
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#') continue;
        for (k = 0; k < 11; k++) {
            fld[k] = p;
            p = strchr(p, '\t');
            if (!p) break;
            *p++ = 0;
        }
        if (k < 10) continue;
        memset(c, 0, sizeof *c);
        snprintf(c->path, sizeof c->path, "%s", fld[0]);
        c->size = atol(fld[1]);
        c->mtime = atol(fld[2]);
        c->fmt = (uint8_t)atoi(fld[3]);
        c->track = (int16_t)atoi(fld[4]);
        c->disc = (int16_t)atoi(fld[5]);
        c->has_art = (uint8_t)atoi(fld[6]);
        snprintf(c->title, sizeof c->title, "%s", fld[7]);
        snprintf(c->artist, sizeof c->artist, "%s", fld[8]);
        snprintf(c->album, sizeof c->album, "%s", fld[9]);
        snprintf(c->album_artist, sizeof c->album_artist, "%s", fld[10]);
        for (k = (int)(hash_str(c->path) & (uint32_t)(cache_slots - 1)); cache_hash[k] >= 0; k = (k + 1) & (cache_slots - 1))
            ;
        cache_hash[k] = ncache++;
    }
    fclose(f);
}

static const struct song *cache_find(const char *path)
{
    int k;
    if (!cache || !ncache) return NULL;
    for (k = (int)(hash_str(path) & (uint32_t)(cache_slots - 1)); cache_hash[k] >= 0; k = (k + 1) & (cache_slots - 1))
        if (!strcmp(cache[cache_hash[k]].path, path)) return &cache[cache_hash[k]];
    return NULL;
}

static void cache_save(void)
{
    FILE *f;
    int i;
    mkdir("uda:/xenith", 0777);
    if (!(f = fopen(CACHE_PATH, "w"))) return;
    fprintf(f, "# Written by Music: what it has read from each song, so it doesn't open them every time.\n");
    for (i = 0; i < nsongs; i++) {
        const struct song *s = &songs[i];
        fprintf(f, "%s\t%ld\t%ld\t%d\t%d\t%d\t%d\t%s\t%s\t%s\t%s\n", s->path, s->size, s->mtime, s->fmt, s->track,
                s->disc, s->has_art, s->title, s->artist, s->album, s->album_artist);
    }
    fclose(f);
}

static void cache_free(void)
{
    free(cache);
    free(cache_hash);
    cache = NULL;
    cache_hash = NULL;
    ncache = 0;
}

/* -------------------------------------------------------------- scanning */

static const char *const exts[] = {".mp3", ".ogg", ".oga", ".flac", ".wav"};
static const int ext_fmt[] = {FMT_MP3, FMT_OGG, FMT_OGG, FMT_FLAC, FMT_WAV};

static int music_fmt(const char *name)
{
    const char *dot = strrchr(name, '.');
    int i;
    for (i = 0; dot && i < (int)(sizeof exts / sizeof exts[0]); i++)
        if (!strcasecmp(dot, exts[i])) return ext_fmt[i];
    return -1;
}

/* How good a cover a picture's name makes: cover.jpg best, an AlbumArt file from Windows last. */
static int cover_rank(const char *name)
{
    static const char *const best[] = {"cover", "folder", "front", "album"};
    const char *dot = strrchr(name, '.');
    int i, len;
    if (!dot || (strcasecmp(dot, ".jpg") && strcasecmp(dot, ".jpeg") && strcasecmp(dot, ".png"))) return 0;
    len = (int)(dot - name);
    for (i = 0; i < 4; i++)
        if (len == (int)strlen(best[i]) && !strncasecmp(name, best[i], (size_t)len)) return 9 - i;
    if (!strncasecmp(name, "albumart", 8)) {
        const char *p;
        for (p = name; *p; p++)
            if (!strncasecmp(p, "large", 5)) return 3;
        return 2;
    }
    return 0;
}

static int is_dir(const char *path, const struct dirent *de)
{
    struct stat s;
#ifdef DT_DIR
    if (de->d_type == DT_DIR) return 1;
    if (de->d_type == DT_REG) return 0;
#endif
    return stat(path, &s) == 0 && S_ISDIR(s.st_mode);
}

static struct { char path[240]; int depth; } stack[96];
static int nstack, folder_rank[MAX_FOLDERS];
static DIR *dir;
static int dir_depth, dir_folder;
static char roots[4][240];
static int nroots;

int library_alloc(void)
{
    songs = calloc(MAX_SONGS, sizeof *songs);
    albums = calloc(MAX_ALBUMS, sizeof *albums);
    artists = calloc(MAX_ARTISTS, sizeof *artists);
    folders = calloc(MAX_FOLDERS, sizeof *folders);
    album_songs = malloc(sizeof(int) * MAX_SONGS);
    artist_songs = malloc(sizeof(int) * MAX_SONGS);
    songs_by_title = malloc(sizeof(int) * MAX_SONGS);
    albums_by_name = malloc(sizeof(int) * MAX_ALBUMS);
    artists_by_name = malloc(sizeof(int) * MAX_ARTISTS);
    return songs && albums && artists && folders && album_songs && artist_songs && songs_by_title && albums_by_name && artists_by_name;
}

static void push(const char *path, int depth)
{
    if (nstack < (int)(sizeof stack / sizeof stack[0])) {
        snprintf(stack[nstack].path, sizeof stack[nstack].path, "%s", path);
        stack[nstack++].depth = depth;
    }
}

const char *music_where(void)
{
    return plat_kind() == PLAT_PC ? "Put songs in your Music folder, or a folder called music in XenithOS's data."
                                  : "Put MP3, OGG, FLAC or WAV songs in a folder called music on your USB stick.";
}

void library_scan_begin(void)
{
    const char *drive;
    char root[240];
    int h = -1;
    library_free_thumbs();
    nsongs = nalbums = nartists = nfolders = nstack = nroots = 0;
    if (dir) { closedir(dir); dir = NULL; }
    cache_load();
    /* "music" and "Music" are the same folder on a USB stick's file system: one of them */
    while ((h = plat_bdev_next(h, &drive)) >= 0) {
        struct stat s;
        snprintf(root, sizeof root, "%s:/music", drive);
        if (stat(root, &s) != 0) snprintf(root, sizeof root, "%s:/Music", drive);
        push(root, 0);
        if (nroots < 4) snprintf(roots[nroots++], sizeof roots[0], "%s", root);
    }
#ifdef XENITH_LINUX
    if (getenv("HOME")) {
        snprintf(root, sizeof root, "%s/Music", getenv("HOME"));
        push(root, 0);
        if (nroots < 4) snprintf(roots[nroots++], sizeof roots[0], "%s", root);
    }
#endif
}

static int is_root(const char *path)
{
    int i;
    for (i = 0; i < nroots; i++)
        if (!strcmp(path, roots[i])) return 1;
    return 0;
}

static void add_file(const char *path, const char *name, int fmt)
{
    struct stat st;
    struct song *s;
    const struct song *c;
    if (nsongs >= MAX_SONGS || stat(path, &st) != 0 || st.st_size <= 0 || strlen(path) >= sizeof s->path) return;
    s = &songs[nsongs];
    c = cache_find(path);
    if (c && c->size == (long)st.st_size && c->mtime == (long)st.st_mtime && c->fmt == fmt) {
        *s = *c;
    } else {
        memset(s, 0, sizeof *s);
        snprintf(s->path, sizeof s->path, "%s", path);
        s->size = (long)st.st_size;
        s->mtime = (long)st.st_mtime;
        s->fmt = (uint8_t)fmt;
        read_tags(s);
    }
    if (!s->title[0]) {                          /* no title: the file's name, without its ending */
        char t[128], *dot;
        text_ascii(t, sizeof t, name);
        if ((dot = strrchr(t, '.'))) *dot = 0;
        snprintf(s->title, sizeof s->title, "%s", t[0] ? t : "Untitled");
    }
    s->folder = dir_folder;
    nsongs++;
}

/* ------------------------------------------------------------ grouping */

/* Names in order as people expect: no case, and "The Beatles" under B. */
static const char *sort_key(const char *s)
{
    return !strncasecmp(s, "the ", 4) && s[4] ? s + 4 : s;
}

static int by_title(const void *a, const void *b)
{
    const struct song *x = &songs[*(const int *)a], *y = &songs[*(const int *)b];
    int c = strcasecmp(sort_key(x->title), sort_key(y->title));
    return c ? c : strcasecmp(x->artist, y->artist);
}

static int in_album(const void *a, const void *b)
{
    const struct song *x = &songs[*(const int *)a], *y = &songs[*(const int *)b];
    if (x->album_i != y->album_i) return x->album_i - y->album_i;
    if (x->disc != y->disc) return x->disc - y->disc;
    if (x->track != y->track) return (x->track ? x->track : 9999) - (y->track ? y->track : 9999);
    return strcasecmp(x->path, y->path);
}

static int album_order(const void *a, const void *b)
{
    const struct album *x = &albums[*(const int *)a], *y = &albums[*(const int *)b];
    int c = strcasecmp(sort_key(x->name), sort_key(y->name));
    return c ? c : strcasecmp(x->artist, y->artist);
}

static int artist_order(const void *a, const void *b)
{
    return strcasecmp(sort_key(artists[*(const int *)a].name), sort_key(artists[*(const int *)b].name));
}

/* The artist's songs: by album (in album order), then track. */
static int in_artist(const void *a, const void *b)
{
    const struct song *x = &songs[*(const int *)a], *y = &songs[*(const int *)b];
    if (x->artist_i != y->artist_i) return x->artist_i - y->artist_i;
    if (x->album_i != y->album_i) {
        int c = strcasecmp(sort_key(albums[x->album_i].name), sort_key(albums[y->album_i].name));
        if (c) return c;
        return x->album_i - y->album_i;
    }
    return in_album(a, b);
}

static const char *folder_name(int f)
{
    const char *p = strrchr(folders[f].path, '/');
    return p ? p + 1 : folders[f].path;
}

/* Finding an album or an artist by name without going through them all (a library of thousands
 * of songs, on the Xbox): open addressing on a lower-case key. */
#define GROUP_SLOTS 8192
static int *group_slots;
static int *album_artist_of;        /* a song in the same album and folder with an album artist, or -1 */

static uint32_t hash_key(const char *a, const char *b, int folder)
{
    uint32_t h = 2166136261u;
    for (; *a; a++) h = (h ^ (unsigned char)tolower((unsigned char)*a)) * 16777619u;
    h = (h ^ 1u) * 16777619u;
    for (; b && *b; b++) h = (h ^ (unsigned char)tolower((unsigned char)*b)) * 16777619u;
    return (h ^ (uint32_t)folder) * 16777619u;
}

/* An album is a name and its album artist, or (without an album artist) a name and a folder. */
static int album_matches(int k, const char *album, const char *aa, int folder)
{
    if (strcasecmp(albums[k].name, album) || albums[k].by_artist != (aa[0] != 0)) return 0;
    return aa[0] ? !strcasecmp(albums[k].artist, aa) : albums[k].folder == folder;
}

static const char *album_of(const struct song *s)
{
    return s->album[0] ? s->album : is_root(folders[s->folder].path) ? "Other songs" : folder_name(s->folder);
}

static void group(void)
{
    int i, k, *aslot, *rslot;
    nalbums = nartists = 0;
    if (!group_slots && !(group_slots = malloc(sizeof(int) * GROUP_SLOTS * 3))) return;
    if (!album_artist_of && !(album_artist_of = malloc(sizeof(int) * MAX_SONGS))) return;
    aslot = group_slots;
    rslot = group_slots + GROUP_SLOTS;
    for (i = 0; i < GROUP_SLOTS * 3; i++) group_slots[i] = -1;
    for (i = 0; i < nsongs; i++) album_artist_of[i] = -1;
    /* A song without an album artist takes the one its album's other songs in the same folder
     * have (a WAV can't say it, and taggers miss songs), so they stay one album. */
    {
        int *fslot = group_slots + 2 * GROUP_SLOTS;
        for (i = 0; i < nsongs; i++) {
            const struct song *s = &songs[i];
            if (!s->album_artist[0]) continue;
            for (k = (int)(hash_key(album_of(s), NULL, s->folder) & (GROUP_SLOTS - 1)); fslot[k] >= 0; k = (k + 1) & (GROUP_SLOTS - 1))
                if (songs[fslot[k]].folder == s->folder && !strcasecmp(album_of(&songs[fslot[k]]), album_of(s))) break;
            if (fslot[k] < 0) fslot[k] = i;
        }
        for (i = 0; i < nsongs; i++) {
            struct song *s = &songs[i];
            if (s->album_artist[0]) continue;
            for (k = (int)(hash_key(album_of(s), NULL, s->folder) & (GROUP_SLOTS - 1)); fslot[k] >= 0; k = (k + 1) & (GROUP_SLOTS - 1))
                if (songs[fslot[k]].folder == s->folder && !strcasecmp(album_of(&songs[fslot[k]]), album_of(s))) {
                    album_artist_of[i] = fslot[k];
                    break;
                }
        }
    }
    for (i = 0; i < nsongs; i++) {
        struct song *s = &songs[i];
        const char *album = album_of(s);
        const char *aa = s->album_artist[0] ? s->album_artist : album_artist_of[i] >= 0 ? songs[album_artist_of[i]].album_artist : "";
        const char *who = aa[0] ? aa : s->artist[0] ? s->artist : "Unknown artist";
        uint32_t h = aa[0] ? hash_key(album, aa, -1) : hash_key(album, NULL, s->folder);
        for (k = (int)(h & (GROUP_SLOTS - 1)); aslot[k] >= 0 && !album_matches(aslot[k], album, aa, s->folder); k = (k + 1) & (GROUP_SLOTS - 1))
            ;
        if (aslot[k] >= 0) {
            k = aslot[k];
            if (!aa[0] && strcasecmp(albums[k].artist, who) && strcmp(albums[k].artist, "Various artists"))
                snprintf(albums[k].artist, sizeof albums[k].artist, "Various artists");
        } else if (nalbums == MAX_ALBUMS) {
            k = nalbums - 1;
        } else {
            aslot[k] = nalbums;
            k = nalbums++;
            memset(&albums[k], 0, sizeof albums[k]);
            snprintf(albums[k].name, sizeof albums[k].name, "%s", album);
            snprintf(albums[k].artist, sizeof albums[k].artist, "%s", who);
            albums[k].by_artist = aa[0] != 0;
            albums[k].folder = s->folder;
            albums[k].art_song = -1;
        }
        s->album_i = k;
        albums[k].n++;
        if (albums[k].art_song < 0 && s->has_art == 1) albums[k].art_song = i;
        /* artists: by the album artist when there is one */
        h = hash_key(who, NULL, -2);
        for (k = (int)(h & (GROUP_SLOTS - 1)); rslot[k] >= 0 && strcasecmp(artists[rslot[k]].name, who); k = (k + 1) & (GROUP_SLOTS - 1))
            ;
        if (rslot[k] >= 0) k = rslot[k];
        else if (nartists == MAX_ARTISTS) k = nartists - 1;
        else {
            rslot[k] = nartists;
            k = nartists++;
            memset(&artists[k], 0, sizeof artists[k]);
            snprintf(artists[k].name, sizeof artists[k].name, "%s", who);
        }
        s->artist_i = k;
        artists[k].n++;
    }
    /* each album's songs together, in track order; the same for artists */
    for (i = 0; i < nsongs; i++) album_songs[i] = artist_songs[i] = songs_by_title[i] = i;
    qsort(album_songs, (size_t)nsongs, sizeof(int), in_album);
    for (i = 0, k = -1; i < nsongs; i++)
        if (songs[album_songs[i]].album_i != k) { k = songs[album_songs[i]].album_i; albums[k].at = i; }
    qsort(artist_songs, (size_t)nsongs, sizeof(int), in_artist);
    for (i = 0, k = -1; i < nsongs; i++) {
        const struct song *s = &songs[artist_songs[i]];
        if (s->artist_i != k) { k = s->artist_i; artists[k].at = i; }
    }
    for (i = 0; i < nartists; i++) {                 /* how many albums each */
        int j, last = -1;
        for (j = 0; j < artists[i].n; j++) {
            int a = songs[artist_songs[artists[i].at + j]].album_i;
            if (a != last) { artists[i].albums++; last = a; }
        }
    }
    qsort(songs_by_title, (size_t)nsongs, sizeof(int), by_title);
    for (i = 0; i < nalbums; i++) albums_by_name[i] = i;
    qsort(albums_by_name, (size_t)nalbums, sizeof(int), album_order);
    for (i = 0; i < nartists; i++) artists_by_name[i] = i;
    qsort(artists_by_name, (size_t)nartists, sizeof(int), artist_order);
}

int library_scan_step(int budget_ms, int *found)
{
    uint64_t until = ui_ms() + (uint64_t)budget_ms;
    for (;;) {
        struct dirent *de;
        if (!dir) {
            if (!nstack) {
                group();
                cache_save();
                cache_free();
                if (found) *found = nsongs;
                return 1;
            }
            nstack--;
            if (!(dir = opendir(stack[nstack].path))) continue;
            dir_depth = stack[nstack].depth;
            dir_folder = nfolders < MAX_FOLDERS ? nfolders++ : MAX_FOLDERS - 1;
            snprintf(folders[dir_folder].path, sizeof folders[dir_folder].path, "%s", stack[nstack].path);
            folders[dir_folder].cover[0] = 0;
            folder_rank[dir_folder] = 0;
        }
        while ((de = readdir(dir))) {
            char path[480];
            int fmt, rank;
            if (de->d_name[0] == '.') continue;
            snprintf(path, sizeof path, "%s/%s", folders[dir_folder].path, de->d_name);
            if (strlen(path) >= sizeof songs[0].path) continue;
            if (is_dir(path, de)) {
                if (dir_depth < MAX_DEPTH) push(path, dir_depth + 1);
            } else if ((fmt = music_fmt(de->d_name)) >= 0) {
                add_file(path, de->d_name, fmt);
            } else if ((rank = cover_rank(de->d_name)) > folder_rank[dir_folder]) {
                folder_rank[dir_folder] = rank;
                snprintf(folders[dir_folder].cover, sizeof folders[dir_folder].cover, "%s", path);
            }
            if (ui_ms() >= until) break;
        }
        if (!de) {
            closedir(dir);
            dir = NULL;
        }
        if (found) *found = nsongs;
        if (ui_ms() >= until) return 0;
    }
}

void library_free_thumbs(void)
{
    int i;
    for (i = 0; albums && i < nalbums; i++) {
        img_free(&albums[i].thumb);
        albums[i].thumb_tried = albums[i].thumb_none = albums[i].thumb_size = 0;
    }
}
