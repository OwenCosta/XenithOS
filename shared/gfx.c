/* XenithOS drawing: back buffer, anti-aliased shapes, Inter text, images. See gfx.h. */
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "gfx.h"
#include "platform.h"

int SCR_W, SCR_H;
uint32_t *gfx_bb;

static int cx0, cy0, cx1, cy1;      /* clip rectangle, x1/y1 exclusive */

void gfx_init(void)
{
    plat_screen_size(&SCR_W, &SCR_H);
    gfx_bb = calloc((size_t)SCR_W * SCR_H, 4);
    gfx_reset_clip();
}

void gfx_present(void)
{
    audio_update();             /* keeps sound fed when there's no mixer thread */
    plat_present(gfx_bb);
    audio_update();
}

uint32_t gfx_new_image_id(void)
{
    static uint32_t next;
    return ++next;
}

/* Off-screen drawing: swap the back buffer and screen size, restore them afterwards. */
static uint32_t *saved_bb;
static int saved_w, saved_h, saved_clip[4], in_target;

int gfx_target(uint32_t *buf, int w, int h)
{
    if (buf && !in_target) {
        saved_bb = gfx_bb;
        saved_w = SCR_W;
        saved_h = SCR_H;
        gfx_get_clip(saved_clip);
        gfx_bb = buf;
        SCR_W = w;
        SCR_H = h;
        in_target = 1;
        gfx_reset_clip();
        return 1;
    }
    if (!buf && in_target) {
        gfx_bb = saved_bb;
        SCR_W = saved_w;
        SCR_H = saved_h;
        in_target = 0;
        gfx_set_clip(saved_clip[0], saved_clip[1], saved_clip[2], saved_clip[3]);
        return 1;
    }
    return 0;
}

void gfx_set_clip(int x, int y, int w, int h)
{
    cx0 = x < 0 ? 0 : x;
    cy0 = y < 0 ? 0 : y;
    cx1 = x + w > SCR_W ? SCR_W : x + w;
    cy1 = y + h > SCR_H ? SCR_H : y + h;
    if (cx1 < cx0) cx1 = cx0;
    if (cy1 < cy0) cy1 = cy0;
}

void gfx_reset_clip(void)
{
    cx0 = 0; cy0 = 0; cx1 = SCR_W; cy1 = SCR_H;
}

void gfx_get_clip(int r[4])
{
    r[0] = cx0; r[1] = cy0; r[2] = cx1 - cx0; r[3] = cy1 - cy0;
}

void gfx_clip_intersect(int x, int y, int w, int h)
{
    int x0 = x > cx0 ? x : cx0, y0 = y > cy0 ? y : cy0;
    int x1 = x + w < cx1 ? x + w : cx1, y1 = y + h < cy1 ? y + h : cy1;
    cx0 = x0; cy0 = y0;
    cx1 = x1 > x0 ? x1 : x0;
    cy1 = y1 > y0 ? y1 : y0;
}

/* ------------------------------------------------------------ pixel blending */

static inline void blend(uint32_t *p, uint32_t c, int a)
{
    uint32_t d;
    int dr, dg, db;
    if (a <= 0) return;
    if (a >= 255) { *p = c; return; }
    a += a >> 7;                                   /* 0..255 -> 0..256 */
    d = *p;
    dr = (d >> 16) & 255; dg = (d >> 8) & 255; db = d & 255;
    dr += ((((int)(c >> 16) & 255) - dr) * a) >> 8;
    dg += ((((int)(c >> 8) & 255) - dg) * a) >> 8;
    db += ((((int)c & 255) - db) * a) >> 8;
    *p = ((uint32_t)dr << 16) | ((uint32_t)dg << 8) | (uint32_t)db;
}

uint32_t gfx_mix(uint32_t a, uint32_t b, int t)
{
    int ra = (a >> 16) & 0xff, ga = (a >> 8) & 0xff, ba = a & 0xff;
    int rb = (b >> 16) & 0xff, gb = (b >> 8) & 0xff, bb = b & 0xff;
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    return RGB(ra + ((rb - ra) * t >> 8), ga + ((gb - ga) * t >> 8), ba + ((bb - ba) * t >> 8));
}

uint32_t gfx_shade(uint32_t c, int pct)
{
    if (pct >= 0) return gfx_mix(c, 0xffffff, pct * 256 / 100);
    return gfx_mix(c, 0x000000, -pct * 256 / 100);
}

/* ----------------------------------------------------------------- rectangles */

typedef uint64_t __attribute__((may_alias)) u64a;

