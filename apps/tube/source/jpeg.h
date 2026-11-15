#ifndef TUBE_JPEG_H
#define TUBE_JPEG_H
#include <stdint.h>

/* Decode a JPEG into 0xFFRRGGBB pixels (free() them). NULL if it isn't one. */
uint32_t *jpeg_decode(const unsigned char *data, int len, int *w, int *h);

#endif
