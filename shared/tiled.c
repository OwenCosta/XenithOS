#include <stdlib.h>
#include <string.h>

#include "tiled.h"

void tiled_init(struct tiled_fb *t, uint32_t *fb, int fb_w, int fb_h, int off_x, int off_y, int w, int h)
{
    int x, y;
    memset(t, 0, sizeof *t);
    t->fb = fb;
    t->pitch = ((fb_w + 31) >> 5) << 5;
    t->rows = ((fb_h + 31) >> 5) << 5;
    t->off_x = off_x;
    t->off_y = off_y;
    t->w = w;
    t->h = h;
    t->tiles_x = t->pitch / 32;
    t->tiles_y = t->rows / 32;

    /* Inside a tile, pixel (x, y) lives at this offset. The XOR on bit 5 only ever touches
     * the low 10 bits, so it can be folded into the table instead of applied per pixel. */
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++)
            t->inner[y * 32 + x] = (uint16_t)(((x & 3) + ((y & 1) << 2) + ((x >> 2) << 3) + ((y >> 1) << 6)) ^ ((y & 8) << 2));

    t->col_tile = malloc(sizeof(int) * w);
    t->col_in = malloc(sizeof(int) * w);
    t->row_base = malloc(sizeof(int) * h);
    for (x = 0; x < w; x++) {
        int X = x + off_x;
        t->col_tile[x] = (X >> 5) << 10;
        t->col_in[x] = X & 31;
    }
    for (y = 0; y < h; y++)
        t->row_base[y] = ((y + off_y) >> 5) * 32 * t->pitch;

    /* No real frame ever has a non-zero top byte, so the first present rewrites everything. */
    t->shadow = malloc((size_t)w * h * 4);
    memset(t->shadow, 0xff, (size_t)w * h * 4);
    t->dirty = calloc((size_t)t->tiles_x * t->tiles_y, 1);
}

typedef uint64_t __attribute__((may_alias)) u64a;

/* Does this 32-pixel piece of a row differ? Two pixels per compare when aligned. */
static int seg_differs(const uint32_t *a, const uint32_t *b, int n, int wide)
{
    int x;
    if (wide) {
        const u64a *a8 = (const u64a *)a, *b8 = (const u64a *)b;
        uint64_t diff = 0;
        for (x = 0; x < n / 2; x++) diff |= a8[x] ^ b8[x];
        return diff != 0;
    } else {
        uint32_t diff = 0;
        for (x = 0; x < n; x++) diff |= a[x] ^ b[x];
        return diff != 0;
    }
}

int tiled_present(struct tiled_fb *t, const uint32_t *px, void (*flush)(void *addr, int bytes))
{
    int bx, by, x, y, changed = 0, i, nbx = (t->w + 31) / 32;
    int wide = !(((uintptr_t)px | (uintptr_t)t->shadow | (uintptr_t)t->w * 4) & 7) && !(t->w & 1);
    unsigned char col_changed[128];
    if (nbx > (int)sizeof col_changed) nbx = sizeof col_changed;
    for (by = 0; by < t->h; by += 32) {
        int bh = t->h - by < 32 ? t->h - by : 32;

        /* Find the changed blocks in this band by walking it row by row: the band is one
         * contiguous piece of memory, which the console reads far faster than block by block. */
        memset(col_changed, 0, (size_t)nbx);
        for (y = by; y < by + bh; y++) {
            const uint32_t *a = px + (size_t)y * t->w, *b = t->shadow + (size_t)y * t->w;
            __builtin_prefetch(a + t->w);
            __builtin_prefetch(b + t->w);
            for (i = 0; i < nbx; i++) {
                int x0 = i * 32, n = t->w - x0 < 32 ? t->w - x0 : 32;
                if (!col_changed[i] && seg_differs(a + x0, b + x0, n, wide && !(n & 1))) col_changed[i] = 1;
            }
        }

        for (i = 0; i < nbx; i++) {
            int bw, tx, ty;
            if (!col_changed[i]) continue;
            bx = i * 32;
            bw = t->w - bx < 32 ? t->w - bx : 32;
            changed++;
            for (y = by; y < by + bh; y++) {
                const uint32_t *src = px + (size_t)y * t->w + bx;
                uint32_t *dst = t->fb + t->row_base[y];
                const uint16_t *inner = &t->inner[((y + t->off_y) & 31) * 32];
                const int *ct = t->col_tile + bx, *ci = t->col_in + bx;
                for (x = 0; x < bw; x++) dst[ct[x] + inner[ci[x]]] = __builtin_bswap32(src[x]);
                memcpy(t->shadow + (size_t)y * t->w + bx, src, (size_t)bw * 4);
            }
            for (ty = (by + t->off_y) >> 5; ty <= (by + bh - 1 + t->off_y) >> 5; ty++)
                for (tx = (bx + t->off_x) >> 5; tx <= (bx + bw - 1 + t->off_x) >> 5; tx++)
                    t->dirty[ty * t->tiles_x + tx] = 1;
        }
    }
    for (i = 0; changed && i < t->tiles_x * t->tiles_y; i++) {
        if (!t->dirty[i]) continue;
        if (flush) flush(t->fb + (i / t->tiles_x) * 32 * t->pitch + ((i % t->tiles_x) << 10), 4096);
        t->dirty[i] = 0;
    }
    return changed;
}

uint32_t tiled_read(const struct tiled_fb *t, int x, int y)
{
    uint32_t v = t->fb[t->row_base[y] + t->col_tile[x] + t->inner[((y + t->off_y) & 31) * 32 + t->col_in[x]]];
    return __builtin_bswap32(v);
}
