#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "img.h"

#define MAX_SIDE 1024

static uint32_t be32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }

int img_load_mem(struct img *out, const void *data, int len)
{
    const unsigned char *d = data;
    int w, h, i;
    uLongf raw_len;
    unsigned char *raw;

    memset(out, 0, sizeof *out);
    if (len < 16 || memcmp(d, "XIMG", 4)) return 0;
    w = be16(d + 4);
    h = be16(d + 6);
    if (d[8] != 1 || d[9] != 1) return 0;
    if (w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE) return 0;
    raw_len = be32(d + 12);
    if (raw_len != (uLongf)w * h * 4) return 0;

    raw = malloc(raw_len);
    if (!raw) return 0;
    if (uncompress(raw, &raw_len, d + 16, len - 16) != Z_OK || raw_len != (uLongf)w * h * 4) {
        free(raw);
        return 0;
    }
    out->px = malloc((size_t)w * h * 4);
    if (!out->px) { free(raw); return 0; }
    for (i = 0; i < w * h; i++) {                    /* stored as R, G, B, A bytes */
        const unsigned char *p = raw + i * 4;
        out->px[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    }
    free(raw);
    out->w = w;
    out->h = h;
    out->id = gfx_new_image_id();          /* scaled copies are cached by this */
    return 1;
}

int img_load_file(struct img *out, const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    void *buf;
    int ok;
    memset(out, 0, sizeof *out);
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 16 || n > 4 * 1024 * 1024) { fclose(f); return 0; }
    buf = malloc(n);
    if (!buf) { fclose(f); return 0; }
    if ((long)fread(buf, 1, n, f) != n) { fclose(f); free(buf); return 0; }
    fclose(f);
    ok = img_load_mem(out, buf, (int)n);
    free(buf);
    return ok;
}

int img_save_file(const struct img *im, const char *path)
{
    size_t raw_len = (size_t)im->w * im->h * 4, i;
    uLongf zlen = compressBound((uLong)raw_len);
    unsigned char *raw, *z, head[16];
    FILE *f;
    int ok = 0;
    if (!im->px || im->w <= 0 || im->h <= 0 || im->w > MAX_SIDE || im->h > MAX_SIDE) return 0;
    raw = malloc(raw_len);
    z = malloc(zlen);
    if (raw && z) {
        for (i = 0; i < (size_t)im->w * im->h; i++) {   /* stored as R, G, B, A bytes */
            uint32_t c = im->px[i];
            raw[i * 4] = (c >> 16) & 255;
            raw[i * 4 + 1] = (c >> 8) & 255;
            raw[i * 4 + 2] = c & 255;
            raw[i * 4 + 3] = c >> 24;
        }
        if (compress2(z, &zlen, raw, (uLong)raw_len, 6) == Z_OK && (f = fopen(path, "wb"))) {
            memcpy(head, "XIMG", 4);
            head[4] = im->w >> 8; head[5] = im->w & 255;
            head[6] = im->h >> 8; head[7] = im->h & 255;
            head[8] = 1;                          /* RGBA8 */
            head[9] = 1;                          /* zlib */
            head[10] = head[11] = 0;
            head[12] = raw_len >> 24; head[13] = (raw_len >> 16) & 255; head[14] = (raw_len >> 8) & 255; head[15] = raw_len & 255;
            ok = fwrite(head, 1, 16, f) == 16 && fwrite(z, 1, zlen, f) == zlen;
            if (fclose(f) != 0) ok = 0;
            if (!ok) remove(path);
        }
    }
    free(raw);
    free(z);
    return ok;
}

void img_free(struct img *im)
{
    free(im->px);
    memset(im, 0, sizeof *im);
}
