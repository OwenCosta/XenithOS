/* XenithOS avatars. Mirror any change here in server/web/static/avatar.js and the counts in
 * server/xenith_cloud.py. Options are only ever added at the end of a list, so an old code
 * keeps drawing the same avatar. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "avatar.h"
#include "gfx.h"

const int avatar_count[AV_PARTS] = {10, 14, 13, 8, 10, 8, 12, 11, 12, 6, 5, 5, 3};
static const int v1_count[AV_PARTS_V1] = {8, 8, 8, 5, 5, 4, 10, 6, 8};
const char *const avatar_part_name[AV_PARTS] = {
    "Skin", "Hair", "Hair color", "Eyes", "Mouth", "Top", "Top color", "Extra", "Background",
    "Eye color", "Eyebrows", "Facial hair", "Cheeks",
};
static const int defaults[AV_PARTS] = {2, 0, 2, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};

static const uint32_t SKIN[10] = {0xF7D7C4, 0xEEC1A0, 0xE0A47E, 0xC68863, 0xA86B47, 0x8A5235, 0x6A3C25, 0x4A2A1A,
                                  0xC9A27A, 0xF2C4B4};
static const uint32_t HAIR[13] = {0x1C1A1A, 0x3A2618, 0x6B4429, 0xA8743F, 0xDDB66B, 0xB4502A, 0xA9ADB3, 0x3D7BFF,
                                  0xE87BB0, 0x7B4FD0, 0x3FA35B, 0xEDEDED, 0xC0392B};
static const uint32_t TOP[12] = {0x2F6BFF, 0xE04A3F, 0x2EA866, 0xF0B429, 0xEDEFF5, 0x24272F, 0x7B8494, 0xF07A2E,
                                 0x7A55D6, 0x1FA6A0, 0xE86AA6, 0x22346B};
static const uint32_t BG[12] = {0x1B3A6B, 0x2F6BFF, 0x157A73, 0x2E7D4F, 0xC2672A, 0xA23B3B, 0x5B4A9E, 0x3C4352,
                                0xB04A7A, 0xB8862B, 0x2E9C86, 0x24262C};
static const uint32_t EYE[6] = {0x2A2224, 0x6B3E1E, 0x2F6FD0, 0x2E8B57, 0x8E7240, 0x6E7B8B};
#define INK 0x2A2224
#define MOUTH 0x6B2A2A
#define GOLD 0xE8B83A

static const char *const NAMES[AV_PARTS][14] = {
    {"Porcelain", "Light", "Warm", "Tan", "Medium", "Brown", "Deep", "Dark", "Olive", "Rosy"},
    {"Short", "Buzz", "Long", "Curly", "Spiky", "Bun", "Swoop", "None", "Afro", "Mohawk", "Ponytail", "Bob",
     "Side part", "Pigtails"},
    {"Black", "Dark brown", "Brown", "Light brown", "Blonde", "Ginger", "Gray", "Blue", "Pink", "Purple",
     "Green", "White", "Red"},
    {"Dots", "Happy", "Sleepy", "Wide", "Wink", "Grumpy", "Sparkle", "Focused"},
    {"Smile", "Grin", "Flat", "Surprised", "Smirk", "Laugh", "Tongue out", "Frown", "Teeth", "Cat"},
    {"T-shirt", "Hoodie", "Collar", "Jacket", "Sweater", "Stripes", "Jersey", "Tank top"},
    {"Blue", "Red", "Green", "Yellow", "White", "Black", "Gray", "Orange", "Violet", "Teal", "Pink", "Navy"},
    {"None", "Glasses", "Shades", "Headphones", "Cap", "Beanie", "Headband", "Crown", "Bow", "Earrings",
     "Round glasses"},
    {"Navy", "Blue", "Teal", "Green", "Orange", "Red", "Violet", "Slate", "Pink", "Gold", "Mint", "Charcoal"},
    {"Dark", "Brown", "Blue", "Green", "Hazel", "Gray"},
    {"Normal", "Thick", "Raised", "Cross", "Thin"},
    {"None", "Stubble", "Mustache", "Beard", "Goatee"},
    {"None", "Blush", "Freckles"},
};

const char *avatar_option_name(int part, int value)
{
    if (part < 0 || part >= AV_PARTS || value < 0 || value >= avatar_count[part]) return "?";
    return NAMES[part][value];
}

int avatar_part_is_color(int part)
{
    return part == AV_SKIN || part == AV_HAIR_COLOR || part == AV_TOP_COLOR || part == AV_BACKGROUND ||
           part == AV_EYE_COLOR;
}

uint32_t avatar_option_color(int part, int value)
{
    if (part < 0 || part >= AV_PARTS || value < 0 || value >= avatar_count[part]) return 0;
    switch (part) {
    case AV_SKIN: return SKIN[value];
    case AV_HAIR_COLOR: return HAIR[value];
    case AV_TOP_COLOR: return TOP[value];
    case AV_BACKGROUND: return BG[value];
    case AV_EYE_COLOR: return EYE[value];
    }
    return 0;
}

static int digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    return -1;
}

/* How many parts a code has (9 or 13), or 0 if it isn't a code at all. */
static int code_parts(const char *code)
{
    size_t n = code ? strlen(code) : 0;
    if (n == 10 && code[0] == '1') return AV_PARTS_V1;
    if (n == AVATAR_CODE_LEN && code[0] == '2') return AV_PARTS;
    return 0;
}

