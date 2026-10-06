/*
 * fca-mkfs - build a FamicomAdvance file-system image.
 *
 * Portable version of fca-mkfs.c from the FamicomAdvance 0.2 package
 * (KIKU, 2002).  Same command line and output, but the header is written
 * explicitly as little-endian 32-bit fields, so it also works on 64-bit
 * systems (the original used "long", which is 8 bytes on 64-bit Linux and
 * produced broken images), and it builds on Windows.
 *
 *   usage: fca-mkfs [-c] [-b base-file] out-file file1 file2 ...
 *
 *   -b  base image to start from (default shell.bin; use fca.gba for 0.2)
 *   -c  don't write the end-of-list record
 *
 * Each file is stored as: u32 0x04174170, char name[32], char ext[4],
 * u32 length, data padded to 4 bytes.  Names that aren't plain ASCII are
 * converted to EUC-JP, the encoding the FamicomAdvance font uses.
 */
#include "common.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

#define FILE_MAGIC 0x04174170u
#define FILE_END_MAGIC 0x41700417u
#define MAX_NAME_LEN 32
#define MAX_EXT_LEN 4
#define HEADER_LEN (4 + MAX_NAME_LEN + MAX_EXT_LEN + 4)

static void usage(void)
{
    printf("usage: fca-mkfs [-c] [-b base-file] out-file file1 file2 ...\n");
    exit(1);
}

static void add_file(buf_t *out, const char *path)
{
    uint8_t h[HEADER_LEN], *data;
    size_t len, i;
    char *stem = path_stem(path), *name;
    const char *ext = path_ext(path);

    if (read_file(path, &data, &len) != 0)
        die("cannot read %s", path);
    if (!*ext)
        ext = "???";
    name = title_to_euc(stem, MAX_NAME_LEN, MAX_NAME_LEN - 1, NULL);
    printf("%s %s %s\n", path, stem, ext);

    memset(h, 0, sizeof h);
    put_le32(h, FILE_MAGIC);
    memcpy(h + 4, name, strlen(name));
    for (i = 0; i < MAX_EXT_LEN - 1 && ext[i]; i++)
        h[4 + MAX_NAME_LEN + i] = (uint8_t)ext[i];
    put_le32(h + 4 + MAX_NAME_LEN + MAX_EXT_LEN, (uint32_t)len);
    buf_append(out, h, sizeof h);
    buf_append(out, data, len);
    buf_pad_to(out, 4); /* align to 4 bytes */
    free(data);
    free(name);
    free(stem);
}

int main(int argc, char **argv)
{
    const char *base_file = "shell.bin", *out_file = NULL;
    int dont_close = 0, i;
    buf_t out = {0};
    uint8_t *data;
    size_t len;

    tool_init(&argc, &argv, "fca-mkfs");
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "-c"))
            dont_close = 1;
        else if (!strcmp(argv[i], "-b") && i + 1 < argc)
            base_file = argv[++i];
        else if (!strncmp(argv[i], "-b", 2) && argv[i][2])
            base_file = argv[i] + 2;
        else if (!strcmp(argv[i], "--version")) {
            printf("fca-mkfs %s\n", FCA_TOOLS_VERSION);
            return 0;
        } else
            usage();
    }
    if (i >= argc)
        usage();
    out_file = argv[i++];

    if (read_file(base_file, &data, &len) != 0)
        die("cannot read %s", base_file);
    buf_append(&out, data, len);
    free(data);

    for (; i < argc; i++)
        add_file(&out, argv[i]);

    if (!dont_close) {
        uint8_t h[HEADER_LEN];
        memset(h, 0, sizeof h);
        put_le32(h, FILE_END_MAGIC);
        buf_append(&out, h, sizeof h);
    }
    if (write_file(out_file, out.data, out.len) != 0)
        die("cannot write %s", out_file);
    buf_free(&out);
    return 0;
}
