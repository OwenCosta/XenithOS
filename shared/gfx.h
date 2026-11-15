#ifndef XENITH_GFX_H
#define XENITH_GFX_H

/* XenithOS drawing. Everything draws into a back buffer (0x00RRGGBB); gfx_present()
 * sends the finished frame to the screen. Shapes are anti-aliased with signed distances,
 * text uses the Inter bitmap fonts from font_data.c. Alpha values run 0..255. */
#include <stdint.h>
#include "font_data.h"

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

extern int SCR_W, SCR_H;
extern uint32_t *gfx_bb;

void gfx_init(void);                 /* after plat_init() */
void gfx_present(void);

void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);
void gfx_get_clip(int r[4]);                          /* x, y, w, h; restore with gfx_set_clip */
void gfx_clip_intersect(int x, int y, int w, int h);

void gfx_clear(uint32_t c);
void gfx_rect(int x, int y, int w, int h, uint32_t c);
void gfx_rect_a(int x, int y, int w, int h, uint32_t c, int a);
void gfx_fill_rrect(float x, float y, float w, float h, float r, uint32_t c, int a);
void gfx_stroke_rrect(float x, float y, float w, float h, float r, float t, uint32_t c, int a);
void gfx_circle(float cx, float cy, float r, uint32_t c, int a);
void gfx_ellipse(float cx, float cy, float rx, float ry, uint32_t c, int a);
void gfx_ring(float cx, float cy, float r, float t, uint32_t c, int a);
void gfx_arc(float cx, float cy, float r, float t, float a0, float a1, uint32_t c, int a);  /* radians, round caps */
void gfx_line(float x0, float y0, float x1, float y1, float t, uint32_t c, int a);           /* round caps */
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c, int a);

/* Text. y is the top of the line box; gfx_line_h() is the box height. */
int  gfx_str(int font, int x, int y, uint32_t c, const char *s);          /* returns width */
int  gfx_str_a(int font, int x, int y, uint32_t c, int a, const char *s);
int  gfx_str_w(int font, const char *s);
int  gfx_str_fit(int font, int x, int y, uint32_t c, const char *s, int max_w);   /* adds "..." */
void gfx_str_c(int font, int cx, int y, uint32_t c, const char *s);
void gfx_str_r(int font, int right_x, int y, uint32_t c, const char *s);
int  gfx_wrap(int font, int x, int y, int w, int gap, uint32_t c, const char *s, int max_lines);
int  gfx_line_h(int font);
int  gfx_cap_h(int font);
int  gfx_str_vc(int font, int x, int cy, uint32_t c, const char *s);     /* caps centred on cy */

/* The 5x7 pixel font, kept for the XenithOS wordmark. */
int  gfx_text(int x, int y, int s, uint32_t c, const char *str);
int  gfx_text_w(int s, const char *str);
#define GFX_TEXT_H(s) (7 * (s))
int  gfx_brand(int x, int y, int s, uint32_t main_color, uint32_t os_color);
#define GFX_BRAND_W(s) gfx_text_w((s), "XENITHOS")

/* Images: 0xAARRGGBB pixels. id is unique per loaded or generated image (used for caching). */
struct img { int w, h; uint32_t *px; uint32_t id; };
uint32_t gfx_new_image_id(void);
/* Scaled and rounded copies are made once and cached, so drawing the same image at the same
 * size every frame is just a copy. */
void gfx_image(const struct img *im, int x, int y, int w, int h, int a);
void gfx_image_rounded(const struct img *im, int x, int y, int w, int h, float r, int a);
void gfx_blit_scaled(const uint32_t *src, int sw, int sh, int dx, int dy, int dw, int dh);  /* opaque, nearest */
/* An alpha mask (coverage in the low byte of each pixel) drawn in one colour. */
void gfx_mask(const struct img *mask, int x, int y, uint32_t color, int a);

/* Draw into an off-screen buffer (w*h pixels) instead of the screen; gfx_target(NULL, 0, 0)
 * goes back to the screen. Used to render things once and cache them. Doesn't nest: returns
 * 0 (and changes nothing) when asked to switch to a buffer while already drawing into one. */
int  gfx_target(uint32_t *buf, int w, int h);

uint32_t gfx_mix(uint32_t a, uint32_t b, int t);     /* t = 0..256, a -> b */
uint32_t gfx_shade(uint32_t c, int pct);              /* pct > 0 lighter, < 0 darker */

#endif
