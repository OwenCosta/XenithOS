/* Shape-drawn icons. Coordinates are fractions of the icon size around its centre. */
#include <math.h>
#include <stdlib.h>

#include "gfx.h"
#include "icons.h"

static float X0, Y0, S;
static uint32_t FG, BGC;
#define PX(f) (X0 + (f) * S)
#define PY(f) (Y0 + (f) * S)
#define L(f) ((f) * S)

static void rr(float x0, float y0, float x1, float y1, float r, uint32_t c) { gfx_fill_rrect(PX(x0), PY(y0), L(x1 - x0), L(y1 - y0), L(r), c, 255); }
static void rs(float x0, float y0, float x1, float y1, float r, float t) { gfx_stroke_rrect(PX(x0), PY(y0), L(x1 - x0), L(y1 - y0), L(r), L(t), FG, 255); }
static void ln(float x0, float y0, float x1, float y1, float t) { gfx_line(PX(x0), PY(y0), PX(x1), PY(y1), L(t), FG, 255); }
static void ci(float x, float y, float r, uint32_t c) { gfx_circle(PX(x), PY(y), L(r), c, 255); }
static void ri(float x, float y, float r, float t) { gfx_ring(PX(x), PY(y), L(r), L(t), FG, 255); }
static void ar(float x, float y, float r, float t, float a0, float a1) { gfx_arc(PX(x), PY(y), L(r), L(t), a0, a1, FG, 255); }

