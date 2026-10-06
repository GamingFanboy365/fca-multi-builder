/*
 * image.c - BMP <-> GBA splash screen conversion.
 */
#include "image.h"
#include "common.h"

#include <stdlib.h>
#include <string.h>

uint8_t *bmp_decode(const uint8_t *d, size_t len, int *w, int *h, const char **err)
{
    uint32_t off, hdr, comp, ncolors;
    int32_t width, height;
    int bpp, bottom_up, x, y;
    size_t stride;
    const uint8_t *pal;
    uint8_t *rgb;

    if (len < 26 || d[0] != 'B' || d[1] != 'M') {
        *err = "not a BMP file";
        return NULL;
    }
    off = get_le32(d + 10);
    hdr = get_le32(d + 14);
    if (hdr == 12) { /* OS/2 BITMAPCOREHEADER */
        width = get_le16(d + 18);
        height = (int16_t)get_le16(d + 20);
        bpp = get_le16(d + 24);
        comp = 0;
        ncolors = 0;
    } else if (hdr >= 40 && len >= 54) {
        width = (int32_t)get_le32(d + 18);
        height = (int32_t)get_le32(d + 22);
        bpp = get_le16(d + 28);
        comp = get_le32(d + 30);
        ncolors = get_le32(d + 46);
    } else {
        *err = "unsupported BMP header";
        return NULL;
    }
    if (comp != 0 && !(comp == 3 && (bpp == 32 || bpp == 16))) {
        *err = "compressed BMP files are not supported (save it uncompressed)";
        return NULL;
    }
    if (width <= 0 || height == 0 || width > 8192 || height > 8192 || height < -8192) {
        *err = "unsupported BMP dimensions";
        return NULL;
    }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) {
        *err = "unsupported BMP bit depth";
        return NULL;
    }
    bottom_up = height > 0;
    if (height < 0)
        height = -height;
    stride = (((size_t)width * (size_t)bpp + 31) / 32) * 4;
    if (off > len || stride * (size_t)height > len - off) {
        *err = "truncated BMP file";
        return NULL;
    }
    pal = d + 14 + hdr;
    if (bpp <= 8) {
        size_t entry = hdr == 12 ? 3 : 4;
        if (!ncolors)
            ncolors = 1u << bpp;
        if ((size_t)(pal - d) + ncolors * entry > off) {
            *err = "truncated BMP palette";
            return NULL;
        }
    }
    rgb = xmalloc((size_t)width * (size_t)height * 3);
    for (y = 0; y < height; y++) {
        const uint8_t *row = d + off + stride * (size_t)(bottom_up ? height - 1 - y : y);
        uint8_t *dst = rgb + (size_t)y * (size_t)width * 3;
        for (x = 0; x < width; x++, dst += 3) {
            uint32_t idx;
            switch (bpp) {
            case 1: case 4: case 8:
                idx = (row[(size_t)x * bpp / 8] >> (8 - bpp - (x * bpp) % 8)) & ((1u << bpp) - 1);
                if (idx >= ncolors)
                    idx = 0;
                {
                    const uint8_t *c = pal + idx * (hdr == 12 ? 3 : 4);
                    dst[0] = c[2]; dst[1] = c[1]; dst[2] = c[0];
                }
                break;
            case 16: {
                unsigned v = get_le16(row + x * 2);
                dst[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31);
                dst[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
                dst[2] = (uint8_t)((v & 31) * 255 / 31);
                break;
            }
            default: {
                const uint8_t *c = row + (size_t)x * (size_t)(bpp / 8);
                dst[0] = c[2]; dst[1] = c[1]; dst[2] = c[0];
                break;
            }
            }
        }
    }
    *w = width;
    *h = height;
    return rgb;
}

