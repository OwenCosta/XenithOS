/* XENITH drawing layer.
 *
 * Writes straight into the Xenos framebuffer. The framebuffer is tiled in 32x32 blocks;
 * the address formula below is the same one libxenon's console driver uses. The memory
 * is cached, so nothing reaches the TV until gfx_flush().
 */
#include <ctype.h>
#include <string.h>
#include <console/console.h>
#include <xenos/xenos.h>
#include <ppc/cache.h>
#include "gfx.h"

int SCR_W, SCR_H;

static uint32_t *fb;
static int fb_bytes;
static int fb_pitch;          /* framebuffer width rounded up to a 32-pixel tile */
static int off_x, off_y;      /* TV overscan margin */

/* Same layout libxenon's console driver reads (ATI display registers). */
struct ati_info {
    uint32_t unknown1[4];
    uint32_t base;
    uint32_t unknown2[8];
    uint32_t width;
    uint32_t height;
} __attribute__((__packed__));

void gfx_init(void)
{
    struct ati_info *ai = (struct ati_info *)0xec806100ULL;

    fb = (uint32_t *)(long)(ai->base | 0x80000000);
    fb_pitch = ((ai->width + 31) >> 5) << 5;
    fb_bytes = fb_pitch * (((ai->height + 31) >> 5) << 5) * 4;

    off_x = off_y = 0;
    if (xenos_is_overscan()) {
        off_x = ai->width / 28;
        off_y = ai->height / 28;
    }
    /* Keep the safe area a whole number of 8x16 cells, like the console does. */
    SCR_W = ((ai->width - off_x * 2) / 8) * 8;
    SCR_H = ((ai->height - off_y * 2) / 16) * 16;
}

void gfx_flush(void)
{
    memdcbst(fb, fb_bytes);
}

/* Framebuffer pixel format is 0xBBGGRR00, i.e. our 0x00RRGGBB byte-swapped. */
static inline uint32_t native(uint32_t c) { return __builtin_bswap32(c); }

static inline void pset_native(int x, int y, uint32_t v)
{
    x += off_x;
    y += off_y;
    fb[(((y >> 5) * 32 * fb_pitch + ((x >> 5) << 10) + (x & 3) + ((y & 1) << 2) +
         (((x & 31) >> 2) << 3) + (((y & 31) >> 1) << 6)) ^ ((y & 8) << 2))] = v;
}

void gfx_rect(int x, int y, int w, int h, uint32_t c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > SCR_W ? SCR_W : x + w;
    int y1 = y + h > SCR_H ? SCR_H : y + h;
    uint32_t v = native(c);
    int px, py;

    for (py = y0; py < y1; py++)
        for (px = x0; px < x1; px++)
            pset_native(px, py, v);
}

void gfx_clear(uint32_t c)
{
    gfx_rect(0, 0, SCR_W, SCR_H, c);
}

#define BLIT_MAX_W 2048

void gfx_blit_scaled(const uint32_t *src, int sw, int sh, int dx, int dy, int dw, int dh)
{
    static int col[BLIT_MAX_W], col_sw, col_dw;   /* source column for each output column */
    int x, y, x0, x1;

    if (dw <= 0 || dh <= 0 || dw > BLIT_MAX_W) return;
    if (col_sw != sw || col_dw != dw) {
        for (x = 0; x < dw; x++) col[x] = x * sw / dw;
        col_sw = sw;
        col_dw = dw;
    }
    x0 = dx < 0 ? -dx : 0;
    x1 = dx + dw > SCR_W ? SCR_W - dx : dw;

    for (y = 0; y < dh; y++) {
        int py = dy + y;
        if (py < 0 || py >= SCR_H) continue;
        const uint32_t *row = src + (y * sh / dh) * sw;
        for (x = x0; x < x1; x++)
            pset_native(dx + x, py, native(row[col[x]]));
    }
}

uint32_t gfx_mix(uint32_t a, uint32_t b, int t)
{
    int ra = (a >> 16) & 0xff, ga = (a >> 8) & 0xff, ba = a & 0xff;
    int rb = (b >> 16) & 0xff, gb = (b >> 8) & 0xff, bb = b & 0xff;
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    return RGB(ra + ((rb - ra) * t >> 8), ga + ((gb - ga) * t >> 8), ba + ((bb - ba) * t >> 8));
}

/* ---- 5x7 font: 7 rows per glyph, low 5 bits of each row, bit 4 = leftmost ---- */
static const uint8_t font[128][7] = {
    ['A'] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    ['B'] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    ['C'] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
    ['D'] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
    ['E'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
    ['F'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    ['G'] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
    ['H'] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    ['I'] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    ['J'] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
    ['K'] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    ['L'] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    ['M'] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    ['N'] = {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},
    ['O'] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    ['P'] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    ['Q'] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    ['R'] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    ['S'] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
    ['T'] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    ['U'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    ['V'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    ['W'] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
    ['X'] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    ['Y'] = {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04},
    ['Z'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
    ['0'] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    ['1'] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    ['2'] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    ['3'] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
    ['4'] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    ['5'] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    ['6'] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    ['7'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    ['8'] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    ['9'] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C},
    [','] = {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08},
    [':'] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00},
    ['-'] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
    ['_'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F},
    ['+'] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},
    ['/'] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10},
    ['%'] = {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03},
    ['('] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02},
    [')'] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08},
    ['>'] = {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08},
    ['<'] = {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02},
    ['!'] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04},
    ['?'] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04},
    ['\''] = {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00},
    ['#'] = {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A},
    ['='] = {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00},
    ['@'] = {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E},
    ['*'] = {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00},
};

static const uint8_t *glyph(char ch)
{
    static const uint8_t unknown[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}; /* '?' */
    unsigned char c = (unsigned char)toupper((unsigned char)ch);
    if (c == ' ') return font[' '];   /* all zero */
    if (c < 128) {
        const uint8_t *g = font[c];
        if (g[0] | g[1] | g[2] | g[3] | g[4] | g[5] | g[6]) return g;
    }
    return unknown;
}

int gfx_text_w(int s, const char *str)
{
    int n = (int)strlen(str);
    return n ? n * 6 * s - s : 0;
}

int gfx_text(int x, int y, int s, uint32_t c, const char *str)
{
    int x0 = x;
    for (; *str; str++, x += 6 * s) {
        const uint8_t *g = glyph(*str);
        int row, col;
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col))
                    gfx_rect(x + col * s, y + row * s, s, s, c);
    }
    return x - x0 - s;
}

void gfx_text_c(int cx, int y, int s, uint32_t c, const char *str)
{
    gfx_text(cx - gfx_text_w(s, str) / 2, y, s, c, str);
}

/* Draw text cut to at most max_w pixels, ending in ".." when it doesn't fit. */
int gfx_text_fit(int x, int y, int s, uint32_t c, const char *str, int max_w)
{
    char buf[256];
    int max_chars = (max_w + s) / (6 * s);
    int n = (int)strlen(str);
    if (n <= max_chars || max_chars < 3) return gfx_text(x, y, s, c, str);
    if (max_chars > (int)sizeof buf - 1) max_chars = sizeof buf - 1;
    memcpy(buf, str, max_chars - 2);
    buf[max_chars - 2] = '.';
    buf[max_chars - 1] = '.';
    buf[max_chars] = 0;
    return gfx_text(x, y, s, c, buf);
}
