/* XenithOS avatars. Mirror any change here in server/web/avatar.js. */
#include <math.h>
#include <string.h>

#include "avatar.h"
#include "gfx.h"

const int avatar_count[AV_PARTS] = {8, 8, 8, 5, 5, 4, 10, 6, 8};
const char *const avatar_part_name[AV_PARTS] = {
    "Skin", "Hair", "Hair color", "Eyes", "Mouth", "Top", "Top color", "Extra", "Background",
};
static const int defaults[AV_PARTS] = {2, 0, 2, 0, 0, 1, 0, 0, 0};

static const uint32_t SKIN[8] = {0xF7D7C4, 0xEEC1A0, 0xE0A47E, 0xC68863, 0xA86B47, 0x8A5235, 0x6A3C25, 0x4A2A1A};
static const uint32_t HAIR[8] = {0x1C1A1A, 0x3A2618, 0x6B4429, 0xA8743F, 0xDDB66B, 0xB4502A, 0xA9ADB3, 0x3D7BFF};
static const uint32_t TOP[10] = {0x2F6BFF, 0xE04A3F, 0x2EA866, 0xF0B429, 0xEDEFF5, 0x24272F, 0x7B8494, 0xF07A2E, 0x7A55D6, 0x1FA6A0};
static const uint32_t BG[8] = {0x1B3A6B, 0x2F6BFF, 0x157A73, 0x2E7D4F, 0xC2672A, 0xA23B3B, 0x5B4A9E, 0x3C4352};
#define INK 0x2A2224

static const char *const NAMES[AV_PARTS][10] = {
    {"Porcelain", "Light", "Warm", "Tan", "Medium", "Brown", "Deep", "Dark"},
    {"Short", "Buzz", "Long", "Curly", "Spiky", "Bun", "Swoop", "None"},
    {"Black", "Dark brown", "Brown", "Light brown", "Blonde", "Ginger", "Gray", "Blue"},
    {"Dots", "Happy", "Sleepy", "Wide", "Wink"},
    {"Smile", "Grin", "Flat", "Surprised", "Smirk"},
    {"T-shirt", "Hoodie", "Collar", "Jacket"},
    {"Blue", "Red", "Green", "Yellow", "White", "Black", "Gray", "Orange", "Violet", "Teal"},
    {"None", "Glasses", "Shades", "Headphones", "Cap", "Beanie"},
    {"Navy", "Blue", "Teal", "Green", "Orange", "Red", "Violet", "Slate"},
};

const char *avatar_option_name(int part, int value)
{
    if (part < 0 || part >= AV_PARTS || value < 0 || value >= avatar_count[part]) return "?";
    return NAMES[part][value];
}

static int digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    return -1;
}

int avatar_valid(const char *code)
{
    int i;
    if (!code || strlen(code) != AVATAR_CODE_LEN || code[0] != '1') return 0;
    for (i = 0; i < AV_PARTS; i++) {
        int d = digit(code[i + 1]);
        if (d < 0 || d >= avatar_count[i]) return 0;
    }
    return 1;
}

void avatar_parse(const char *code, int v[AV_PARTS])
{
    int i, ok = code && strlen(code) == AVATAR_CODE_LEN && code[0] == '1';
    for (i = 0; i < AV_PARTS; i++) {
        int d = ok ? digit(code[i + 1]) : -1;
        v[i] = (d >= 0 && d < avatar_count[i]) ? d : defaults[i];
    }
}