void gfx_clear(uint32_t c)
{
    size_t i, n = (size_t)SCR_W * SCR_H;
    if (((uintptr_t)gfx_bb & 7) == 0) {          /* two pixels per store */
        u64a *p = (u64a *)gfx_bb, v = ((uint64_t)c << 32) | c;
        for (i = 0; i < n / 2; i++) p[i] = v;
        if (n & 1) gfx_bb[n - 1] = c;
    } else {
        for (i = 0; i < n; i++) gfx_bb[i] = c;
    }
}

void gfx_rect(int x, int y, int w, int h, uint32_t c)
{
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    int px, py;
    for (py = y0; py < y1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        for (px = x0; px < x1; px++) row[px] = c;
    }
}

/* Same result as blend() on every pixel, but red and blue share one multiply and the colour
 * side is worked out once. Screen fades cover every pixel, so this matters. */
void gfx_rect_a(int x, int y, int w, int h, uint32_t c, int a)
{
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    int px, py;
    uint32_t k, ik, crb, cg;
    if (a >= 255) { gfx_rect(x, y, w, h, c); return; }
    if (a <= 0) return;
    k = (uint32_t)(a + (a >> 7));
    ik = 256 - k;
    crb = (c & 0xff00ff) * k;
    cg = (c & 0x00ff00) * k;
    for (py = y0; py < y1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        for (px = x0; px < x1; px++) {
            uint32_t d = row[px];
            row[px] = ((((d & 0xff00ff) * ik + crb) >> 8) & 0xff00ff) | ((((d & 0x00ff00) * ik + cg) >> 8) & 0x00ff00);
        }
    }
}

/* ----------------------------------------------------- signed-distance shapes */

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Coverage of a pixel whose centre is d pixels outside the shape edge. */
static inline int cover(float d, int a)
{
    if (d <= -0.5f) return a;
    if (d >= 0.5f) return 0;
    return (int)(a * (0.5f - d));
}

static int bbox(float x0, float y0, float x1, float y1, int *ix0, int *iy0, int *ix1, int *iy1)
{
    *ix0 = (int)floorf(x0); *iy0 = (int)floorf(y0);
    *ix1 = (int)ceilf(x1);  *iy1 = (int)ceilf(y1);
    if (*ix0 < cx0) *ix0 = cx0;
    if (*iy0 < cy0) *iy0 = cy0;
    if (*ix1 > cx1) *ix1 = cx1;
    if (*iy1 > cy1) *iy1 = cy1;
    return *ix0 < *ix1 && *iy0 < *iy1;
}

static inline float sd_rrect(float px, float py, float cx, float cy, float hx, float hy, float r)
{
    float qx = fabsf(px - cx) - hx, qy = fabsf(py - cy) - hy;
    if (qx <= 0 && qy <= 0) return (qx > qy ? qx : qy) - r;
    if (qx <= 0) return qy - r;
    if (qy <= 0) return qx - r;
    return sqrtf(qx * qx + qy * qy) - r;
}

/* For one row of a rounded rect (qy = |row centre - cy| - hy), the half-width (from cx) of the
 * region where the signed distance is <= k - r. Negative when the row has none. Used to find
 * the spans that are fully inside or fully outside, so only edge pixels need the SDF. */
static inline float rr_half(float qy, float hx, float k)
{
    if (qy <= 0) return qy <= k ? hx + k : -1.0f;
    if (k < 0 || qy > k) return -1.0f;
    return hx + sqrtf(k * k - qy * qy);
}

/* Pixels whose centres are within half-width hw of cx: [*p0, *p1]. Empty when *p0 > *p1. */
static inline void span(float cx, float hw, int *p0, int *p1)
{
    if (hw < 0) { *p0 = 1; *p1 = 0; return; }
    *p0 = (int)ceilf(cx - hw - 0.5f);
    *p1 = (int)floorf(cx + hw - 0.5f);
}

void gfx_fill_rrect(float x, float y, float w, float h, float r, uint32_t c, int a)
{
    int ix0, iy0, ix1, iy1, px, py;
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hx, hy;
    if (w <= 0 || h <= 0 || a <= 0) return;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0) r = 0;
    hx = w * 0.5f - r;
    hy = h * 0.5f - r;
    if (!bbox(x, y, x + w, y + h, &ix0, &iy0, &ix1, &iy1)) return;
    for (py = iy0; py < iy1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        float fy = py + 0.5f, qy = fabsf(fy - cy) - hy;
        int f0, f1, o0, o1, lo, hi;
        span(cx, rr_half(qy, hx, r - 0.5f), &f0, &f1);       /* fully covered */
        span(cx, rr_half(qy, hx, r + 0.5f), &o0, &o1);       /* touched at all */
        lo = o0 < ix0 ? ix0 : o0;
        hi = o1 > ix1 - 1 ? ix1 - 1 : o1;
        for (px = lo; px <= hi; px++) {
            if (px >= f0 && px <= f1) {
                int end = f1 < hi ? f1 : hi;
                if (a >= 255) for (; px <= end; px++) row[px] = c;
                else for (; px <= end; px++) blend(&row[px], c, a);
                px = end;
                continue;
            }
            {
                int k = cover(sd_rrect(px + 0.5f, fy, cx, cy, hx, hy, r), a);
                if (k) blend(&row[px], c, k);
            }
        }
    }
}