static void icon_render(int icon, float cx, float cy, float size, uint32_t fg, uint32_t bg)
{
    const float PI = 3.14159265f;
    int i;
    X0 = cx; Y0 = cy; S = size; FG = fg; BGC = bg;

    switch (icon) {
    case ICON_APPS:
        rr(-0.42f, -0.42f, -0.06f, -0.06f, 0.09f, FG);
        rr(0.06f, -0.42f, 0.42f, -0.06f, 0.09f, FG);
        rr(-0.42f, 0.06f, -0.06f, 0.42f, 0.09f, FG);
        rr(0.06f, 0.06f, 0.42f, 0.42f, 0.09f, FG);
        break;
    case ICON_STORE:
        ar(0, -0.14f, 0.17f, 0.075f, PI, 2 * PI);
        rr(-0.38f, -0.16f, 0.38f, 0.42f, 0.09f, FG);
        ci(-0.17f, 0.02f, 0.04f, BGC);
        ci(0.17f, 0.02f, 0.04f, BGC);
        break;
    case ICON_CLOUD:
        ci(-0.18f, 0.08f, 0.20f, FG);
        ci(0.08f, -0.06f, 0.26f, FG);
        ci(0.27f, 0.12f, 0.17f, FG);
        rr(-0.40f, 0.06f, 0.44f, 0.29f, 0.11f, FG);
        break;
    case ICON_PROFILE: {
        int clip[4];
        ci(0, -0.17f, 0.19f, FG);
        gfx_get_clip(clip);
        gfx_clip_intersect((int)PX(-0.5f), (int)PY(-0.5f), (int)L(1), (int)L(0.92f));
        gfx_ellipse(PX(0), PY(0.40f), L(0.36f), L(0.26f), FG, 255);
        gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
        break;
    }
    case ICON_SETTINGS:
        for (i = 0; i < 8; i++) {
            float a = i * PI / 4;
            ln(cosf(a) * 0.27f, sinf(a) * 0.27f, cosf(a) * 0.38f, sinf(a) * 0.38f, 0.14f);
        }
        ri(0, 0, 0.22f, 0.13f);
        break;
    case ICON_POWER:
        ar(0, 0.03f, 0.32f, 0.085f, -PI / 2 + 0.62f, 1.5f * PI - 0.62f);
        ln(0, -0.42f, 0, -0.06f, 0.085f);
        break;
    case ICON_SYSTEM:
        rs(-0.40f, -0.32f, 0.40f, 0.18f, 0.06f, 0.075f);
        ln(0, 0.20f, 0, 0.33f, 0.075f);
        ln(-0.20f, 0.36f, 0.20f, 0.36f, 0.075f);
        break;
    case ICON_CONTROLLER:
        ci(-0.27f, 0.13f, 0.15f, FG);
        ci(0.27f, 0.13f, 0.15f, FG);
        rr(-0.43f, -0.20f, 0.43f, 0.16f, 0.16f, FG);
        gfx_line(PX(-0.30f), PY(-0.04f), PX(-0.14f), PY(-0.04f), L(0.055f), BGC, 255);
        gfx_line(PX(-0.22f), PY(-0.12f), PX(-0.22f), PY(0.04f), L(0.055f), BGC, 255);
        ci(0.20f, -0.09f, 0.035f, BGC);
        ci(0.28f, -0.01f, 0.035f, BGC);
        break;
    case ICON_DOWNLOAD:
        ln(0, -0.40f, 0, 0.12f, 0.09f);
        ln(-0.19f, -0.07f, 0, 0.14f, 0.09f);
        ln(0.19f, -0.07f, 0, 0.14f, 0.09f);
        ln(-0.34f, 0.37f, 0.34f, 0.37f, 0.09f);
        break;
    case ICON_CHECK:
        ln(-0.31f, 0.02f, -0.09f, 0.24f, 0.11f);
        ln(-0.09f, 0.24f, 0.33f, -0.21f, 0.11f);
        break;
    case ICON_SOUND:
        rr(-0.40f, -0.12f, -0.20f, 0.12f, 0.03f, FG);
        gfx_tri(PX(-0.24f), PY(-0.12f), PX(0.02f), PY(-0.34f), PX(0.02f), PY(0.0f), FG, 255);
        gfx_tri(PX(-0.24f), PY(0.12f), PX(0.02f), PY(0.34f), PX(0.02f), PY(0.0f), FG, 255);
        ar(0.06f, 0, 0.18f, 0.06f, -0.85f, 0.85f);
        ar(0.06f, 0, 0.33f, 0.06f, -0.85f, 0.85f);
        break;
    case ICON_NETWORK:
        rr(-0.38f, 0.14f, -0.20f, 0.38f, 0.04f, FG);
        rr(-0.09f, -0.06f, 0.09f, 0.38f, 0.04f, FG);
        rr(0.20f, -0.30f, 0.38f, 0.38f, 0.04f, FG);
        break;
    case ICON_INFO:
        ri(0, 0, 0.38f, 0.075f);
        ci(0, -0.17f, 0.05f, FG);
        ln(0, -0.03f, 0, 0.20f, 0.085f);
        break;
    case ICON_PLAY:
        gfx_tri(PX(-0.22f), PY(-0.34f), PX(0.36f), PY(0), PX(-0.22f), PY(0.34f), FG, 255);
        break;
    case ICON_PERSON_ADD:
        ci(-0.10f, -0.17f, 0.17f, FG);
        gfx_ellipse(PX(-0.10f), PY(0.38f), L(0.31f), L(0.22f), FG, 255);
        ln(0.28f, -0.16f, 0.28f, 0.10f, 0.07f);
        ln(0.15f, -0.03f, 0.41f, -0.03f, 0.07f);
        break;
    case ICON_SIGN_OUT:
        rs(-0.40f, -0.36f, 0.04f, 0.36f, 0.06f, 0.075f);
        ln(-0.10f, 0, 0.40f, 0, 0.08f);
        ln(0.24f, -0.15f, 0.40f, 0, 0.08f);
        ln(0.24f, 0.15f, 0.40f, 0, 0.08f);
        break;
    case ICON_MAIL:
        rs(-0.40f, -0.28f, 0.40f, 0.28f, 0.06f, 0.075f);
        ln(-0.33f, -0.20f, 0, 0.06f, 0.075f);
        ln(0.33f, -0.20f, 0, 0.06f, 0.075f);
        break;
    case ICON_TROPHY:
        ar(-0.25f, -0.14f, 0.11f, 0.065f, PI / 2, 1.5f * PI);
        ar(0.25f, -0.14f, 0.11f, 0.065f, -PI / 2, PI / 2);
        rr(-0.24f, -0.36f, 0.24f, 0.06f, 0.14f, FG);
        ln(0, 0.06f, 0, 0.26f, 0.08f);
        rr(-0.20f, 0.26f, 0.20f, 0.38f, 0.04f, FG);
        break;
    case ICON_SERVER:
        for (i = 0; i < 3; i++) {
            float y = -0.36f + i * 0.26f;
            rr(-0.38f, y, 0.38f, y + 0.20f, 0.05f, FG);
            ci(0.24f, y + 0.10f, 0.035f, BGC);
        }
        break;
    case ICON_KEY:
        ri(-0.18f, -0.06f, 0.16f, 0.08f);
        ln(-0.02f, 0.04f, 0.38f, 0.30f, 0.08f);
        ln(0.20f, 0.18f, 0.12f, 0.31f, 0.07f);
        ln(0.32f, 0.26f, 0.25f, 0.37f, 0.07f);
        break;
    case ICON_PALETTE:
        ci(0, 0, 0.40f, FG);
        ci(0.14f, 0.16f, 0.09f, BGC);
        ci(-0.17f, -0.12f, 0.055f, BGC);
        ci(0.04f, -0.20f, 0.055f, BGC);
        ci(0.20f, -0.06f, 0.055f, BGC);
        break;
    case ICON_REFRESH:
        ar(0, 0, 0.30f, 0.08f, -PI * 0.35f, PI * 1.45f);
        gfx_tri(PX(0.17f), PY(-0.41f), PX(0.36f), PY(-0.22f), PX(0.12f), PY(-0.16f), FG, 255);
        break;
    case ICON_HOME:
        ln(-0.40f, 0.0f, 0, -0.36f, 0.08f);
        ln(0, -0.36f, 0.40f, 0.0f, 0.08f);
        rs(-0.27f, -0.08f, 0.27f, 0.38f, 0.05f, 0.075f);
        rr(-0.08f, 0.14f, 0.08f, 0.38f, 0.03f, FG);
        break;
    case ICON_CLOSE:
        ln(-0.28f, -0.28f, 0.28f, 0.28f, 0.09f);
        ln(0.28f, -0.28f, -0.28f, 0.28f, 0.09f);
        break;
    case ICON_SWITCH:                       /* two arrows, one each way */
        ln(-0.36f, -0.17f, 0.34f, -0.17f, 0.075f);
        ln(0.20f, -0.31f, 0.36f, -0.17f, 0.075f);
        ln(0.20f, -0.03f, 0.36f, -0.17f, 0.075f);
        ln(0.36f, 0.17f, -0.34f, 0.17f, 0.075f);
        ln(-0.20f, 0.03f, -0.36f, 0.17f, 0.075f);
        ln(-0.20f, 0.31f, -0.36f, 0.17f, 0.075f);
        break;
    case ICON_POINTER:                      /* a mouse pointer */
        gfx_tri(PX(-0.26f), PY(-0.40f), PX(-0.26f), PY(0.24f), PX(0.26f), PY(0.08f), FG, 255);
        ln(-0.06f, 0.10f, 0.10f, 0.40f, 0.11f);
        break;
    case ICON_FRIENDS: {                    /* two people, one a little behind the other */
        int clip[4];
        gfx_get_clip(clip);
        gfx_clip_intersect((int)PX(-0.5f), (int)PY(-0.5f), (int)L(1), (int)L(0.92f));
        ci(0.19f, -0.19f, 0.15f, FG);
        gfx_ellipse(PX(0.19f), PY(0.30f), L(0.27f), L(0.21f), FG, 255);
        ci(-0.09f, -0.10f, 0.215f, BGC);    /* a gap around the one in front */
        gfx_ellipse(PX(-0.09f), PY(0.45f), L(0.38f), L(0.29f), BGC, 255);
        ci(-0.09f, -0.10f, 0.16f, FG);
        gfx_ellipse(PX(-0.09f), PY(0.45f), L(0.32f), L(0.23f), FG, 255);
        gfx_set_clip(clip[0], clip[1], clip[2], clip[3]);
        break;
    }
    }
}

