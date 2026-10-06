/*
 * gbarun - headless mGBA runner for the emulator tests.
 *
 *   gbarun IMAGE.gba FRAMES [FRAME:KEYS[:HOLD] ...]
 *
 * Runs the image for FRAMES frames, pressing KEYS at the given frames
 * (A B s=select S=start R L U D r l), then prints the most common colour
 * on screen as "top RRGGBB COUNT".  Environment:
 *   SHOT=file.bmp     also save a screenshot
 *   SRAM64=1          give the cart 64 KB SRAM (FamicomAdvance, PocketNES)
 *   SAVE_IN=file      load this save file first
 *   SAVE_OUT=file     write the save data afterwards
 * Built against libmgba 0.10 (Debian/Ubuntu libmgba-dev).
 */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/savedata.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"

static void nolog(struct mLogger *l, int c, enum mLogLevel v, const char *f, va_list a)
{
    (void)l; (void)c; (void)v; (void)f; (void)a;
}
static struct mLogger quiet_logger = { .log = nolog };

int main(int argc, char **argv)
{
    struct mCore *core;
    unsigned w, h, i;
    color_t *buf;
    int frames, f;
    const char *s;

    if (argc < 3) {
        fprintf(stderr, "usage: gbarun IMAGE FRAMES [FRAME:KEYS[:HOLD]...]\n");
        return 2;
    }
    mLogSetDefaultLogger(&quiet_logger);
    core = mCoreFind(argv[1]);
    if (!core || !core->init(core))
        return 1;
    mCoreInitConfig(core, NULL);
    core->desiredVideoDimensions(core, &w, &h);
    buf = calloc((size_t)w * h, sizeof(color_t));
    core->setVideoBuffer(core, buf, w);
    if (!mCoreLoadFile(core, argv[1]))
        return 1;
    core->reset(core);
    if (getenv("SRAM64"))
        GBASavedataForceType(&((struct GBA *)core->board)->memory.savedata, SAVEDATA_SRAM512);
    if ((s = getenv("SAVE_IN")) != NULL) {
        FILE *sf = fopen(s, "rb");
        static unsigned char sb[0x20000];
        size_t n = sf ? fread(sb, 1, sizeof sb, sf) : 0;
        unsigned char *copy = malloc(n ? n : 1);
        if (sf)
            fclose(sf);
        memcpy(copy, sb, n);
        if (!n || !core->savedataRestore(core, copy, n, false))
            fprintf(stderr, "could not load %s\n", s);
    }
    frames = atoi(argv[2]);
    for (f = 0; f < frames; f++) {
        uint32_t keys = 0;
        int a;
        for (a = 3; a < argc; a++) {
            int at, hold = 6;
            char k[32], *p;
            if (sscanf(argv[a], "%d:%31[^:]:%d", &at, k, &hold) < 2 || f < at || f >= at + hold)
                continue;
            for (p = k; *p; p++) {
                switch (*p) {
                case 'A': keys |= 1; break;   case 'B': keys |= 2; break;
                case 's': keys |= 4; break;   case 'S': keys |= 8; break;
                case 'R': keys |= 16; break;  case 'L': keys |= 32; break;
                case 'U': keys |= 64; break;  case 'D': keys |= 128; break;
                case 'r': keys |= 256; break; case 'l': keys |= 512; break;
                }
            }
        }
        core->setKeys(core, keys);
        core->runFrame(core);
    }
    {
        unsigned char *rgb = malloc((size_t)w * h * 3);
        unsigned best = 0, best_n = 0;
        for (i = 0; i < w * h; i++) {
            uint32_t c = buf[i];
            rgb[i * 3] = c & 0xFF;
            rgb[i * 3 + 1] = (c >> 8) & 0xFF;
            rgb[i * 3 + 2] = (c >> 16) & 0xFF;
        }
        /* most common colour (screens are mostly flat in these tests) */
        for (i = 0; i < w * h; i += 37) {
            unsigned j, n = 0, c = (rgb[i * 3] << 16) | (rgb[i * 3 + 1] << 8) | rgb[i * 3 + 2];
            for (j = 0; j < w * h; j++)
                if (((rgb[j * 3] << 16) | (rgb[j * 3 + 1] << 8) | rgb[j * 3 + 2]) == c)
                    n++;
            if (n > best_n) {
                best_n = n;
                best = c;
            }
        }
        printf("top %06X %u\n", best, best_n);
        if ((s = getenv("SHOT")) != NULL) {
            size_t n;
            unsigned char *bmp = bmp_encode(rgb, (int)w, (int)h, &n);
            FILE *o = fopen(s, "wb");
            if (o) {
                fwrite(bmp, 1, n, o);
                fclose(o);
            }
        }
    }
    if ((s = getenv("SAVE_OUT")) != NULL) {
        void *sd = NULL;
        size_t n = core->savedataClone(core, &sd);
        FILE *o = fopen(s, "wb");
        if (o) {
            fwrite(sd, 1, n, o);
            fclose(o);
        }
    }
    core->deinit(core);
    return 0;
}