void gfx_stroke_rrect(float x, float y, float w, float h, float r, float t, uint32_t c, int a)
{
    int ix0, iy0, ix1, iy1, px, py;
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hx, hy, half = t * 0.5f;
    if (w <= 0 || h <= 0 || a <= 0) return;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0) r = 0;
    hx = w * 0.5f - r;
    hy = h * 0.5f - r;
    if (!bbox(x - half - 1, y - half - 1, x + w + half + 1, y + h + half + 1, &ix0, &iy0, &ix1, &iy1)) return;
    for (py = iy0; py < iy1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        float fy = py + 0.5f, qy = fabsf(fy - cy) - hy;
        int s0, s1, o0, o1, lo, hi;
        span(cx, rr_half(qy, hx, r - half - 0.5f), &s0, &s1);   /* inside the ring's hole: skip */
        span(cx, rr_half(qy, hx, r + half + 0.5f), &o0, &o1);   /* touched at all */
        lo = o0 < ix0 ? ix0 : o0;
        hi = o1 > ix1 - 1 ? ix1 - 1 : o1;
        for (px = lo; px <= hi; px++) {
            float d;
            int k;
            if (px >= s0 && px <= s1) { px = s1; continue; }
            d = fabsf(sd_rrect(px + 0.5f, fy, cx, cy, hx, hy, r)) - half;
            k = cover(d, a);
            if (k) blend(&row[px], c, k);
        }
    }
}

void gfx_circle(float cx, float cy, float r, uint32_t c, int a)
{
    int ix0, iy0, ix1, iy1, px, py;
    if (!bbox(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, &ix0, &iy0, &ix1, &iy1)) return;
    for (py = iy0; py < iy1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        float dy = py + 0.5f - cy;
        for (px = ix0; px < ix1; px++) {
            float dx = px + 0.5f - cx;
            int k = cover(sqrtf(dx * dx + dy * dy) - r, a);
            if (k) blend(&row[px], c, k);
        }
    }
}

void gfx_ellipse(float cx, float cy, float rx, float ry, uint32_t c, int a)
{
    int ix0, iy0, ix1, iy1, px, py;
    if (rx <= 0 || ry <= 0) return;
    if (!bbox(cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1, &ix0, &iy0, &ix1, &iy1)) return;
    for (py = iy0; py < iy1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        float dy = py + 0.5f - cy;
        for (px = ix0; px < ix1; px++) {
            float dx = px + 0.5f - cx;
            float ux = dx / rx, uy = dy / ry, vx = dx / (rx * rx), vy = dy / (ry * ry);
            float k0 = sqrtf(ux * ux + uy * uy), k1 = sqrtf(vx * vx + vy * vy);
            float d = k1 > 0 ? k0 * (k0 - 1.0f) / k1 : -fminf(rx, ry);   /* close to the true distance */
            int k = cover(d, a);
            if (k) blend(&row[px], c, k);
        }
    }
}

void gfx_ring(float cx, float cy, float r, float t, uint32_t c, int a)
{
    int ix0, iy0, ix1, iy1, px, py;
    float o = r + t * 0.5f + 1;
    if (!bbox(cx - o, cy - o, cx + o, cy + o, &ix0, &iy0, &ix1, &iy1)) return;
    for (py = iy0; py < iy1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        float dy = py + 0.5f - cy;
        for (px = ix0; px < ix1; px++) {
            float dx = px + 0.5f - cx;
            int k = cover(fabsf(sqrtf(dx * dx + dy * dy) - r) - t * 0.5f, a);
            if (k) blend(&row[px], c, k);
        }
    }
}

