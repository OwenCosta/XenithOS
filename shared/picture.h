#ifndef XENITH_PICTURE_H
#define XENITH_PICTURE_H

/* Pictures: JPEG, PNG, BMP and GIF (its first frame) through stb_image, as XenithOS images.
 * Used by Photos, by Tube for video frames and thumbnails, and on PCs for the pictures of
 * Steam games. */
#include <stdint.h>
#include "gfx.h"

#define PICTURE_MAX_PIXELS (48 * 1000 * 1000)    /* bigger than that isn't opened */

/* The whole picture as 0xAARRGGBB pixels (free() them), or NULL. */
uint32_t *picture_decode(const unsigned char *data, int len, int *w, int *h);
/* 1 if data looks like a picture this can open; its size in *w, *h. */
int picture_info(const unsigned char *data, int len, int *w, int *h);

/* Decoded and made smaller to fit inside max_w x max_h, keeping its shape (it's never made
 * bigger), ready for gfx_image(). 1 on success. */
int picture_load_mem(struct img *out, const unsigned char *data, int len, int max_w, int max_h);
int picture_load_file(struct img *out, const char *path, int max_w, int max_h);

/* px (w x h) scaled and cropped to fill exactly out_w x out_h, centred, as an image. */
int picture_cover(struct img *out, const uint32_t *px, int w, int h, int out_w, int out_h);
/* The part of px from (sx, sy), sw x sh in size, made out_w x out_h, as an image. */
int picture_region(struct img *out, const uint32_t *px, int w, int h, int sx, int sy, int sw, int sh,
                   int out_w, int out_h);

#endif
