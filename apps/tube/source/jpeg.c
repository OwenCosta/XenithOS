/* JPEG decoding for TUBE, through stb_image (public domain / MIT, see stb_image.h).
 * Only the JPEG decoder is built in. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#if defined(XENON)
#define STBI_NO_SIMD               /* stb has SSE2 and NEON paths, nothing for the Xbox */
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "stb_image.h"

#include "jpeg.h"

uint32_t *jpeg_decode(const unsigned char *data, int len, int *w, int *h)
{
    int comp, i, n;
    unsigned char *rgb = stbi_load_from_memory(data, len, w, h, &comp, 3);
    uint32_t *out;
    if (!rgb) return NULL;
    n = *w * *h;
    out = malloc(sizeof(uint32_t) * (size_t)n);
    if (out)
        for (i = 0; i < n; i++)
            out[i] = 0xff000000u | ((uint32_t)rgb[i * 3] << 16) | ((uint32_t)rgb[i * 3 + 1] << 8) | rgb[i * 3 + 2];
    stbi_image_free(rgb);
    return out;
}