void avatar_encode(const int v[AV_PARTS], char out[AVATAR_CODE_LEN + 1])
{
    static const char D[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    int i;
    out[0] = '1';
    for (i = 0; i < AV_PARTS; i++) {
        int d = v[i] % avatar_count[i];
        out[i + 1] = D[d < 0 ? 0 : d];
    }
    out[AVATAR_CODE_LEN] = 0;
}

void avatar_random(int v[AV_PARTS], uint32_t seed)
{
    int i;
    for (i = 0; i < AV_PARTS; i++) {
        seed = seed * 1103515245u + 12345u;
        v[i] = (int)((seed >> 16) % (uint32_t)avatar_count[i]);
    }
    if (v[AV_ACCESSORY] >= 4 && seed % 3) v[AV_ACCESSORY] = 0;   /* hats less often */
}

uint32_t avatar_bg_color(const char *code)
{
    int v[AV_PARTS];
    avatar_parse(code, v);
    return BG[v[AV_BACKGROUND]];
}

/* ----------------------------------------------------------------- drawing */

static float ox, oy, sz;
#define PX(f) (ox + (f) * sz)
#define PY(f) (oy + (f) * sz)
#define L(f) ((f) * sz)

static void ell(float cx, float cy, float rx, float ry, uint32_t c) { gfx_ellipse(PX(cx), PY(cy), L(rx), L(ry), c, 255); }
static void circ(float cx, float cy, float r, uint32_t c) { gfx_circle(PX(cx), PY(cy), L(r), c, 255); }
static void rr(float x, float y, float w, float h, float r, uint32_t c) { gfx_fill_rrect(PX(x), PY(y), L(w), L(h), L(r), c, 255); }
static void ln(float x0, float y0, float x1, float y1, float t, uint32_t c) { gfx_line(PX(x0), PY(y0), PX(x1), PY(y1), L(t), c, 255); }
static void arc(float cx, float cy, float r, float t, float a0, float a1, uint32_t c) { gfx_arc(PX(cx), PY(cy), L(r), L(t), a0, a1, c, 255); }

/* Draw only above (or below) a horizontal line, inside the avatar's own clip. */
static int saved[4];
static void clip_above(float fy) { gfx_get_clip(saved); gfx_clip_intersect((int)ox, (int)oy, (int)sz, (int)(PY(fy) - oy)); }
static void clip_below(float fy) { gfx_get_clip(saved); gfx_clip_intersect((int)ox, (int)PY(fy), (int)sz, (int)(oy + sz - PY(fy))); }
static void clip_restore(void) { gfx_set_clip(saved[0], saved[1], saved[2], saved[3]); }

static void hair_cap(float cut, uint32_t c)
{
    clip_above(cut);
    ell(0.5f, 0.355f, 0.205f, 0.145f, c);
    clip_restore();
}

void avatar_draw_v(const int v[AV_PARTS], int x, int y, int size)
{
    const float PI = 3.14159265f;
    uint32_t skin = SKIN[v[AV_SKIN]], hair = HAIR[v[AV_HAIR_COLOR]], top = TOP[v[AV_TOP_COLOR]];
    uint32_t skin_dark = gfx_shade(skin, -14), top_dark = gfx_shade(top, -22);
    int style = v[AV_HAIR], acc = v[AV_ACCESSORY], outer[4];
    int hat = acc == 4 || acc == 5;

    ox = (float)x; oy = (float)y; sz = (float)size;
    gfx_get_clip(outer);
    gfx_fill_rrect(PX(0), PY(0), L(1), L(1), L(0.22f), BG[v[AV_BACKGROUND]], 255);
    gfx_clip_intersect(x, y, size, size);

    /* Behind the head */
    if (style == 2) rr(0.26f, 0.26f, 0.48f, 0.54f, 0.18f, hair);
    if (v[AV_TOP] == 1) rr(0.30f, 0.66f, 0.40f, 0.16f, 0.08f, top_dark);           /* hood */

    /* Body */
    rr(0.20f, 0.74f, 0.60f, 0.40f, 0.16f, top);
    rr(0.44f, 0.58f, 0.12f, 0.20f, 0.04f, skin_dark);                                /* neck */
    switch (v[AV_TOP]) {
    case 1:                                                                          /* hoodie strings */
        ln(0.46f, 0.80f, 0.45f, 0.92f, 0.012f, 0xE8ECF4);
        ln(0.54f, 0.80f, 0.55f, 0.92f, 0.012f, 0xE8ECF4);
        break;
    case 2:                                                                          /* collar */
        gfx_tri(PX(0.43f), PY(0.74f), PX(0.50f), PY(0.81f), PX(0.39f), PY(0.83f), 0xF4F6FA, 255);
        gfx_tri(PX(0.57f), PY(0.74f), PX(0.50f), PY(0.81f), PX(0.61f), PY(0.83f), 0xF4F6FA, 255);
        break;
    case 3:                                                                          /* jacket */
        rr(0.44f, 0.76f, 0.12f, 0.30f, 0.0f, 0xE8ECF4);
        ln(0.44f, 0.76f, 0.40f, 0.95f, 0.02f, top_dark);
        ln(0.56f, 0.76f, 0.60f, 0.95f, 0.02f, top_dark);
        break;
    }

    /* Head */
    circ(0.305f, 0.47f, 0.045f, skin);
    circ(0.695f, 0.47f, 0.045f, skin);
    ell(0.5f, 0.45f, 0.195f, 0.225f, skin);

    /* Hair in front */
    if (!hat) {
        switch (style) {
        case 0:
            hair_cap(0.385f, hair);
            rr(0.305f, 0.36f, 0.04f, 0.10f, 0.02f, hair);
            rr(0.655f, 0.36f, 0.04f, 0.10f, 0.02f, hair);
            break;
        case 1:
            hair_cap(0.33f, gfx_mix(hair, skin, 90));
            break;
        case 2:
            hair_cap(0.37f, hair);
            rr(0.29f, 0.33f, 0.07f, 0.38f, 0.03f, hair);
            rr(0.64f, 0.33f, 0.07f, 0.38f, 0.03f, hair);
            break;
        case 3: {
            int i;
            hair_cap(0.36f, hair);
            for (i = 0; i < 8; i++) {
                float a = PI * (200 + i * 20) / 180.0f;
                circ(0.5f + 0.19f * cosf(a), 0.40f + 0.17f * sinf(a), 0.075f, hair);
            }
            break;
        }
        case 4: {
            int i;
            hair_cap(0.36f, hair);
            for (i = 0; i < 5; i++) {
                float bx = 0.33f + i * 0.085f;
                gfx_tri(PX(bx), PY(0.29f), PX(bx + 0.085f), PY(0.29f), PX(bx + 0.03f), PY(0.15f + (i % 2) * 0.03f), hair, 255);
            }
            break;
        }
        case 5:
            circ(0.5f, 0.19f, 0.075f, hair);
            hair_cap(0.38f, hair);
            break;
        case 6:
            hair_cap(0.36f, hair);
            ln(0.33f, 0.33f, 0.60f, 0.25f, 0.09f, hair);
            break;
        default:
            break;
        }
    } else if (style == 2) {
        rr(0.29f, 0.33f, 0.07f, 0.38f, 0.03f, hair);
        rr(0.64f, 0.33f, 0.07f, 0.38f, 0.03f, hair);
    }

    /* Face */
    {
        uint32_t brow = style == 7 ? gfx_shade(skin, -40) : hair;
        ln(0.41f, 0.405f, 0.46f, 0.40f, 0.016f, brow);
        ln(0.54f, 0.40f, 0.59f, 0.405f, 0.016f, brow);
    }
    switch (v[AV_EYES]) {
    case 1:
        arc(0.43f, 0.48f, 0.028f, 0.014f, PI + 0.35f, 2 * PI - 0.35f, INK);
        arc(0.57f, 0.48f, 0.028f, 0.014f, PI + 0.35f, 2 * PI - 0.35f, INK);
        break;
    case 2:
        ln(0.405f, 0.47f, 0.455f, 0.47f, 0.014f, INK);
        ln(0.545f, 0.47f, 0.595f, 0.47f, 0.014f, INK);
        break;
    case 3:
        circ(0.43f, 0.465f, 0.034f, 0xFFFFFF);
        circ(0.57f, 0.465f, 0.034f, 0xFFFFFF);
        circ(0.43f, 0.469f, 0.019f, INK);
        circ(0.57f, 0.469f, 0.019f, INK);
        break;
    case 4:
        circ(0.43f, 0.465f, 0.022f, INK);
        arc(0.57f, 0.48f, 0.028f, 0.014f, PI + 0.35f, 2 * PI - 0.35f, INK);
        break;
    default:
        circ(0.43f, 0.465f, 0.022f, INK);
        circ(0.57f, 0.465f, 0.022f, INK);
        break;
    }
    circ(0.5f, 0.52f, 0.012f, gfx_shade(skin, -18));
    switch (v[AV_MOUTH]) {
    case 1:
        clip_below(0.555f);
        circ(0.5f, 0.555f, 0.05f, 0x6B2A2A);
        clip_restore();
        rr(0.465f, 0.555f, 0.07f, 0.014f, 0.004f, 0xFFFFFF);
        break;
    case 2:
        ln(0.465f, 0.57f, 0.535f, 0.57f, 0.014f, 0x6B2A2A);
        break;
    case 3:
        ell(0.5f, 0.572f, 0.022f, 0.028f, 0x6B2A2A);
        break;
    case 4:
        arc(0.52f, 0.55f, 0.04f, 0.014f, 0.45f, 1.9f, 0x6B2A2A);
        break;
    default:
        arc(0.5f, 0.55f, 0.045f, 0.014f, 0.35f, PI - 0.35f, 0x6B2A2A);
        break;
    }

    /* Accessories */
    switch (acc) {
    case 1:
        gfx_ring(PX(0.43f), PY(0.465f), L(0.05f), L(0.012f), 0x23262E, 255);
        gfx_ring(PX(0.57f), PY(0.465f), L(0.05f), L(0.012f), 0x23262E, 255);
        ln(0.48f, 0.462f, 0.52f, 0.462f, 0.012f, 0x23262E);
        ln(0.38f, 0.46f, 0.31f, 0.45f, 0.012f, 0x23262E);
        ln(0.62f, 0.46f, 0.69f, 0.45f, 0.012f, 0x23262E);
        break;
    case 2:
        rr(0.375f, 0.44f, 0.11f, 0.055f, 0.02f, 0x15151A);
        rr(0.515f, 0.44f, 0.11f, 0.055f, 0.02f, 0x15151A);
        ln(0.48f, 0.455f, 0.52f, 0.455f, 0.014f, 0x15151A);
        break;
    case 3:
        arc(0.5f, 0.45f, 0.235f, 0.035f, PI + 0.15f, 2 * PI - 0.15f, 0x2B2F3A);
        rr(0.255f, 0.40f, 0.075f, 0.14f, 0.03f, 0x2B2F3A);
        rr(0.67f, 0.40f, 0.075f, 0.14f, 0.03f, 0x2B2F3A);
        rr(0.268f, 0.425f, 0.05f, 0.09f, 0.02f, top);
        rr(0.682f, 0.425f, 0.05f, 0.09f, 0.02f, top);
        break;
    case 4:
        clip_above(0.365f);
        ell(0.5f, 0.35f, 0.215f, 0.16f, top);
        clip_restore();
        rr(0.33f, 0.335f, 0.42f, 0.045f, 0.02f, top_dark);
        break;
    case 5:
        rr(0.29f, 0.17f, 0.42f, 0.21f, 0.10f, top);
        rr(0.285f, 0.32f, 0.43f, 0.06f, 0.03f, top_dark);
        break;
    }

    gfx_set_clip(outer[0], outer[1], outer[2], outer[3]);
}

void avatar_draw(const char *code, int x, int y, int size)
{
    int v[AV_PARTS];
    avatar_parse(code, v);
    avatar_draw_v(v, x, y, size);
}
