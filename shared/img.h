#ifndef XENITH_IMG_H
#define XENITH_IMG_H

/* XIMG: the image format XenithOS uses for thumbnails (made by tools/mkart.py).
 *
 *   "XIMG"  u16 width  u16 height  u8 format (1 = RGBA8)  u8 compression (1 = zlib)
 *   u16 reserved  u32 raw size  then the zlib data.   All numbers big-endian.
 */
#include "gfx.h"

int  img_load_file(struct img *out, const char *path);        /* 1 on success */
int  img_load_mem(struct img *out, const void *data, int len);
void img_free(struct img *im);

#endif