void gfx_arc(float cx, float cy, float r, float t, float a0, float a1, uint32_t c, int a)
{
    const float TAU = 6.2831853f;
    int ix0, iy0, ix1, iy1, px, py;
    float o = r + t * 0.5f + 1, span, e0x, e0y, e1x, e1y;
    if (a1 < a0) { float s = a0; a0 = a1; a1 = s; }
    span = a1 - a0;
    if (span >= TAU) { gfx_ring(cx, cy, r, t, c, a); return; }
    a0 = fmodf(a0, TAU);
    if (a0 < 0) a0 += TAU;
    e0x = cx + cosf(a0) * r; e0y = cy + sinf(a0) * r;
    e1x = cx + cosf(a0 + span) * r; e1y = cy + sinf(a0 + span) * r;
    if (!bbox(cx - o, cy - o, cx + o, cy + o, &ix0, &iy0, &ix1, &iy1)) return;
    for (py = iy0; py < iy1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        float dy = py + 0.5f - cy;
        for (px = ix0; px < ix1; px++) {
            float dx = px + 0.5f - cx, ang = atan2f(dy, dx), d;
            if (ang < 0) ang += TAU;
            ang -= a0;
            if (ang < 0) ang += TAU;
            if (ang <= span) {
                d = fabsf(sqrtf(dx * dx + dy * dy) - r) - t * 0.5f;
            } else {
                float ax = px + 0.5f - e0x, ay = py + 0.5f - e0y, bx = px + 0.5f - e1x, by = py + 0.5f - e1y;
                d = sqrtf(fminf(ax * ax + ay * ay, bx * bx + by * by)) - t * 0.5f;
            }
            {
                int k = cover(d, a);
                if (k) blend(&row[px], c, k);
            }
        }
    }
}

void gfx_line(float x0, float y0, float x1, float y1, float t, uint32_t c, int a)
{
    int ix0, iy0, ix1, iy1, px, py;
    float half = t * 0.5f, ex = x1 - x0, ey = y1 - y0, ee = ex * ex + ey * ey;
    if (!bbox(fminf(x0, x1) - half - 1, fminf(y0, y1) - half - 1,
              fmaxf(x0, x1) + half + 1, fmaxf(y0, y1) + half + 1, &ix0, &iy0, &ix1, &iy1)) return;
    for (py = iy0; py < iy1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        for (px = ix0; px < ix1; px++) {
            float vx = px + 0.5f - x0, vy = py + 0.5f - y0;
            float h = ee > 0 ? clampf((vx * ex + vy * ey) / ee, 0, 1) : 0;
            float qx = vx - ex * h, qy = vy - ey * h;
            int k = cover(sqrtf(qx * qx + qy * qy) - half, a);
            if (k) blend(&row[px], c, k);
        }
    }
}

void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c, int a)
{
    int ix0, iy0, ix1, iy1, px, py;
    float e0x = x1 - x0, e0y = y1 - y0, e1x = x2 - x1, e1y = y2 - y1, e2x = x0 - x2, e2y = y0 - y2;
    float s = (e0x * e2y - e0y * e2x) > 0 ? 1.0f : -1.0f;
    if (!bbox(fminf(x0, fminf(x1, x2)) - 1, fminf(y0, fminf(y1, y2)) - 1,
              fmaxf(x0, fmaxf(x1, x2)) + 1, fmaxf(y0, fmaxf(y1, y2)) + 1, &ix0, &iy0, &ix1, &iy1)) return;
    for (py = iy0; py < iy1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        for (px = ix0; px < ix1; px++) {
            float p_x = px + 0.5f, p_y = py + 0.5f;
            float v0x = p_x - x0, v0y = p_y - y0, v1x = p_x - x1, v1y = p_y - y1, v2x = p_x - x2, v2y = p_y - y2;
            float h0 = clampf((v0x * e0x + v0y * e0y) / (e0x * e0x + e0y * e0y), 0, 1);
            float h1 = clampf((v1x * e1x + v1y * e1y) / (e1x * e1x + e1y * e1y), 0, 1);
            float h2 = clampf((v2x * e2x + v2y * e2y) / (e2x * e2x + e2y * e2y), 0, 1);
            float q0x = v0x - e0x * h0, q0y = v0y - e0y * h0;
            float q1x = v1x - e1x * h1, q1y = v1y - e1y * h1;
            float q2x = v2x - e2x * h2, q2y = v2y - e2y * h2;
            float d0 = q0x * q0x + q0y * q0y, d1 = q1x * q1x + q1y * q1y, d2 = q2x * q2x + q2y * q2y;
            float s0 = s * (v0x * e0y - v0y * e0x), s1 = s * (v1x * e1y - v1y * e1x), s2 = s * (v2x * e2y - v2y * e2x);
            float dd = fminf(d0, fminf(d1, d2)), ss = fminf(s0, fminf(s1, s2));
            float d = -sqrtf(dd) * (ss > 0 ? 1.0f : -1.0f);
            int k = cover(d, a);
            if (k) blend(&row[px], c, k);
        }
    }
}

/* The signed distance to a polygon (any shape) gives each pixel its cover, so a shape made of
 * one polygon has no inner seams the way triangles side by side would. */
