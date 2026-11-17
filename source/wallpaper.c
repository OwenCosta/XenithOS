/* WALLPAPERS: the home screen's background pictures. See wallpaper.h.
 *
 * Every one is flat: solid shapes in a few tones between the theme's background and its cards,
 * with a touch of its accent, no gradients and no glows. They're laid out in fractions of the
 * box, so a card's preview is the same picture as the screen. Edges that run across the screen
 * (ridges, waves, bands) are filled a column at a time with smooth tops, which is quick even on
 * the Xbox; the picture is made once and copied in after that.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "theme.h"
#include "ui.h"
#include "wallpaper.h"

static const char *const ids[WALL_COUNT] = {"plain", "rings", "peaks", "bands", "halftone", "waves"};
static const char *const names[WALL_COUNT] = {"Plain", "Rings", "Peaks", "Bands", "Halftone", "Waves"};

const char *wallpaper_id(int w) { return ids[w >= 0 && w < WALL_COUNT ? w : 0]; }
const char *wallpaper_name(int w) { return names[w >= 0 && w < WALL_COUNT ? w : 0]; }

int wallpaper_find(const char *id)
{
    int i;
    for (i = 0; id && i < WALL_COUNT; i++)
        if (!strcmp(id, ids[i])) return i;
    return WALL_PLAIN;
}

/* The tones: from the background up to a little past the cards, and two of the accent. */
struct tones { uint32_t bg, t1, t2, t3, a1, a2; };

static void tones(struct tones *t)
{
    t->bg = C_BG;
    t->t1 = gfx_mix(C_BG, C_SURFACE, 120);
    t->t2 = gfx_mix(C_BG, C_SURFACE, 220);
    t->t3 = gfx_mix(C_SURFACE, C_SURFACE2, 150);
    t->a1 = gfx_mix(C_BG, C_ACCENT, 30);
    t->a2 = gfx_mix(C_BG, C_ACCENT, 62);
}

/* --------------------------------------------------------------- filling */

/* A height for each column: the edge of what's filled, in box fractions (0 top, 1 bottom). */
typedef float (*edge_fn)(float u, const void *arg);

/* Fills everything below the edge, a column at a time. Each column's top pixel is shaded by how
 * much of it is below the edge at four points across it, so steep slopes stay smooth too. */
static void fill_below(int x, int y, int bw, int bh, edge_fn edge, const void *arg, uint32_t c)
{
    int X, bottom = y + bh;
    for (X = 0; X < bw; X++) {
        float h[4], lo = 1e9f, hi = -1e9f;
        int k, Y, y0, y1;
        for (k = 0; k < 4; k++) {
            h[k] = y + edge((X + 0.125f + 0.25f * k) / bw, arg) * bh;
            if (h[k] < lo) lo = h[k];
            if (h[k] > hi) hi = h[k];
        }
        y0 = (int)floorf(lo);
        y1 = (int)ceilf(hi);
        if (y0 < y) y0 = y;
        for (Y = y0; Y < y1 && Y < bottom; Y++) {
            float cov = 0;
            for (k = 0; k < 4; k++) {
                float f = Y + 1 - h[k];
                cov += f < 0 ? 0 : f > 1 ? 1 : f;
            }
            if (cov > 0.02f) gfx_rect_a(x + X, Y, 1, 1, c, (int)(cov * 63.75f + 0.5f));
        }
        if (y1 < y) y1 = y;
        if (y1 < bottom) gfx_rect(x + X, y1, 1, bottom - y1, c);
    }
}

/* A line through (u, v) pairs, u going up. */
struct polyline { const float *p; int n; };

static float along(float u, const void *arg)
{
    const struct polyline *l = arg;
    int i;
    if (u <= l->p[0]) return l->p[1];
    for (i = 1; i < l->n; i++)
        if (u <= l->p[2 * i]) {
            float u0 = l->p[2 * i - 2], v0 = l->p[2 * i - 1], u1 = l->p[2 * i], v1 = l->p[2 * i + 1];
            return v0 + (v1 - v0) * (u - u0) / (u1 - u0);
        }
    return l->p[2 * l->n - 1];
}

/* A straight edge: v = a + b * u */
struct straight { float a, b; };

static float straight_edge(float u, const void *arg)
{
    const struct straight *s = arg;
    return s->a + s->b * u;
}

/* A wave: base + the sum of two sines (amplitudes in box heights, frequencies in waves across). */
struct wave { float base, a1, f1, p1, a2, f2, p2; };

static float wave_edge(float u, const void *arg)
{
    const struct wave *w = arg;
    return w->base + w->a1 * sinf(6.2831853f * (u * w->f1 + w->p1)) + w->a2 * sinf(6.2831853f * (u * w->f2 + w->p2));
}

/* ------------------------------------------------------------ the pictures */

/* Rings round the top right corner, like the logo's, one of them with a quarter in the accent. */
static void rings(int x, int y, int bw, int bh, const struct tones *t)
{
    float cx = x + bw * 0.97f, cy = y - bh * 0.10f, step = bh * 0.155f, th = bh * 0.05f;
    int i;
    for (i = 0; i < 7; i++) {
        float r = bh * 0.34f + step * i;
        gfx_ring(cx, cy, r, th, i & 1 ? t->t1 : t->t2, 255);
        if (i == 2) gfx_arc(cx, cy, r, th, 1.95f, 2.75f, t->a2, 255);
    }
}

