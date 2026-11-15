/* JPEG decoding for TUBE: shared/picture.c (stb_image) does the work. */
#include "jpeg.h"
#include "picture.h"

uint32_t *jpeg_decode(const unsigned char *data, int len, int *w, int *h)
{
    return picture_decode(data, len, w, h);
}
