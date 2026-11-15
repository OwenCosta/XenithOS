#ifndef XENITH_GFX_H
#define XENITH_GFX_H

#include <stdint.h>

/* Colors are plain 0xRRGGBB. */
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* Usable screen area in pixels (already inside the TV overscan margin). */
extern int SCR_W, SCR_H;

void gfx_init(void);                 /* call after console_init() */
void gfx_flush(void);                /* push drawing out of the CPU cache to the screen */

void gfx_rect(int x, int y, int w, int h, uint32_t c);
void gfx_clear(uint32_t c);

/* 5x7 pixel font, each font pixel drawn as an s-by-s block. Lowercase draws as uppercase. */
int  gfx_text(int x, int y, int s, uint32_t c, const char *str);   /* returns width drawn */
int  gfx_text_w(int s, const char *str);
void gfx_text_c(int cx, int y, int s, uint32_t c, const char *str); /* centered on cx */
#define GFX_TEXT_H(s) (7 * (s))

uint32_t gfx_mix(uint32_t a, uint32_t b, int t); /* t = 0..256, a -> b */

#endif
