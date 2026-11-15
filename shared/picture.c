/* Pictures through stb_image (public domain / MIT, see stb_image.h). See picture.h. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#if defined(XENON)
#define STBI_NO_SIMD               /* stb has SSE2 and NEON paths, nothing for the Xbox */
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "picture.h"

int picture_info(const unsigned char *data, int len, int *w, int *h)
{
    int comp;
    if (!data || len <= 0) return 0;
    return stbi_info_from_memory(data, len, w, h, &comp) && *w > 0 && *h > 0;
}

uint32_t *picture_decode(const unsigned char *data, int len, int *w, int *h)
{
    int comp, i, n;
    unsigned char *rgba;
    uint32_t *out;
    if (!picture_info(data, len, w, h) || (long long)*w * *h > PICTURE_MAX_PIXELS) return NULL;
    rgba = stbi_load_from_memory(data, len, w, h, &comp, 4);
    if (!rgba) return NULL;
    n = *w * *h;
    out = malloc(sizeof(uint32_t) * (size_t)n);
    if (out)
        for (i = 0; i < n; i++) {
            const unsigned char *p = rgba + (size_t)i * 4;
            out[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    stbi_image_free(rgba);
    return out;
}

/* The part of px from (sx, sy), sw x sh in size, averaged down (or stretched up) to
 * dw x dh. Each output pixel is the average of the source pixels it covers. */
static uint32_t *resample(const uint32_t *px, int w, int sx, int sy, int sw, int sh, int dw, int dh)
{
    uint32_t *out = malloc(sizeof(uint32_t) * (size_t)dw * dh);
    int x, y;
    if (!out) return NULL;
    for (y = 0; y < dh; y++) {
        int y0 = sy + (int)((long long)y * sh / dh), y1 = sy + (int)((long long)(y + 1) * sh / dh);
        if (y1 <= y0) y1 = y0 + 1;
        for (x = 0; x < dw; x++) {
            int x0 = sx + (int)((long long)x * sw / dw), x1 = sx + (int)((long long)(x + 1) * sw / dw), xx, yy;
            uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;
            if (x1 <= x0) x1 = x0 + 1;
            for (yy = y0; yy < y1; yy++)
                for (xx = x0; xx < x1; xx++) {
                    uint32_t c = px[(size_t)yy * w + xx];
                    a += c >> 24; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255;
                    n++;
                }
            out[(size_t)y * dw + x] = ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
    return out;
}

static int finish(struct img *out, uint32_t *px, int w, int h)
{
    memset(out, 0, sizeof *out);
    if (!px) return 0;
    out->px = px;
    out->w = w;
    out->h = h;
    out->id = gfx_new_image_id();
    return 1;
}

int picture_load_mem(struct img *out, const unsigned char *data, int len, int max_w, int max_h)
{
    int w, h, dw, dh;
    uint32_t *px = picture_decode(data, len, &w, &h), *small;
    memset(out, 0, sizeof *out);
    if (!px) return 0;
    dw = w;
    dh = h;
    if (max_w > 0 && dw > max_w) { dh = (int)((long long)dh * max_w / dw); dw = max_w; }
    if (max_h > 0 && dh > max_h) { dw = (int)((long long)dw * max_h / dh); dh = max_h; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (dw == w && dh == h) return finish(out, px, w, h);
    small = resample(px, w, 0, 0, w, h, dw, dh);
    free(px);
    return finish(out, small, dw, dh);
}

int picture_load_file(struct img *out, const char *path, int max_w, int max_h)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;
    int ok;
    memset(out, 0, sizeof *out);
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64L * 1024 * 1024) { fclose(f); return 0; }
    buf = malloc((size_t)n);
    if (!buf) { fclose(f); return 0; }
    if ((long)fread(buf, 1, (size_t)n, f) != n) { fclose(f); free(buf); return 0; }
    fclose(f);
    ok = picture_load_mem(out, buf, (int)n, max_w, max_h);
    free(buf);
    return ok;
}

int picture_cover(struct img *out, const uint32_t *px, int w, int h, int out_w, int out_h)
{
    int sw = w, sh = h;
    memset(out, 0, sizeof *out);
    if (!px || w <= 0 || h <= 0 || out_w <= 0 || out_h <= 0) return 0;
    /* The biggest part of the picture with the output's shape, from its middle. */
    if ((long long)w * out_h > (long long)h * out_w) sw = (int)((long long)h * out_w / out_h);
    else sh = (int)((long long)w * out_h / out_w);
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    return finish(out, resample(px, w, (w - sw) / 2, (h - sh) / 2, sw, sh, out_w, out_h), out_w, out_h);
}

int picture_region(struct img *out, const uint32_t *px, int w, int h, int sx, int sy, int sw, int sh,
                   int out_w, int out_h)
{
    memset(out, 0, sizeof *out);
    if (!px || w <= 0 || h <= 0 || out_w <= 0 || out_h <= 0) return 0;
    if (sw > w) sw = w;
    if (sh > h) sh = h;
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx + sw > w) sx = w - sw;
    if (sy + sh > h) sy = h - sh;
    return finish(out, resample(px, w, sx, sy, sw, sh, out_w, out_h), out_w, out_h);
}
