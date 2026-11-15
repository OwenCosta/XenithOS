#ifndef XENITH_TILED_H
#define XENITH_TILED_H

/* Copying finished frames into the Xbox 360's tiled framebuffer.
 *
 * The framebuffer is stored in 32x32 tiles with a fixed pixel order inside each tile, and
 * in 0xBBGGRR00 byte order. tiled_present() compares the new frame with the last one in
 * 32x32 blocks and only rewrites (and flushes) blocks that changed, so a still screen costs
 * almost nothing. Pure C, so the simulator can run and check it too. */
#include <stdint.h>

struct tiled_fb {
    uint32_t *fb;              /* framebuffer (the console's, or a fake one in the simulator) */
    int pitch, rows;           /* framebuffer size rounded up to whole tiles */
    int off_x, off_y;          /* where the safe area starts inside the framebuffer */
    int w, h;                  /* safe area size, the size of the frames we're given */
    int tiles_x, tiles_y;
    uint16_t inner[32 * 32];   /* pixel order inside a tile */
    int *col_tile, *col_in, *row_base;
    uint32_t *shadow;          /* the last frame we sent */
    unsigned char *dirty;      /* framebuffer tiles written since the last flush */
};

void tiled_init(struct tiled_fb *t, uint32_t *fb, int fb_w, int fb_h, int off_x, int off_y, int w, int h);
/* Returns how many 32x32 blocks changed. flush(addr, bytes) writes cache lines out to memory. */
int  tiled_present(struct tiled_fb *t, const uint32_t *px, void (*flush)(void *addr, int bytes));
uint32_t tiled_read(const struct tiled_fb *t, int x, int y);   /* back to 0x00RRGGBB, for checking */

#endif