int avatar_valid(const char *code)
{
    int i, parts = code_parts(code);
    if (!parts) return 0;
    for (i = 0; i < parts; i++) {
        int d = digit(code[i + 1]);
        if (d < 0 || d >= (parts == AV_PARTS_V1 ? v1_count[i] : avatar_count[i])) return 0;
    }
    return 1;
}

void avatar_parse(const char *code, int v[AV_PARTS])
{
    int i, parts = code_parts(code);
    for (i = 0; i < AV_PARTS; i++) {
        int d = i < parts ? digit(code[i + 1]) : -1;
        v[i] = (d >= 0 && d < avatar_count[i]) ? d : defaults[i];
    }
}

void avatar_encode(const int v[AV_PARTS], char out[AVATAR_CODE_LEN + 1])
{
    static const char D[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    int i;
    out[0] = '2';
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
    /* Keep the busy parts rarer so random looks don't all come out cluttered. */
    if (v[AV_ACCESSORY] >= 4 && seed % 3) v[AV_ACCESSORY] = 0;
    if (v[AV_FACIAL_HAIR] && (seed >> 3) % 2) v[AV_FACIAL_HAIR] = 0;
    if (v[AV_CHEEKS] && (seed >> 5) % 2) v[AV_CHEEKS] = 0;
    if (v[AV_BROWS] && (seed >> 7) % 2) v[AV_BROWS] = 0;
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
static void ell_a(float cx, float cy, float rx, float ry, uint32_t c, int a) { gfx_ellipse(PX(cx), PY(cy), L(rx), L(ry), c, a); }
static void circ(float cx, float cy, float r, uint32_t c) { gfx_circle(PX(cx), PY(cy), L(r), c, 255); }
static void rr(float x, float y, float w, float h, float r, uint32_t c) { gfx_fill_rrect(PX(x), PY(y), L(w), L(h), L(r), c, 255); }
static void ln(float x0, float y0, float x1, float y1, float t, uint32_t c) { gfx_line(PX(x0), PY(y0), PX(x1), PY(y1), L(t), c, 255); }
static void arc(float cx, float cy, float r, float t, float a0, float a1, uint32_t c) { gfx_arc(PX(cx), PY(cy), L(r), L(t), a0, a1, c, 255); }
static void ring(float cx, float cy, float r, float t, uint32_t c) { gfx_ring(PX(cx), PY(cy), L(r), L(t), c, 255); }
static void tri(float a, float b, float c, float d, float e, float f, uint32_t col) { gfx_tri(PX(a), PY(b), PX(c), PY(d), PX(e), PY(f), col, 255); }

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

static void avatar_render(const int v[AV_PARTS], int x, int y, int size)
{
    const float PI = 3.14159265f;
    uint32_t skin = SKIN[v[AV_SKIN]], hair = HAIR[v[AV_HAIR_COLOR]], top = TOP[v[AV_TOP_COLOR]];
    uint32_t skin_dark = gfx_shade(skin, -14), top_dark = gfx_shade(top, -22);
    uint32_t eye = EYE[v[AV_EYE_COLOR]], dot = v[AV_EYE_COLOR] ? gfx_shade(eye, -45) : INK;
    int style = v[AV_HAIR], acc = v[AV_ACCESSORY], outer[4];
    int hat = acc == 4 || acc == 5;

    ox = (float)x; oy = (float)y; sz = (float)size;
    gfx_get_clip(outer);
    gfx_fill_rrect(PX(0), PY(0), L(1), L(1), L(0.22f), BG[v[AV_BACKGROUND]], 255);
    gfx_clip_intersect(x, y, size, size);

    /* Behind the head */
    if (style == 2) rr(0.26f, 0.26f, 0.48f, 0.54f, 0.18f, hair);                    /* long */
    if (style == 11) rr(0.27f, 0.27f, 0.46f, 0.40f, 0.16f, hair);                   /* bob */
    if (style == 8 && !hat) {                                                        /* afro */
        circ(0.5f, 0.35f, 0.255f, hair);
        circ(0.29f, 0.45f, 0.09f, hair);
        circ(0.71f, 0.45f, 0.09f, hair);
    }
    if (style == 10) {                                                               /* ponytail */
        ell(0.71f, 0.52f, 0.06f, 0.16f, hair);
        circ(0.675f, 0.37f, 0.024f, gfx_shade(hair, -35));
    }
    if (style == 13) {                                                               /* pigtails */
        circ(0.255f, 0.50f, 0.075f, hair);
        circ(0.745f, 0.50f, 0.075f, hair);
        circ(0.30f, 0.45f, 0.02f, top);
        circ(0.70f, 0.45f, 0.02f, top);
    }
    if (v[AV_TOP] == 1) rr(0.30f, 0.66f, 0.40f, 0.16f, 0.08f, top_dark);           /* hood */

    /* Body */
    rr(0.20f, 0.74f, 0.60f, 0.40f, 0.16f, v[AV_TOP] == 7 ? skin : top);
    rr(0.44f, 0.58f, 0.12f, 0.20f, 0.04f, skin_dark);                                /* neck */
    switch (v[AV_TOP]) {
    case 1:                                                                          /* hoodie strings */
        ln(0.46f, 0.80f, 0.45f, 0.92f, 0.012f, 0xE8ECF4);
        ln(0.54f, 0.80f, 0.55f, 0.92f, 0.012f, 0xE8ECF4);
        break;
    case 2:                                                                          /* collar */
        tri(0.43f, 0.74f, 0.50f, 0.81f, 0.39f, 0.83f, 0xF4F6FA);
        tri(0.57f, 0.74f, 0.50f, 0.81f, 0.61f, 0.83f, 0xF4F6FA);
        break;
    case 3:                                                                          /* jacket */
        rr(0.44f, 0.76f, 0.12f, 0.30f, 0.0f, 0xE8ECF4);
        ln(0.44f, 0.76f, 0.40f, 0.95f, 0.02f, top_dark);
        ln(0.56f, 0.76f, 0.60f, 0.95f, 0.02f, top_dark);
        break;
    case 4:                                                                          /* sweater */
        rr(0.40f, 0.735f, 0.20f, 0.045f, 0.022f, top_dark);
        rr(0.20f, 0.88f, 0.60f, 0.035f, 0.0f, gfx_shade(top, 22));
        break;
    case 5:                                                                          /* stripes */
        rr(0.20f, 0.83f, 0.60f, 0.03f, 0.0f, gfx_shade(top, 45));
        rr(0.20f, 0.91f, 0.60f, 0.03f, 0.0f, gfx_shade(top, 45));
        break;
    case 6:                                                                          /* jersey */
        tri(0.445f, 0.74f, 0.555f, 0.74f, 0.50f, 0.80f, skin_dark);
        rr(0.215f, 0.80f, 0.035f, 0.30f, 0.0f, 0xF4F6FA);
        rr(0.75f, 0.80f, 0.035f, 0.30f, 0.0f, 0xF4F6FA);
        break;
    case 7:                                                                          /* tank top */
        rr(0.315f, 0.76f, 0.37f, 0.40f, 0.09f, top);
        rr(0.33f, 0.74f, 0.04f, 0.06f, 0.02f, top);
        rr(0.63f, 0.74f, 0.04f, 0.06f, 0.02f, top);
        break;
    }

    /* Head */
    circ(0.305f, 0.47f, 0.045f, skin);
    circ(0.695f, 0.47f, 0.045f, skin);
    ell(0.5f, 0.45f, 0.195f, 0.225f, skin);

    /* Cheeks */
    if (v[AV_CHEEKS] == 1) {
        ell_a(0.385f, 0.535f, 0.036f, 0.021f, 0xFF7A8A, 95);
        ell_a(0.615f, 0.535f, 0.036f, 0.021f, 0xFF7A8A, 95);
    } else if (v[AV_CHEEKS] == 2) {
        uint32_t fc = gfx_shade(skin, -32);
        circ(0.375f, 0.515f, 0.0065f, fc); circ(0.395f, 0.53f, 0.0065f, fc); circ(0.37f, 0.54f, 0.0065f, fc);
        circ(0.625f, 0.515f, 0.0065f, fc); circ(0.605f, 0.53f, 0.0065f, fc); circ(0.63f, 0.54f, 0.0065f, fc);
    }

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
                tri(bx, 0.29f, bx + 0.085f, 0.29f, bx + 0.03f, 0.15f + (i % 2) * 0.03f, hair);
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
        case 8:                                                                      /* afro */
            hair_cap(0.35f, hair);
            break;
        case 9:                                                                      /* mohawk */
            hair_cap(0.33f, gfx_mix(hair, skin, 110));
            rr(0.455f, 0.12f, 0.09f, 0.23f, 0.045f, hair);
            break;
        case 10:                                                                     /* ponytail */
            hair_cap(0.375f, hair);
            break;
        case 11:                                                                     /* bob */
            hair_cap(0.37f, hair);
            rr(0.285f, 0.33f, 0.065f, 0.30f, 0.03f, hair);
            rr(0.65f, 0.33f, 0.065f, 0.30f, 0.03f, hair);
            break;
        case 12:                                                                     /* side part */
            hair_cap(0.37f, hair);
            tri(0.30f, 0.37f, 0.56f, 0.25f, 0.33f, 0.44f, hair);
            ln(0.56f, 0.24f, 0.585f, 0.33f, 0.012f, gfx_shade(hair, -30));
            break;
        case 13:                                                                     /* pigtails */
            hair_cap(0.37f, hair);
            break;
        default:
            break;
        }
    } else if (style == 2 || style == 11) {
        rr(0.29f, 0.33f, 0.07f, style == 2 ? 0.38f : 0.30f, 0.03f, hair);
        rr(0.64f, 0.33f, 0.07f, style == 2 ? 0.38f : 0.30f, 0.03f, hair);
    }

    /* Eyebrows */
    {
        uint32_t brow = style == 7 ? gfx_shade(skin, -40) : hair;
        switch (v[AV_BROWS]) {
        case 1:
            ln(0.41f, 0.405f, 0.46f, 0.40f, 0.026f, brow);
            ln(0.54f, 0.40f, 0.59f, 0.405f, 0.026f, brow);
            break;
        case 2:
            ln(0.41f, 0.392f, 0.46f, 0.381f, 0.016f, brow);
            ln(0.54f, 0.381f, 0.59f, 0.392f, 0.016f, brow);
            break;
        case 3:
            ln(0.41f, 0.393f, 0.46f, 0.412f, 0.018f, brow);
            ln(0.54f, 0.412f, 0.59f, 0.393f, 0.018f, brow);
            break;
        case 4:
            ln(0.41f, 0.405f, 0.46f, 0.40f, 0.009f, brow);
            ln(0.54f, 0.40f, 0.59f, 0.405f, 0.009f, brow);
            break;
        default:
            ln(0.41f, 0.405f, 0.46f, 0.40f, 0.016f, brow);
            ln(0.54f, 0.40f, 0.59f, 0.405f, 0.016f, brow);
            break;
        }
    }

    /* Eyes */
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
        circ(0.43f, 0.469f, 0.019f, v[AV_EYE_COLOR] ? eye : INK);
        circ(0.57f, 0.469f, 0.019f, v[AV_EYE_COLOR] ? eye : INK);
        break;
    case 4:
        circ(0.43f, 0.465f, 0.022f, dot);
        arc(0.57f, 0.48f, 0.028f, 0.014f, PI + 0.35f, 2 * PI - 0.35f, INK);
        break;
    case 5:                                                                          /* grumpy */
        clip_below(0.457f);
        circ(0.43f, 0.468f, 0.024f, dot);
        circ(0.57f, 0.468f, 0.024f, dot);
        clip_restore();
        break;
    case 6:                                                                          /* sparkle */
        circ(0.43f, 0.465f, 0.03f, v[AV_EYE_COLOR] ? eye : INK);
        circ(0.57f, 0.465f, 0.03f, v[AV_EYE_COLOR] ? eye : INK);
        circ(0.438f, 0.455f, 0.0095f, 0xFFFFFF);
        circ(0.578f, 0.455f, 0.0095f, 0xFFFFFF);
        break;
    case 7:                                                                          /* focused */
        ell(0.43f, 0.468f, 0.027f, 0.013f, dot);
        ell(0.57f, 0.468f, 0.027f, 0.013f, dot);
        break;
    default:
        circ(0.43f, 0.465f, 0.022f, dot);
        circ(0.57f, 0.465f, 0.022f, dot);
        break;
    }
    circ(0.5f, 0.52f, 0.012f, gfx_shade(skin, -18));

    /* Facial hair */
    switch (v[AV_FACIAL_HAIR]) {
    case 1:                                                                          /* stubble */
        clip_below(0.535f);
        ell_a(0.5f, 0.585f, 0.165f, 0.085f, gfx_shade(skin, -38), 105);
        clip_restore();
        break;
    case 2:                                                                          /* mustache */
        ell(0.468f, 0.538f, 0.034f, 0.013f, hair);
        ell(0.532f, 0.538f, 0.034f, 0.013f, hair);
        break;
    case 3:                                                                          /* beard */
        clip_below(0.53f);
        ell(0.5f, 0.585f, 0.19f, 0.105f, hair);
        clip_restore();
        ell(0.468f, 0.538f, 0.034f, 0.013f, hair);
        ell(0.532f, 0.538f, 0.034f, 0.013f, hair);
        break;
    case 4:                                                                          /* goatee */
        ell(0.5f, 0.632f, 0.032f, 0.03f, hair);
        ell(0.468f, 0.538f, 0.03f, 0.011f, hair);
        ell(0.532f, 0.538f, 0.03f, 0.011f, hair);
        break;
    }

    /* Mouth */
    switch (v[AV_MOUTH]) {
    case 1:
        clip_below(0.555f);
        circ(0.5f, 0.555f, 0.05f, MOUTH);
        clip_restore();
        rr(0.465f, 0.555f, 0.07f, 0.014f, 0.004f, 0xFFFFFF);
        break;
    case 2:
        ln(0.465f, 0.57f, 0.535f, 0.57f, 0.014f, MOUTH);
        break;
    case 3:
        ell(0.5f, 0.572f, 0.022f, 0.028f, MOUTH);
        break;
    case 4:
        arc(0.52f, 0.55f, 0.04f, 0.014f, 0.45f, 1.9f, MOUTH);
        break;
    case 5:                                                                          /* laugh */
        clip_below(0.55f);
        circ(0.5f, 0.55f, 0.062f, MOUTH);
        circ(0.5f, 0.605f, 0.03f, 0xE0707A);
        clip_restore();
        rr(0.458f, 0.55f, 0.084f, 0.014f, 0.004f, 0xFFFFFF);
        break;
    case 6:                                                                          /* tongue out */
        ell(0.515f, 0.585f, 0.018f, 0.016f, 0xE0707A);
        arc(0.5f, 0.55f, 0.045f, 0.014f, 0.35f, PI - 0.35f, MOUTH);
        break;
    case 7:                                                                          /* frown */
        arc(0.5f, 0.605f, 0.042f, 0.014f, PI + 0.4f, 2 * PI - 0.4f, MOUTH);
        break;
    case 8:                                                                          /* teeth */
        rr(0.458f, 0.548f, 0.084f, 0.04f, 0.018f, MOUTH);
        rr(0.466f, 0.554f, 0.068f, 0.028f, 0.012f, 0xFFFFFF);
        break;
    case 9:                                                                          /* cat */
        arc(0.48f, 0.553f, 0.02f, 0.012f, 0.25f, PI - 0.25f, MOUTH);
        arc(0.52f, 0.553f, 0.02f, 0.012f, 0.25f, PI - 0.25f, MOUTH);
        break;
    default:
        arc(0.5f, 0.55f, 0.045f, 0.014f, 0.35f, PI - 0.35f, MOUTH);
        break;
    }

    /* Accessories */
    switch (acc) {
    case 1:
        ring(0.43f, 0.465f, 0.05f, 0.012f, 0x23262E);
        ring(0.57f, 0.465f, 0.05f, 0.012f, 0x23262E);
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
    case 6:                                                                          /* headband */
        rr(0.305f, 0.325f, 0.39f, 0.038f, 0.019f, top);
        break;
    case 7:                                                                          /* crown */
        rr(0.375f, 0.235f, 0.25f, 0.055f, 0.01f, GOLD);
        tri(0.375f, 0.24f, 0.425f, 0.24f, 0.385f, 0.165f, GOLD);
        tri(0.47f, 0.24f, 0.53f, 0.24f, 0.50f, 0.15f, GOLD);
        tri(0.575f, 0.24f, 0.625f, 0.24f, 0.615f, 0.165f, GOLD);
        circ(0.50f, 0.262f, 0.012f, 0xE04A3F);
        break;
    case 8:                                                                          /* bow */
        tri(0.60f, 0.245f, 0.60f, 0.325f, 0.665f, 0.285f, top);
        tri(0.73f, 0.245f, 0.73f, 0.325f, 0.665f, 0.285f, top);
        circ(0.665f, 0.285f, 0.02f, top_dark);
        break;
    case 9:                                                                          /* earrings */
        circ(0.302f, 0.518f, 0.013f, GOLD);
        circ(0.698f, 0.518f, 0.013f, GOLD);
        break;
    case 10:                                                                         /* round glasses */
        ring(0.43f, 0.465f, 0.046f, 0.008f, 0xB8902C);
        ring(0.57f, 0.465f, 0.046f, 0.008f, 0xB8902C);
        ln(0.476f, 0.46f, 0.524f, 0.46f, 0.008f, 0xB8902C);
        ln(0.384f, 0.46f, 0.31f, 0.452f, 0.008f, 0xB8902C);
        ln(0.616f, 0.46f, 0.69f, 0.452f, 0.008f, 0xB8902C);
        break;
    }

    gfx_set_clip(outer[0], outer[1], outer[2], outer[3]);
}

