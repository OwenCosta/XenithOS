/* ART: album covers.
 *
 * An album's cover is the picture next to its songs (cover.jpg, folder.jpg...), else the one inside
 * a song (ID3's APIC frame, FLAC's PICTURE block). Covers are made once at ART_SIZE and kept in
 * uda:/xenith/thumbs like Photos' thumbnails, and drawn at any size from that. One is made a frame,
 * for albums on screen, so scrolling never stops; ones not drawn for a while are let go. An album
 * without one gets a flat made-up cover: its colour from its name, a record and its first letter.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gfx.h"
#include "img.h"
#include "music.h"
#include "picture.h"
#include "theme.h"

#define ART_SIZE 360
#define KEEP 48                      /* covers kept in memory before old ones go */

static int wanted[64], nwanted;
static unsigned frame, *drawn_at;

void art_frame_begin(void)
{
    nwanted = 0;
    frame++;
}

static uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

/* ---------------------------------------------------------- made-up ones */

void art_placeholder(const char *name, int x, int y, int size, float radius)
{
    static const uint32_t tones[8] = {
        RGB(52, 40, 74), RGB(30, 58, 82), RGB(28, 70, 62), RGB(84, 48, 36),
        RGB(78, 34, 52), RGB(40, 52, 30), RGB(66, 60, 30), RGB(44, 46, 58),
    };
    uint32_t h = hash_str(name ? name : ""), bg = tones[h % 8], lit = gfx_mix(bg, 0xFFFFFF, 40);
    float cx = x + size * 0.64f, cy = y + size * 0.64f, r = size * 0.36f;
    char letter[2] = {0, 0};
    int font = size >= 200 ? FONT_BIG : size >= 120 ? FONT_HEAD : size >= 70 ? FONT_TITLE : FONT_BODY_SB, clip[4];
    const char *p;
    for (p = name ? name : ""; *p && !isalnum((unsigned char)*p); p++)
        ;
    letter[0] = *p ? (char)toupper((unsigned char)*p) : '#';
    gfx_get_clip(clip);
    gfx_clip_intersect(x, y, size, size);
    gfx_fill_rrect((float)x, (float)y, (float)size, (float)size, radius, bg, 255);
    /* a record, half out of the corner */
    gfx_circle(cx, cy, r, lit, 255);
    gfx_ring(cx, cy, r * 0.70f, size * 0.008f + 1, bg, 120);
    gfx_circle(cx, cy, r * 0.30f, bg, 255);
    gfx_circle(cx, cy, r * 0.07f, lit, 255);
    gfx_str(font, x + size / 9, y + size / 12, gfx_mix(bg, 0xFFFFFF, 210), letter);
    gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
}

/* ------------------------------------------------------- pictures inside */

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* The picture in an MP3's ID3 tag: its bytes (free them), or NULL. The front cover when there's
 * more than one. */
static unsigned char *mp3_picture(FILE *f, long *len)
{
    unsigned char h[10], *best = NULL;
    long end, pos = 10, best_len = 0;
    int ver, best_front = 0;
    if (fread(h, 1, 10, f) != 10 || memcmp(h, "ID3", 3)) return NULL;
    ver = h[3];
    end = 10 + ((long)(h[6] & 127) << 21 | (h[7] & 127) << 14 | (h[8] & 127) << 7 | (h[9] & 127));
    if (ver < 2 || ver > 4) return NULL;
    while (pos + 10 <= end) {
        long hl = ver == 2 ? 6 : 10, size;
        char id[5] = {0};
        if (fseek(f, pos, SEEK_SET) || fread(h, 1, (size_t)hl, f) != (size_t)hl) break;
        memcpy(id, h, ver == 2 ? 3 : 4);
        size = ver == 2 ? (long)(h[3] << 16 | h[4] << 8 | h[5])
             : ver == 4 ? ((long)(h[4] & 127) << 21 | (h[5] & 127) << 14 | (h[6] & 127) << 7 | (h[7] & 127)) : (long)be32(h + 4);
        if (!isalnum((unsigned char)id[0]) || size <= 0 || pos + hl + size > end) break;
        if ((!strcmp(id, "APIC") || !strcmp(id, "PIC")) && size < 12 * 1024 * 1024 && !(ver == 4 && (h[9] & 0x0E))) {
            unsigned char *d = malloc((size_t)size);
            if (d && fread(d, 1, (size_t)size, f) == (size_t)size) {
                long i = 1, start;
                int enc = d[0], type;
                if (ver == 4 && (h[9] & 1)) i += 4;          /* a length first */
                if (ver == 2) i += 3;                        /* "JPG" */
                else { while (i < size && d[i]) i++; i++; }  /* the MIME type */
                type = i < size ? d[i] : 0;
                i++;
                if (enc == 1 || enc == 2) {                  /* the description, UTF-16: ends 00 00 */
                    while (i + 1 < size && (d[i] || d[i + 1])) i += 2;
                    i += 2;
                } else {
                    while (i < size && d[i]) i++;
                    i++;
                }
                start = i;
                if (start < size && (!best || (type == 3 && !best_front))) {
                    free(best);
                    best_len = size - start;
                    best = malloc((size_t)best_len);
                    if (best) memcpy(best, d + start, (size_t)best_len);
                    best_front = type == 3;
                }
            }
            free(d);
        }
        pos += hl + size;
    }
    *len = best_len;
    return best;
}

