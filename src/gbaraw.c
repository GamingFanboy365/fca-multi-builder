/*
 * gbaraw - convert splash screens between BMP and the raw 240x160 15-bit
 * format PocketNES and PCEAdvance show at start-up (like the .raw files
 * in this package).
 *
 *   gbaraw IN.bmp OUT.raw     picture -> splash
 *   gbaraw IN.raw OUT.bmp     splash -> picture (to view or edit it)
 */
#include "common.h"
#include "image.h"

#include <stdlib.h>
#include <string.h>

static void usage(FILE *f)
{
    fprintf(f,
"Usage: gbaraw [--scale] INPUT OUTPUT\n"
"\n"
"Converts between BMP pictures and raw GBA splash screens (240x160,\n"
"15-bit, 76800 bytes).  The direction follows the file extensions:\n"
"  gbaraw title.bmp splash.raw    make a splash screen\n"
"  gbaraw splash.raw title.bmp    view or edit an existing one\n"
"\n"
"Pictures smaller than 240x160 are centred on black.  Larger ones are\n"
"cropped to the centre, or shrunk to fit with --scale.\n");
}

int main(int argc, char **argv)
{
    const char *in = NULL, *out = NULL, *err;
    int scale = 0, i;
    uint8_t splash[SPLASH_SIZE];

    tool_init(&argc, &argv, "gbaraw");
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale"))
            scale = 1;
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
            printf("gbaraw %s\n", FCA_TOOLS_VERSION);
            return 0;
        } else if (!in)
            in = argv[i];
        else if (!out)
            out = argv[i];
        else {
            usage(stderr);
            return 2;
        }
    }
    if (!in || !out) {
        usage(stderr);
        return 2;
    }

    if (str_ieq(path_ext(out), "bmp")) {
        uint8_t *data, *rgb, *bmp;
        size_t len, blen;
        if (read_file(in, &data, &len) != 0)
            die("cannot read %s", in);
        if (len != SPLASH_SIZE)
            die("%s is %zu bytes; a raw splash screen is %d bytes", in, len, SPLASH_SIZE);
        rgb = xmalloc(SPLASH_W * SPLASH_H * 3);
        splash_to_rgb(data, rgb);
        bmp = bmp_encode(rgb, SPLASH_W, SPLASH_H, &blen);
        if (write_file(out, bmp, blen) != 0)
            die("cannot write %s", out);
        free(bmp);
        free(rgb);
        free(data);
    } else {
        if (load_splash(in, scale, splash, &err) != 0)
            die("%s: %s", in, err);
        if (write_file(out, splash, SPLASH_SIZE) != 0)
            die("cannot write %s", out);
    }
    note("Wrote %s", out);
    return 0;
}