/* Drawing an avatar is a few hundred shapes, so each (avatar, size) is drawn once into an
 * image and copied after that. The rounded outline becomes the image's alpha, so the
 * corners blend with whatever is behind them. */
#define AV_CACHE 48
#define AV_CACHE_BYTES (8u * 1024 * 1024)      /* the editor's big preview makes 700 KB copies */
static struct { char code[AVATAR_CODE_LEN + 1]; int size; struct img im; uint32_t used; } av_cache[AV_CACHE];
static uint32_t av_clock;
static size_t av_bytes;

static void av_evict(int i)
{
    av_bytes -= (size_t)av_cache[i].size * av_cache[i].size * 4;
    free(av_cache[i].im.px);
    memset(&av_cache[i], 0, sizeof av_cache[i]);
}

static int av_lru(void)
{
    int i, lru = -1;
    for (i = 0; i < AV_CACHE; i++)
        if (av_cache[i].im.px && (lru < 0 || av_cache[i].used < av_cache[lru].used)) lru = i;
    return lru;
}

static const struct img *avatar_cached(const int v[AV_PARTS], int size)
{
    char code[AVATAR_CODE_LEN + 1];
    int i, slot = -1, n = size * size;
    uint32_t *px, *mask, bg = BG[v[AV_BACKGROUND]];
    if (size <= 0 || size > 512) return NULL;
    avatar_encode(v, code);
    for (i = 0; i < AV_CACHE; i++)
        if (av_cache[i].im.px && av_cache[i].size == size && !strcmp(av_cache[i].code, code)) {
            av_cache[i].used = ++av_clock;
            return &av_cache[i].im;
        }

    px = malloc((size_t)n * 4);
    mask = calloc((size_t)n, 4);
    if (!px || !mask) { free(px); free(mask); return NULL; }
    if (!gfx_target(mask, size, size)) { free(px); free(mask); return NULL; }
    gfx_fill_rrect(0, 0, (float)size, (float)size, size * 0.22f, 0xFFFFFF, 255);
    gfx_target(NULL, 0, 0);
    for (i = 0; i < n; i++) px[i] = bg;
    gfx_target(px, size, size);
    avatar_render(v, 0, 0, size);
    gfx_target(NULL, 0, 0);
    for (i = 0; i < n; i++) px[i] = (px[i] & 0xffffff) | ((mask[i] & 255) << 24);
    free(mask);

    while (av_bytes + (size_t)n * 4 > AV_CACHE_BYTES && (i = av_lru()) >= 0) av_evict(i);
    for (i = 0; i < AV_CACHE && slot < 0; i++) if (!av_cache[i].im.px) slot = i;
    if (slot < 0) { slot = av_lru(); av_evict(slot); }
    av_bytes += (size_t)n * 4;
    memcpy(av_cache[slot].code, code, sizeof code);
    av_cache[slot].size = size;
    av_cache[slot].im.w = size;
    av_cache[slot].im.h = size;
    av_cache[slot].im.px = px;
    av_cache[slot].im.id = gfx_new_image_id();
    av_cache[slot].used = ++av_clock;
    return &av_cache[slot].im;
}

void avatar_draw_v(const int v[AV_PARTS], int x, int y, int size)
{
    const struct img *c = avatar_cached(v, size);
    if (c) gfx_image(c, x, y, size, size, 255);
    else avatar_render(v, x, y, size);
}

void avatar_draw(const char *code, int x, int y, int size)
{
    int v[AV_PARTS];
    avatar_parse(code, v);
    avatar_draw_v(v, x, y, size);
}