/* The picture in a FLAC's PICTURE block. */
static unsigned char *flac_picture(FILE *f, long *len)
{
    unsigned char h[4];
    int k;
    if (fread(h, 1, 4, f) != 4) return NULL;
    if (!memcmp(h, "ID3", 3)) {                      /* skip an ID3 tag in front of it */
        unsigned char t[6];
        if (fread(t, 1, 6, f) != 6) return NULL;
        if (fseek(f, 10 + ((long)(t[2] & 127) << 21 | (t[3] & 127) << 14 | (t[4] & 127) << 7 | (t[5] & 127)), SEEK_SET)
                || fread(h, 1, 4, f) != 4)
            return NULL;
    }
    if (memcmp(h, "fLaC", 4)) return NULL;
    for (k = 0; k < 64; k++) {
        long blen;
        int last;
        if (fread(h, 1, 4, f) != 4) return NULL;
        last = h[0] & 0x80;
        blen = (long)(h[1] << 16 | h[2] << 8 | h[3]);
        if ((h[0] & 0x7F) == 6 && blen > 32 && blen < 12 * 1024 * 1024) {
            unsigned char *b = malloc((size_t)blen), *out = NULL;
            if (b && fread(b, 1, (size_t)blen, f) == (size_t)blen) {
                long i = 4, l;
                l = (long)be32(b + i); i += 4 + l;              /* MIME */
                if (i + 4 <= blen) { l = (long)be32(b + i); i += 4 + l; }   /* description */
                i += 16;                                        /* width, height, depth, colours */
                if (i + 4 <= blen) {
                    l = (long)be32(b + i);
                    i += 4;
                    if (l > 0 && i + l <= blen && (out = malloc((size_t)l))) { memcpy(out, b + i, (size_t)l); *len = l; }
                }
            }
            free(b);
            if (out) return out;
        } else if (fseek(f, blen, SEEK_CUR)) return NULL;
        if (last) break;
    }
    return NULL;
}

/* ------------------------------------------------------------- covers */

static void cache_path(const char *src, long size, char *out, int n)
{
    snprintf(out, n, "uda:/xenith/thumbs/music-%08x-%lx-%d.ximg", (unsigned)hash_str(src), (unsigned long)size, ART_SIZE);
}

static int make_cover(struct album *a)
{
    const char *src = folders[a->folder].cover[0] ? folders[a->folder].cover : a->art_song >= 0 ? songs[a->art_song].path : NULL;
    unsigned char *data = NULL;
    long len = 0;
    char cache[200];
    struct stat st;
    uint32_t *px;
    int w, h, ok = 0;
    if (!src || stat(src, &st) != 0) return 0;
    cache_path(src, (long)st.st_size, cache, sizeof cache);
    if (img_load_file(&a->thumb, cache) && a->thumb.w == ART_SIZE) return 1;
    img_free(&a->thumb);
    if (folders[a->folder].cover[0]) {
        FILE *f = fopen(src, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            len = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (len > 0 && len < 24L * 1024 * 1024 && (data = malloc((size_t)len)) && fread(data, 1, (size_t)len, f) != (size_t)len) {
                free(data);
                data = NULL;
            }
            fclose(f);
        }
    } else {
        FILE *f = fopen(src, "rb");
        if (f) {
            data = songs[a->art_song].fmt == FMT_FLAC ? flac_picture(f, &len) : mp3_picture(f, &len);
            fclose(f);
        }
    }
    if (data && picture_info(data, (int)len, &w, &h) && (long long)w * h <= 24LL * 1000 * 1000
            && (px = picture_decode(data, (int)len, &w, &h))) {
        if (picture_cover(&a->thumb, px, w, h, ART_SIZE, ART_SIZE)) {
            mkdir("uda:/xenith/thumbs", 0777);
            img_save_file(&a->thumb, cache);
            ok = 1;
        }
        free(px);
    }
    free(data);
    return ok;
}

void art_mark_drawn(int album, int size)
{
    (void)size;
    if (album < 0 || album >= nalbums) return;
    if (nwanted < (int)(sizeof wanted / sizeof wanted[0])) wanted[nwanted++] = album;
}

void art_draw(int album, int x, int y, int size, float radius)
{
    struct album *a;
    if (album < 0 || album >= nalbums) { art_placeholder("", x, y, size, radius); return; }
    a = &albums[album];
    art_mark_drawn(album, size);
    if (!drawn_at) drawn_at = calloc(MAX_ALBUMS, sizeof *drawn_at);
    if (drawn_at) drawn_at[album] = frame;
    if (a->thumb.px) gfx_image_rounded(&a->thumb, x, y, size, size, radius, 255);
    else art_placeholder(a->name, x, y, size, radius);
}

int art_load_one(int size)
{
    int i, held = 0;
    (void)size;
    for (i = 0; i < nwanted; i++) {
        struct album *a = &albums[wanted[i]];
        if (a->thumb.px || a->thumb_tried) continue;
        a->thumb_tried = 1;
        a->thumb_none = !make_cover(a);
        return 1;
    }
    /* let covers not drawn for a while go, once there are a lot */
    for (i = 0; i < nalbums; i++) if (albums[i].thumb.px) held++;
    for (i = 0; held > KEEP && drawn_at && i < nalbums; i++)
        if (albums[i].thumb.px && frame - drawn_at[i] > 120) {
            img_free(&albums[i].thumb);
            albums[i].thumb_tried = 0;
            held--;
        }
    return 0;
}