/* Three ridges of mountains, the nearest the lightest, under a low moon. */
static void peaks(int x, int y, int bw, int bh, const struct tones *t)
{
    static const float far[] = {0, .30f, .06f, .23f, .12f, .28f, .20f, .13f, .28f, .24f, .35f, .18f, .43f, .27f,
                                .51f, .16f, .59f, .25f, .66f, .11f, .74f, .21f, .82f, .17f, .90f, .26f, 1, .19f};
    static const float mid[] = {0, .50f, .09f, .41f, .17f, .47f, .26f, .35f, .36f, .46f, .45f, .39f, .54f, .49f,
                                .63f, .38f, .73f, .45f, .83f, .34f, .92f, .43f, 1, .38f};
    static const float near[] = {0, .70f, .12f, .62f, .23f, .69f, .34f, .58f, .47f, .68f, .58f, .61f, .70f, .70f,
                                 .81f, .59f, .91f, .66f, 1, .60f};
    struct polyline a = {far, sizeof far / sizeof far[0] / 2}, b = {mid, sizeof mid / sizeof mid[0] / 2};
    struct polyline c = {near, sizeof near / sizeof near[0] / 2};
    gfx_circle(x + bw * 0.64f, y + bh * 0.115f, bh * 0.05f, t->a2, 255);
    fill_below(x, y, bw, bh, along, &a, t->t1);
    fill_below(x, y, bw, bh, along, &b, t->t2);
    fill_below(x, y, bw, bh, along, &c, t->t3);
}

/* Wide stripes rising to the right, one thin one in the accent. */
static void bands(int x, int y, int bw, int bh, const struct tones *t)
{
    static const struct straight lines[] = {
        {0.62f, -0.95f}, {0.80f, -0.95f}, {0.98f, -0.95f}, {1.03f, -0.95f}, {1.30f, -0.95f}, {1.58f, -0.95f},
    };
    const uint32_t colors[] = {t->t1, t->t2, t->a2, t->t2, t->t1, t->bg};
    int i;
    for (i = 0; i < (int)(sizeof lines / sizeof lines[0]); i++) fill_below(x, y, bw, bh, straight_edge, &lines[i], colors[i]);
}

/* A halftone corner: a grid of dots growing towards the top right. */
static void halftone(int x, int y, int bw, int bh, const struct tones *t)
{
    float sp = bh * 0.052f, aspect = (float)bw / bh;
    int cols = (int)(bw / sp) + 2, rows = (int)(bh / sp) + 2, i, j;
    for (j = 0; j < rows; j++)
        for (i = 0; i < cols; i++) {
            float px = (i + 0.5f) * sp, py = (j + 0.5f) * sp;
            float du = (bw - px) / bh, dv = py / bh, d = sqrtf(du * du + dv * dv), k = 1.0f - d / (0.62f * (aspect > 1.5f ? 1.15f : 1));
            if (k <= 0.05f) continue;
            gfx_circle(x + px, y + py, sp * 0.46f * (k > 1 ? 1 : k), k > 0.82f ? t->a2 : t->t3, 255);
        }
}

/* Waves rolling in along the bottom. */
static void waves(int x, int y, int bw, int bh, const struct tones *t)
{
    static const struct wave w[] = {
        {0.50f, 0.035f, 1.3f, 0.10f, 0.012f, 3.1f, 0.60f},
        {0.64f, 0.040f, 1.1f, 0.55f, 0.015f, 2.6f, 0.20f},
        {0.79f, 0.030f, 1.6f, 0.30f, 0.010f, 3.7f, 0.85f},
    };
    fill_below(x, y, bw, bh, wave_edge, &w[0], t->t1);
    fill_below(x, y, bw, bh, wave_edge, &w[1], t->a1);
    fill_below(x, y, bw, bh, wave_edge, &w[2], t->t2);
}

void wallpaper_draw(int w, int x, int y, int bw, int bh)
{
    struct tones t;
    int clip[4];
    if (bw <= 0 || bh <= 0) return;
    tones(&t);
    gfx_get_clip(clip);
    gfx_clip_intersect(x, y, bw, bh);
    gfx_rect(x, y, bw, bh, t.bg);
    switch (w) {
    case WALL_RINGS: rings(x, y, bw, bh, &t); break;
    case WALL_PEAKS: peaks(x, y, bw, bh, &t); break;
    case WALL_BANDS: bands(x, y, bw, bh, &t); break;
    case WALL_HALFTONE: halftone(x, y, bw, bh, &t); break;
    case WALL_WAVES: waves(x, y, bw, bh, &t); break;
    }
    gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
}

/* ------------------------------------------------------------- the screen */

static uint32_t *cache;
static int cache_w, cache_h, cache_wall = -1;
static struct xenith_palette cache_pal;

void wallpaper_bg(int w)
{
    size_t n = (size_t)SCR_W * SCR_H;
    if (w <= WALL_PLAIN || w >= WALL_COUNT) {
        ui_bg();
        return;
    }
    if (!cache || cache_w != SCR_W || cache_h != SCR_H || cache_wall != w || memcmp(&cache_pal, &pal, sizeof pal)) {
        int sw = SCR_W, sh = SCR_H;
        if (!cache || cache_w != SCR_W || cache_h != SCR_H) {
            free(cache);
            cache = malloc(n * 4);
            if (!cache) { ui_bg(); return; }
        }
        cache_w = SCR_W;
        cache_h = SCR_H;
        if (!gfx_target(cache, sw, sh)) { ui_bg(); return; }
        wallpaper_draw(w, 0, 0, sw, sh);
        gfx_target(NULL, 0, 0);
        cache_wall = w;
        cache_pal = pal;
    }
    memcpy(gfx_bb, cache, n * 4);
}
