/* Minimal PNG decode (stb_image) and encode (zlib), with size-limited palette fallback. */
#ifndef VT_PNGIO_H
#define VT_PNGIO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *rgba; /* w * h * 4 bytes */
} Image;

/* Largest icon side accepted from untrusted files. */
#define IMG_MAX_SIDE 1024

/* Decodes PNG bytes into RGBA. Returns 0 on success. */
int img_decode(const uint8_t *png, size_t len, Image *out);
void img_free(Image *img);
int img_has_alpha(const Image *img);

/*
 * Encodes the image as PNG, truecolor (RGB when opaque, RGBA otherwise).
 * Returns a malloc'd buffer or NULL.
 */
uint8_t *png_encode_truecolor(const Image *img, size_t *out_len);

/*
 * Encodes the image as an indexed PNG with at most max_colors colors
 * (median cut). Returns a malloc'd buffer or NULL.
 */
uint8_t *png_encode_palette(const Image *img, int max_colors, size_t *out_len);

/*
 * Encodes the image so the PNG is at most max_len bytes: truecolor first, then
 * 256, 64 and 16 color palettes. Returns NULL when nothing fits.
 */
uint8_t *png_encode_fit(const Image *img, size_t max_len, size_t *out_len);

#endif