/* Each icon is drawn once per size (and quarter-pixel position) as a white-on-black
 * coverage mask, then stamped in any colour. Cut-outs come out see-through. */
#define ICON_CACHE 64
static struct { int icon, size_q, frac; struct img m; uint32_t used; } cache[ICON_CACHE];
static uint32_t clock_;

static const struct img *icon_mask(int icon, int size_q, int fx, int fy, int side)
{
    int i, slot = -1, frac = fx * 4 + fy;
    uint32_t *buf;
    for (i = 0; i < ICON_CACHE; i++)
        if (cache[i].m.px && cache[i].icon == icon && cache[i].size_q == size_q && cache[i].frac == frac) {
            cache[i].used = ++clock_;
            return &cache[i].m;
        }
    buf = calloc((size_t)side * side, 4);
    if (!buf) return NULL;
    if (!gfx_target(buf, side, side)) { free(buf); return NULL; }
    icon_render(icon, side / 2 + fx * 0.25f, side / 2 + fy * 0.25f, size_q * 0.25f, 0xFFFFFF, 0x000000);
    gfx_target(NULL, 0, 0);

    for (i = 0; i < ICON_CACHE; i++) {
        if (!cache[i].m.px) { slot = i; break; }
        if (slot < 0 || cache[i].used < cache[slot].used) slot = i;
    }
    free(cache[slot].m.px);
    cache[slot].icon = icon;
    cache[slot].size_q = size_q;
    cache[slot].frac = frac;
    cache[slot].m.w = side;
    cache[slot].m.h = side;
    cache[slot].m.px = buf;
    cache[slot].m.id = gfx_new_image_id();
    cache[slot].used = ++clock_;
    return &cache[slot].m;
}

void icon_draw(int icon, float cx, float cy, float size, uint32_t fg, uint32_t bg)
{
    int ix = (int)floorf(cx), iy = (int)floorf(cy);
    int fx = (int)((cx - ix) * 4), fy = (int)((cy - iy) * 4);
    int size_q = (int)(size * 4 + 0.5f), side;
    const struct img *m;
    if (fx > 3) fx = 3;
    if (fy > 3) fy = 3;
    side = (int)ceilf(size_q * 0.25f * 1.4f) + 6;          /* some shapes reach past the box */
    if (icon < 0 || icon >= ICON_COUNT || size_q <= 0 || side > 400) { icon_render(icon, cx, cy, size, fg, bg); return; }
    m = icon_mask(icon, size_q, fx, fy, side);
    if (m) gfx_mask(m, ix - side / 2, iy - side / 2, fg, 255);
    else icon_render(icon, cx, cy, size, fg, bg);
}