void gfx_poly(const float *xy, int n, float ox, float oy, float s, uint32_t c, int a)
{
    float px[16], py[16], x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    int i, j, ix0, iy0, ix1, iy1, X, Y;
    if (n < 3 || n > 16 || a <= 0) return;
    for (i = 0; i < n; i++) {
        px[i] = ox + xy[2 * i] * s;
        py[i] = oy + xy[2 * i + 1] * s;
        x0 = fminf(x0, px[i]); x1 = fmaxf(x1, px[i]);
        y0 = fminf(y0, py[i]); y1 = fmaxf(y1, py[i]);
    }
    if (!bbox(x0 - 1, y0 - 1, x1 + 1, y1 + 1, &ix0, &iy0, &ix1, &iy1)) return;
    for (Y = iy0; Y < iy1; Y++) {
        uint32_t *row = gfx_bb + (size_t)Y * SCR_W;
        float qy = Y + 0.5f;
        for (X = ix0; X < ix1; X++) {
            float qx = X + 0.5f, dd = 1e30f, sign = 1;
            int k;
            for (i = 0, j = n - 1; i < n; j = i++) {
                float ex = px[j] - px[i], ey = py[j] - py[i], wx = qx - px[i], wy = qy - py[i];
                float h = clampf((wx * ex + wy * ey) / (ex * ex + ey * ey), 0, 1);
                float bx = wx - ex * h, by = wy - ey * h, d2 = bx * bx + by * by;
                int up = qy >= py[i], below = qy < py[j], left = ex * wy > ey * wx;
                if (d2 < dd) dd = d2;
                if ((up && below && left) || (!up && !below && !left)) sign = -sign;
            }
            k = cover(sign * sqrtf(dd), a);         /* sign is -1 inside: an odd number of crossings */
            if (k) blend(&row[X], c, k);
        }
    }
}

void gfx_sector(float cx, float cy, float r, float t, float a0, float a1, uint32_t c, int a)
{
    const float Q = 1.5707963f;
    float ro = r + t * 0.5f, ri = r - t * 0.5f, x0, y0, x1, y1, n0x, n0y, n1x, n1y;
    int ix0, iy0, ix1, iy1, X, Y, q;
    if (a1 <= a0 || a <= 0) return;
    /* its box: both ends, and the edge wherever it passes 3, 6, 9 or 12 o'clock */
    x0 = x1 = cx + cosf(a0) * ri;
    y0 = y1 = cy + sinf(a0) * ri;
#define GROW(gx, gy) do { float gx_ = (gx), gy_ = (gy); \
        x0 = fminf(x0, gx_); x1 = fmaxf(x1, gx_); y0 = fminf(y0, gy_); y1 = fmaxf(y1, gy_); } while (0)
    GROW(cx + cosf(a0) * ro, cy + sinf(a0) * ro);
    GROW(cx + cosf(a1) * ri, cy + sinf(a1) * ri);
    GROW(cx + cosf(a1) * ro, cy + sinf(a1) * ro);
    for (q = (int)ceilf(a0 / Q); q * Q <= a1; q++) GROW(cx + cosf(q * Q) * ro, cy + sinf(q * Q) * ro);
#undef GROW
    if (!bbox(x0 - 1, y0 - 1, x1 + 1, y1 + 1, &ix0, &iy0, &ix1, &iy1)) return;
    /* each end is a line through the centre; these point away from the piece across it */
    n0x = sinf(a0); n0y = -cosf(a0);
    n1x = -sinf(a1); n1y = cosf(a1);
    for (Y = iy0; Y < iy1; Y++) {
        uint32_t *row = gfx_bb + (size_t)Y * SCR_W;
        float dy = Y + 0.5f - cy;
        for (X = ix0; X < ix1; X++) {
            float dx = X + 0.5f - cx;
            float band = fabsf(sqrtf(dx * dx + dy * dy) - r) - t * 0.5f;
            float ends = fmaxf(dx * n0x + dy * n0y, dx * n1x + dy * n1y);
            int k = cover(fmaxf(band, ends), a);
            if (k) blend(&row[X], c, k);
        }
    }
}

/* ----------------------------------------------------------------------- text */

static const struct font *F(int font)
{
    if (font < 0 || font >= FONT_COUNT) font = 0;
    return &font_table[font];
}

static const struct font_glyph *G(const struct font *f, char ch)
{
    unsigned char c = (unsigned char)ch;
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    return &f->glyphs[c - FONT_FIRST];
}

int gfx_line_h(int font) { const struct font *f = F(font); return f->ascent + f->descent; }
int gfx_cap_h(int font) { return F(font)->cap; }

int gfx_str_w(int font, const char *s)
{
    const struct font *f = F(font);
    int pen = 0;
    for (; *s; s++) pen += G(f, *s)->adv64;
    return (pen + 32) >> 6;
}