uint8_t *bmp_encode(const uint8_t *rgb, int w, int h, size_t *out_len)
{
    size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3;
    size_t size = 54 + stride * (size_t)h;
    uint8_t *d = xcalloc(size, 1);
    int x, y;

    d[0] = 'B'; d[1] = 'M';
    put_le32(d + 2, (uint32_t)size);
    put_le32(d + 10, 54);
    put_le32(d + 14, 40);
    put_le32(d + 18, (uint32_t)w);
    put_le32(d + 22, (uint32_t)h);
    put_le16(d + 26, 1);
    put_le16(d + 28, 24);
    put_le32(d + 34, (uint32_t)(stride * (size_t)h));
    put_le32(d + 38, 2835);
    put_le32(d + 42, 2835);
    for (y = 0; y < h; y++) {
        uint8_t *row = d + 54 + stride * (size_t)(h - 1 - y);
        for (x = 0; x < w; x++) {
            const uint8_t *s = rgb + ((size_t)y * (size_t)w + (size_t)x) * 3;
            row[x * 3] = s[2];
            row[x * 3 + 1] = s[1];
            row[x * 3 + 2] = s[0];
        }
    }
    *out_len = size;
    return d;
}

void rgb_to_splash(const uint8_t *rgb, int w, int h, int scale, uint8_t *splash)
{
    int x, y, ox = 0, oy = 0, dw = w, dh = h;
    double sx = 1, sy = 1;

    memset(splash, 0, SPLASH_SIZE);
    if (scale && (w > SPLASH_W || h > SPLASH_H)) {
        double f = (double)SPLASH_W / w;
        if ((double)SPLASH_H / h < f)
            f = (double)SPLASH_H / h;
        dw = (int)(w * f + 0.5);
        dh = (int)(h * f + 0.5);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        if (dw > SPLASH_W) dw = SPLASH_W;
        if (dh > SPLASH_H) dh = SPLASH_H;
        sx = (double)w / dw;
        sy = (double)h / dh;
    }
    ox = (SPLASH_W - dw) / 2;
    oy = (SPLASH_H - dh) / 2;
    for (y = 0; y < SPLASH_H; y++) {
        for (x = 0; x < SPLASH_W; x++) {
            int srcx = (int)((x - ox) * sx), srcy = (int)((y - oy) * sy);
            const uint8_t *p;
            unsigned v;
            if (x - ox < 0 || y - oy < 0 || x - ox >= dw || y - oy >= dh)
                continue;
            if (srcx >= w) srcx = w - 1;
            if (srcy >= h) srcy = h - 1;
            p = rgb + ((size_t)srcy * (size_t)w + (size_t)srcx) * 3;
            v = (unsigned)(p[0] >> 3) | ((unsigned)(p[1] >> 3) << 5) | ((unsigned)(p[2] >> 3) << 10);
            put_le16(splash + (y * SPLASH_W + x) * 2, (uint16_t)v);
        }
    }
}

void splash_to_rgb(const uint8_t *splash, uint8_t *rgb)
{
    int i;
    for (i = 0; i < SPLASH_W * SPLASH_H; i++) {
        unsigned v = get_le16(splash + i * 2);
        rgb[i * 3] = (uint8_t)((v & 31) * 255 / 31);
        rgb[i * 3 + 1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
        rgb[i * 3 + 2] = (uint8_t)(((v >> 10) & 31) * 255 / 31);
    }
}

int load_splash(const char *path, int scale, uint8_t *splash, const char **err)
{
    uint8_t *data, *rgb;
    size_t len;
    int w, h;

    if (read_file(path, &data, &len) != 0) {
        *err = "cannot read file";
        return -1;
    }
    if (len >= 2 && data[0] == 'B' && data[1] == 'M') {
        rgb = bmp_decode(data, len, &w, &h, err);
        free(data);
        if (!rgb)
            return -1;
        rgb_to_splash(rgb, w, h, scale, splash);
        free(rgb);
        return 0;
    }
    if (len != SPLASH_SIZE) {
        free(data);
        *err = "a raw splash screen must be exactly 76800 bytes (240x160, 15-bit)";
        return -1;
    }
    memcpy(splash, data, SPLASH_SIZE);
    free(data);
    return 0;
}
