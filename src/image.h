/*
 * image.h - splash screen images.
 *
 * PocketNES and PCEAdvance accept an optional splash screen right after the
 * emulator: a raw 240x160 picture in the GBA's 15-bit BGR555 format
 * (76800 bytes).  These helpers convert to and from Windows BMP files.
 */
#ifndef FCA_IMAGE_H
#define FCA_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define SPLASH_W 240
#define SPLASH_H 160
#define SPLASH_SIZE (SPLASH_W * SPLASH_H * 2)

/* Decodes an uncompressed BMP (1/4/8-bit palette, 16-bit 555, 24/32-bit)
 * into 8-bit RGB triplets.  Returns NULL and sets *err on failure. */
uint8_t *bmp_decode(const uint8_t *data, size_t len, int *w, int *h, const char **err);

/* Encodes 24-bit RGB into a BMP file.  Returns malloc'd data. */
uint8_t *bmp_encode(const uint8_t *rgb, int w, int h, size_t *out_len);

/* Converts RGB to a 240x160 BGR555 splash.  Smaller pictures are centred
 * on black; larger ones are scaled down to fit (nearest neighbour) when
 * scale is set, otherwise centre-cropped. */
void rgb_to_splash(const uint8_t *rgb, int w, int h, int scale, uint8_t *splash);
void splash_to_rgb(const uint8_t *splash, uint8_t *rgb);

/* Loads a splash from a .raw (must be exactly 76800 bytes) or a .bmp.
 * Returns 0 on success, otherwise -1 with a message in *err. */
int load_splash(const char *path, int scale, uint8_t *splash, const char **err);

#endif