int gfx_str_a(int font, int x, int y, uint32_t c, int a, const char *s)
{
    const struct font *f = F(font);
    int pen = x * 64, base = y + f->ascent;    /* x can be off the left edge (a row sliding by) */
    for (; *s; s++) {
        const struct font_glyph *g = G(f, *s);
        int gx = ((pen + 32) >> 6) + g->x, gy = base + g->y, i, j;
        const uint8_t *al = f->alpha + g->off;
        for (j = 0; j < g->h; j++) {
            int py = gy + j;
            uint32_t *row;
            if (py < cy0 || py >= cy1) continue;
            row = gfx_bb + (size_t)py * SCR_W;
            for (i = 0; i < g->w; i++) {
                int px = gx + i, k = al[j * g->w + i];
                if (k && px >= cx0 && px < cx1) blend(&row[px], c, a >= 255 ? k : (k * a) / 255);
            }
        }
        pen += g->adv64;
    }
    return ((pen + 32) >> 6) - x;
}

int gfx_str(int font, int x, int y, uint32_t c, const char *s)
{
    return gfx_str_a(font, x, y, c, 255, s);
}

int gfx_str_fit(int font, int x, int y, uint32_t c, const char *s, int max_w)
{
    char buf[256];
    int n;
    if (gfx_str_w(font, s) <= max_w) return gfx_str(font, x, y, c, s);
    n = (int)strlen(s);
    if (n > (int)sizeof buf - 4) n = sizeof buf - 4;
    memcpy(buf, s, n);
    for (; n > 0; n--) {
        buf[n] = 0;
        while (n > 0 && buf[n - 1] == ' ') buf[--n] = 0;
        strcpy(buf + n, "...");
        if (gfx_str_w(font, buf) <= max_w) break;
    }
    return gfx_str(font, x, y, c, buf);
}

void gfx_str_c(int font, int cx, int y, uint32_t c, const char *s)
{
    gfx_str(font, cx - gfx_str_w(font, s) / 2, y, c, s);
}

void gfx_str_r(int font, int right_x, int y, uint32_t c, const char *s)
{
    gfx_str(font, right_x - gfx_str_w(font, s), y, c, s);
}

int gfx_str_vc(int font, int x, int cy, uint32_t c, const char *s)
{
    const struct font *f = F(font);
    return gfx_str(font, x, cy + f->cap / 2 - f->ascent, c, s);
}

int gfx_wrap(int font, int x, int y, int w, int gap, uint32_t c, const char *s, int max_lines)
{
    char line[256];
    int lines = 0, lh = gfx_line_h(font) + gap;
    while (*s && lines < max_lines) {
        int n = 0, last_space = -1;
        while (s[n] && s[n] != '\n' && n < (int)sizeof line - 1) {
            line[n] = s[n];
            line[n + 1] = 0;
            if (s[n] == ' ') last_space = n;
            if (gfx_str_w(font, line) > w && n > 0) {
                if (last_space > 0) n = last_space;
                break;
            }
            n++;
        }
        memcpy(line, s, n);
        line[n] = 0;
        if (lines == max_lines - 1 && s[n] && s[n] != '\n') gfx_str_fit(font, x, y, c, s, w);
        else gfx_str(font, x, y, c, line);
        s += n;
        while (*s == ' ' || *s == '\n') s++;
        y += lh;
        lines++;
    }
    return lines;
}

/* --------------------------------------------- 5x7 pixel font (brand wordmark) */

