/* QR codes, so a link on the TV (the playit.gg claim, Mojang's EULA) opens on a phone.
 *
 * Byte mode, error correction level M, versions 1 to 10 (up to 213 bytes), and the mask with
 * the lowest penalty, as the QR standard (ISO/IEC 18004) lays out. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "mn.h"

static const int ECC_PER_BLOCK[11] = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};   /* level M */
static const int NUM_BLOCKS[11]    = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};

static uint8_t mod[QR_MAX][QR_MAX], fn[QR_MAX][QR_MAX];   /* dark modules; function patterns */
static int sz;

static int raw_modules(int ver)
{
    int r = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int na = ver / 7 + 2;
        r -= (25 * na - 10) * na - 55;
        if (ver >= 7) r -= 36;
    }
    return r;
}

static int data_codewords(int ver)
{
    return raw_modules(ver) / 8 - ECC_PER_BLOCK[ver] * NUM_BLOCKS[ver];
}

/* ------------------------------------------------------ Reed-Solomon */

static uint8_t gf_mul(uint8_t x, uint8_t y)
{
    int z = 0, i;
    for (i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (uint8_t)z;
}

static void rs_divisor(int degree, uint8_t *g)
{
    uint8_t root = 1;
    int i, j;
    memset(g, 0, (size_t)degree);
    g[degree - 1] = 1;
    for (i = 0; i < degree; i++) {
        for (j = 0; j < degree; j++) {
            g[j] = gf_mul(g[j], root);
            if (j + 1 < degree) g[j] ^= g[j + 1];
        }
        root = gf_mul(root, 0x02);
    }
}

static void rs_remainder(const uint8_t *data, int len, const uint8_t *g, int degree, uint8_t *out)
{
    int i, j;
    memset(out, 0, (size_t)degree);
    for (i = 0; i < len; i++) {
        uint8_t factor = data[i] ^ out[0];
        memmove(out, out + 1, (size_t)degree - 1);
        out[degree - 1] = 0;
        for (j = 0; j < degree; j++) out[j] ^= gf_mul(g[j], factor);
    }
}

/* ------------------------------------------------------ the pattern */

static void set_fn(int x, int y, int dark)
{
    mod[y][x] = (uint8_t)dark;
    fn[y][x] = 1;
}

static void finder(int cx, int cy)
{
    int dx, dy;
    for (dy = -4; dy <= 4; dy++)
        for (dx = -4; dx <= 4; dx++) {
            int x = cx + dx, y = cy + dy, d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            if (x >= 0 && x < sz && y >= 0 && y < sz) set_fn(x, y, d != 2 && d != 4);
        }
}

static void alignment(int cx, int cy)
{
    int dx, dy;
    for (dy = -2; dy <= 2; dy++)
        for (dx = -2; dx <= 2; dx++)
            set_fn(cx + dx, cy + dy, (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) != 1);
}

static int align_positions(int ver, int *pos)
{
    int n, step, i, p;
    if (ver == 1) return 0;
    n = ver / 7 + 2;
    step = (ver * 4 + n * 2 + 1) / (n * 2 - 2) * 2;
    pos[0] = 6;
    for (i = n - 1, p = sz - 7; i >= 1; i--, p -= step) pos[i] = p;
    return n;
}

static void format_bits(int mask)
{
    int data = (0 << 3) | mask, rem = data, bits, i;     /* level M is 00 */
    for (i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    bits = ((data << 10) | rem) ^ 0x5412;
    for (i = 0; i <= 5; i++) set_fn(8, i, (bits >> i) & 1);
    set_fn(8, 7, (bits >> 6) & 1);
    set_fn(8, 8, (bits >> 7) & 1);
    set_fn(7, 8, (bits >> 8) & 1);
    for (i = 9; i < 15; i++) set_fn(14 - i, 8, (bits >> i) & 1);
    for (i = 0; i < 8; i++) set_fn(sz - 1 - i, 8, (bits >> i) & 1);
    for (i = 8; i < 15; i++) set_fn(8, sz - 15 + i, (bits >> i) & 1);
    set_fn(8, sz - 8, 1);                                  /* the dark module */
}

static void version_bits(int ver)
{
    int rem = ver, bits, i;
    if (ver < 7) return;
    for (i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    bits = (ver << 12) | rem;
    for (i = 0; i < 18; i++) {
        int bit = (bits >> i) & 1, a = sz - 11 + i % 3, b = i / 3;
        set_fn(a, b, bit);
        set_fn(b, a, bit);
    }
}

static void function_patterns(int ver)
{
    int pos[8], n, i, j;
    for (i = 0; i < sz; i++) {
        set_fn(6, i, i % 2 == 0);
        set_fn(i, 6, i % 2 == 0);
    }
    finder(3, 3);
    finder(sz - 4, 3);
    finder(3, sz - 4);
    n = align_positions(ver, pos);
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            if (!((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))) alignment(pos[i], pos[j]);
    format_bits(0);
    version_bits(ver);
}

static void place(const uint8_t *data, int len)
{
    int i = 0, right, vert, j;
    for (right = sz - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (vert = 0; vert < sz; vert++)
            for (j = 0; j < 2; j++) {
                int x = right - j, up = ((right + 1) & 2) == 0, y = up ? sz - 1 - vert : vert;
                if (!fn[y][x] && i < len * 8) {
                    mod[y][x] = (data[i >> 3] >> (7 - (i & 7))) & 1;
                    i++;
                }
            }
    }
}

static int mask_bit(int mask, int x, int y)
{
    switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

static void apply_mask(int mask)
{
    int x, y;
    for (y = 0; y < sz; y++)
        for (x = 0; x < sz; x++)
            if (!fn[y][x] && mask_bit(mask, x, y)) mod[y][x] ^= 1;
}

/* How hard the pattern is to read: long runs, 2x2 blocks, things that look like finder
 * patterns, and too much or too little dark. */
static long penalty(void)
{
    long p = 0;
    int x, y, i, dark = 0;
    for (i = 0; i < 2; i++)                               /* rows, then columns */
        for (y = 0; y < sz; y++) {
            int run = 1;
            for (x = 1; x <= sz; x++) {
                int same = x < sz && (i ? mod[x][y] == mod[x - 1][y] : mod[y][x] == mod[y][x - 1]);
                if (same) run++;
                else { if (run >= 5) p += 3 + run - 5; run = 1; }
            }
            for (x = -4; x < sz; x++) {                   /* 1011101 with four light on one side */
                static const uint8_t pat[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
                int k, fwd = 1, back = 1;
                for (k = 0; k < 11; k++) {
                    int xx = x + k, v = xx >= 0 && xx < sz ? (i ? mod[xx][y] : mod[y][xx]) : 0;
                    if (v != pat[k]) fwd = 0;
                    if (v != pat[10 - k]) back = 0;
                }
                p += 40 * (fwd + back);
            }
        }
    for (y = 0; y + 1 < sz; y++)
        for (x = 0; x + 1 < sz; x++)
            if (mod[y][x] == mod[y][x + 1] && mod[y][x] == mod[y + 1][x] && mod[y][x] == mod[y + 1][x + 1]) p += 3;
    for (y = 0; y < sz; y++)
        for (x = 0; x < sz; x++) dark += mod[y][x];
    {
        int total = sz * sz, k = (abs(dark * 20 - total * 10) + total - 1) / total - 1;
        p += 10L * (k > 0 ? k : 0);
    }
    return p;
}

int qr_make(const char *text, uint8_t out[QR_MAX][QR_MAX])
{
    uint8_t data[400], all[400], block[200], ecc[40], gen[40];
    int len = (int)strlen(text), ver, cap = 0, bits = 0, i, j, k;
    int nblocks, ecc_len, raw, nshort, short_len, best = 0, total = 0;
    long best_p = -1;

    for (ver = 1; ver <= 10; ver++) {
        cap = data_codewords(ver);
        if (4 + (ver < 10 ? 8 : 16) + 8 * len <= cap * 8) break;
    }
    if (ver > 10) return 0;
    sz = ver * 4 + 17;

    /* The data: mode, length, the bytes, a terminator, then filler. */
    memset(data, 0, sizeof data);
#define PUT(v, n) do { int b_; for (b_ = (n) - 1; b_ >= 0; b_--, bits++) if (((v) >> b_) & 1) data[bits >> 3] |= (uint8_t)(0x80 >> (bits & 7)); } while (0)
    PUT(4, 4);
    PUT(len, ver < 10 ? 8 : 16);
    for (i = 0; i < len; i++) PUT((uint8_t)text[i], 8);
    bits += cap * 8 - bits < 4 ? cap * 8 - bits : 4;
    bits = (bits + 7) / 8 * 8;
    for (i = bits / 8, j = 0; i < cap; i++, j ^= 1) data[i] = j ? 0x11 : 0xEC;
#undef PUT

    /* Split into blocks, add each one's error correction, and interleave. */
    nblocks = NUM_BLOCKS[ver];
    ecc_len = ECC_PER_BLOCK[ver];
    raw = raw_modules(ver) / 8;
    nshort = nblocks - raw % nblocks;
    short_len = raw / nblocks;
    rs_divisor(ecc_len, gen);
    {
        uint8_t blocks[5][200];
        int dlen;
        for (i = 0, k = 0; i < nblocks; i++) {
            dlen = short_len - ecc_len + (i < nshort ? 0 : 1);
            memcpy(block, data + k, (size_t)dlen);
            k += dlen;
            rs_remainder(block, dlen, gen, ecc_len, ecc);
            memset(blocks[i], 0, sizeof blocks[i]);
            memcpy(blocks[i], block, (size_t)dlen);
            memcpy(blocks[i] + short_len - ecc_len + 1, ecc, (size_t)ecc_len);   /* short blocks get a gap */
        }
        for (i = 0; i < short_len + 1; i++)
            for (j = 0; j < nblocks; j++)
                if (i != short_len - ecc_len || j >= nshort) all[total++] = blocks[j][i];
    }

    memset(mod, 0, sizeof mod);
    memset(fn, 0, sizeof fn);
    function_patterns(ver);
    place(all, total);
    for (i = 0; i < 8; i++) {
        long p;
        apply_mask(i);
        format_bits(i);
        p = penalty();
        if (best_p < 0 || p < best_p) { best_p = p; best = i; }
        apply_mask(i);                                     /* undo it */
    }
    apply_mask(best);
    format_bits(best);
    memcpy(out, mod, sizeof mod);
    return sz;
}

/* The code on a white square with a quiet zone of four modules, size pixels across. */
void qr_draw(const char *text, int x, int y, int size)
{
    static char last[256];
    static uint8_t m[QR_MAX][QR_MAX];
    static int n;
    int cell, ox, oy, i, j;
    if (strcmp(last, text)) {
        snprintf(last, sizeof last, "%s", text);
        n = qr_make(text, m);
    }
    if (!n) return;
    cell = size / (n + 8);
    if (cell < 1) cell = 1;
    ox = x + (size - cell * n) / 2;
    oy = y + (size - cell * n) / 2;
    gfx_fill_rrect((float)x, (float)y, (float)size, (float)size, 10, 0xFFFFFF, 255);
    for (j = 0; j < n; j++)
        for (i = 0; i < n; i++)
            if (m[j][i]) gfx_rect(ox + i * cell, oy + j * cell, cell, cell, 0x000000);
}