static const uint8_t pix[128][7] = {
    ['A'] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, ['B'] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    ['C'] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, ['D'] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    ['E'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, ['F'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    ['G'] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, ['H'] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    ['I'] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, ['J'] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
    ['K'] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, ['L'] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    ['M'] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, ['N'] = {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},
    ['O'] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, ['P'] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    ['Q'] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, ['R'] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    ['S'] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, ['T'] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    ['U'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, ['V'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    ['W'] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, ['X'] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    ['Y'] = {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}, ['Z'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    ['0'] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, ['1'] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    ['2'] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, ['3'] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
    ['4'] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, ['5'] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    ['6'] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, ['7'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    ['8'] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, ['9'] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, ['-'] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
};

int gfx_text_w(int s, const char *str)
{
    int n = (int)strlen(str);
    return n ? n * 6 * s - s : 0;
}

int gfx_text(int x, int y, int s, uint32_t c, const char *str)
{
    int x0 = x;
    for (; *str; str++, x += 6 * s) {
        unsigned char ch = (unsigned char)toupper((unsigned char)*str);
        const uint8_t *g = ch < 128 ? pix[ch] : pix[' '];
        int row, col;
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col))
                    gfx_rect(x + col * s, y + row * s, s, s, c);
    }
    return x - x0 - s;
}

int gfx_brand(int x, int y, int s, uint32_t main_color, uint32_t os_color)
{
    int w = gfx_text(x, y, s, main_color, "XENITH") + s;
    return w + gfx_text(x + w, y, s, os_color, "OS");
}

/* --------------------------------------------------------------------- images */

static inline uint32_t sample(const struct img *im, float sx, float sy)
{
    int x0, y0, x1, y1, fx, fy;
    uint32_t p00, p10, p01, p11, out = 0;
    int ch;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx > im->w - 1) sx = (float)(im->w - 1);
    if (sy > im->h - 1) sy = (float)(im->h - 1);
    x0 = (int)sx; y0 = (int)sy;
    x1 = x0 + 1 < im->w ? x0 + 1 : x0;
    y1 = y0 + 1 < im->h ? y0 + 1 : y0;
    fx = (int)((sx - x0) * 256); fy = (int)((sy - y0) * 256);
    p00 = im->px[y0 * im->w + x0]; p10 = im->px[y0 * im->w + x1];
    p01 = im->px[y1 * im->w + x0]; p11 = im->px[y1 * im->w + x1];
    for (ch = 0; ch < 32; ch += 8) {
        int a = (p00 >> ch) & 255, b = (p10 >> ch) & 255, c = (p01 >> ch) & 255, d = (p11 >> ch) & 255;
        int top = a + (((b - a) * fx) >> 8), bot = c + (((d - c) * fx) >> 8);
        out |= (uint32_t)((top + (((bot - top) * fy) >> 8)) & 255) << ch;
    }
    return out;
}

/* Draw an image at its own size, blending by its alpha (times a). */
static void blit(const struct img *im, int x, int y, int a)
{
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + im->w > cx1 ? cx1 : x + im->w, y1 = y + im->h > cy1 ? cy1 : y + im->h;
    int px, py;
    for (py = y0; py < y1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        const uint32_t *src = im->px + (size_t)(py - y) * im->w;
        for (px = x0; px < x1; px++) {
            uint32_t s = src[px - x];
            int k = (int)(s >> 24);
            if (a < 255) k = k * a / 255;
            if (k >= 255) row[px] = s & 0xffffff;
            else if (k) blend(&row[px], s & 0xffffff, k);
        }
    }
}

void gfx_mask(const struct img *m, int x, int y, uint32_t color, int a)
{
    int x0, y0, x1, y1, px, py;
    if (!m || !m->px) return;
    x0 = x < cx0 ? cx0 : x;
    y0 = y < cy0 ? cy0 : y;
    x1 = x + m->w > cx1 ? cx1 : x + m->w;
    y1 = y + m->h > cy1 ? cy1 : y + m->h;
    for (py = y0; py < y1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        const uint32_t *src = m->px + (size_t)(py - y) * m->w;
        for (px = x0; px < x1; px++) {
            int k = (int)(src[px - x] & 255);
            if (a < 255) k = k * a / 255;
            if (k >= 255) row[px] = color;
            else if (k) blend(&row[px], color, k);
        }
    }
}

/* A w x h copy of im: scaled with bilinear filtering, rounded corners baked into the alpha. */
static void make_scaled(const struct img *im, struct img *out, int w, int h, float r)
{
    int x, y;
    float sxs = (float)im->w / w, sys = (float)im->h / h, cx = w * 0.5f, cy = h * 0.5f, hx, hy;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0) r = 0;
    hx = w * 0.5f - r;
    hy = h * 0.5f - r;
    out->w = w;
    out->h = h;
    out->id = gfx_new_image_id();
    out->px = malloc((size_t)w * h * 4);
    if (!out->px) return;
    for (y = 0; y < h; y++) {
        float sy = (y + 0.5f) * sys - 0.5f;
        for (x = 0; x < w; x++) {
            uint32_t s = sample(im, (x + 0.5f) * sxs - 0.5f, sy);
            int k = r > 0 ? cover(sd_rrect(x + 0.5f, y + 0.5f, cx, cy, hx, hy, r), 255) : 255;
            out->px[(size_t)y * w + x] = ((uint32_t)((s >> 24) * k / 255) << 24) | (s & 0xffffff);
        }
    }
}

#define IMG_CACHE 40
#define IMG_CACHE_BYTES (24u * 1024 * 1024)
static struct { uint32_t src; int w, h, r; struct img im; uint32_t used; } icache[IMG_CACHE];
static uint32_t icache_clock;
static size_t icache_bytes;

static void icache_evict(void)
{
    int i, lru = -1;
    for (i = 0; i < IMG_CACHE; i++)
        if (icache[i].im.px && (lru < 0 || icache[i].used < icache[lru].used)) lru = i;
    if (lru < 0) return;
    icache_bytes -= (size_t)icache[lru].im.w * icache[lru].im.h * 4;
    free(icache[lru].im.px);
    memset(&icache[lru], 0, sizeof icache[lru]);
}

static const struct img *scaled_copy(const struct img *im, int w, int h, float r)
{
    int i, slot = -1, rk = (int)(r * 16 + 0.5f);
    size_t bytes = (size_t)w * h * 4;
    if (!im->id || bytes > IMG_CACHE_BYTES / 2) return NULL;
    for (i = 0; i < IMG_CACHE; i++)
        if (icache[i].im.px && icache[i].src == im->id && icache[i].w == w && icache[i].h == h && icache[i].r == rk) {
            icache[i].used = ++icache_clock;
            return &icache[i].im;
        }
    while (icache_bytes + bytes > IMG_CACHE_BYTES) icache_evict();
    for (i = 0; i < IMG_CACHE && slot < 0; i++) if (!icache[i].im.px) slot = i;
    if (slot < 0) {
        icache_evict();
        for (i = 0; i < IMG_CACHE && slot < 0; i++) if (!icache[i].im.px) slot = i;
    }
    make_scaled(im, &icache[slot].im, w, h, r);
    if (!icache[slot].im.px) return NULL;
    icache[slot].src = im->id;
    icache[slot].w = w;
    icache[slot].h = h;
    icache[slot].r = rk;
    icache[slot].used = ++icache_clock;
    icache_bytes += bytes;
    return &icache[slot].im;
}

static void image_draw_direct(const struct img *im, int x, int y, int w, int h, float r, int a);

static void image_draw(const struct img *im, int x, int y, int w, int h, float r, int a)
{
    const struct img *c;
    if (!im || !im->px || w <= 0 || h <= 0) return;
    if (w == im->w && h == im->h && r <= 0) { blit(im, x, y, a); return; }
    c = scaled_copy(im, w, h, r);
    if (c) blit(c, x, y, a);
    else image_draw_direct(im, x, y, w, h, r, a);
}

static void image_draw_direct(const struct img *im, int x, int y, int w, int h, float r, int a)
{
    int ix0, iy0, ix1, iy1, px, py;
    float sxs, sys, cx = x + w * 0.5f, cy = y + h * 0.5f, hx, hy;
    if (!im || !im->px || w <= 0 || h <= 0) return;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    hx = w * 0.5f - r;
    hy = h * 0.5f - r;
    sxs = (float)im->w / w;
    sys = (float)im->h / h;
    if (!bbox((float)x, (float)y, (float)(x + w), (float)(y + h), &ix0, &iy0, &ix1, &iy1)) return;
    for (py = iy0; py < iy1; py++) {
        uint32_t *row = gfx_bb + (size_t)py * SCR_W;
        float sy = (py - y + 0.5f) * sys - 0.5f;
        for (px = ix0; px < ix1; px++) {
            int k = a;
            uint32_t s;
            if (r > 0) k = cover(sd_rrect(px + 0.5f, py + 0.5f, cx, cy, hx, hy, r), a);
            if (!k) continue;
            s = sample(im, (px - x + 0.5f) * sxs - 0.5f, sy);
            k = k * (int)(s >> 24) / 255;
            if (k) blend(&row[px], s & 0xffffff, k);
        }
    }
}

void gfx_image(const struct img *im, int x, int y, int w, int h, int a)
{
    image_draw(im, x, y, w, h, 0, a);
}

void gfx_image_rounded(const struct img *im, int x, int y, int w, int h, float r, int a)
{
    image_draw(im, x, y, w, h, r, a);
}

void gfx_blit_scaled(const uint32_t *src, int sw, int sh, int dx, int dy, int dw, int dh)
{
    static int *col;
    static int col_sw, col_dw;
    int x, y, x0, x1;
    if (dw <= 0 || dh <= 0) return;
    if (col_sw != sw || col_dw != dw) {
        free(col);
        col = malloc(sizeof(int) * dw);
        for (x = 0; x < dw; x++) col[x] = x * sw / dw;
        col_sw = sw;
        col_dw = dw;
    }
    x0 = dx < 0 ? -dx : 0;
    x1 = dx + dw > SCR_W ? SCR_W - dx : dw;
    for (y = 0; y < dh; y++) {
        int py = dy + y;
        const uint32_t *srow;
        uint32_t *drow;
        if (py < 0 || py >= SCR_H) continue;
        srow = src + (size_t)(y * sh / dh) * sw;
        drow = gfx_bb + (size_t)py * SCR_W + dx;
        for (x = x0; x < x1; x++) drow[x] = srow[col[x]] & 0xffffff;
    }
}
