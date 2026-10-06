/*
 * fcasvedt - inspect and edit GBA save files (SRAM dumps) of
 * FamicomAdvance and PocketNES.
 *
 * A cross-platform replacement for fcasvedt 0.31/0.5 (studio KAZAMA) and
 * the SRAM manager in the old PocketNES ROM Builder.
 *
 * FamicomAdvance save (64 KB):
 *   0x0000  u32 magic 0xA838861A
 *   0x0004  7 directory entries of 40 bytes:
 *           u32 magic (0x04174170 used, 0x41700417 free),
 *           char name[32] (EUC-JP), char ext[4] ("sav")
 *   0x011C  u32 check = XOR of the 71 words at 0x0000..0x011B
 *   slot n (0..6) data: 8 KB at (n + 1) * 0x2000
 *
 * PocketNES save (64 KB, or 32 KB on small carts):
 *   0x0000  u32 0x57A731D7 (or 0x57A731D8)
 *   0x0004  records: u16 size (header + data, word aligned), u16 type
 *           (0 state, 1 SRAM, 2 config), u32 uncompressed size,
 *           u32 frame count, u32 ROM checksum, char title[32], then the
 *           LZO1X-compressed data.  A size of 0 ends the list, followed
 *           by u32 0 and u32 0xFFFFFFFF.
 *   Records stay below 0xE000 (0x6000 on 32 KB carts); the 8 KB above
 *   that is the live SRAM of the game named by the config record.
 */
#include "common.h"
#include "lzo.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

#define FCA_SAVE_MAGIC 0xA838861Au
#define FCA_USED 0x04174170u
#define FCA_FREE 0x41700417u
#define FCA_SLOTS 7
#define FCA_SLOT_SIZE 0x2000
#define FCA_CHECK_OFS 0x11C

#define PNES_ID 0x57A731D7u
#define PNES_ID2 0x57A731D8u
#define PNES_HDR 48
#define PNES_STATE 0
#define PNES_SRAM 1
#define PNES_CONFIG 2
#define PNES_SRAM_SIZE 0x2000

#define SAVE_SIZE 0x10000

enum { FMT_UNKNOWN, FMT_FCA, FMT_PNES };

static uint8_t *sav;
static size_t sav_len;
static int fmt;
static const char *sav_path;

static void usage(FILE *f)
{
    fprintf(f,
"Usage: fcasvedt SAVEFILE [COMMAND [ARGS]] [options]\n"
"\n"
"Edits FamicomAdvance and PocketNES save files (64 KB SRAM dumps).\n"
"\n"
"Commands:\n"
"  list                  show the saves (default)\n"
"  extract N             write save N to a file (-o to name it)\n"
"  extract-all           write every save to the current dir (-o DIR)\n"
"  add FILE              add a save (.sav, or .sta with --state)\n"
"  delete N              remove save N\n"
"  rename N TITLE        change the title of save N\n"
"  create fca|pocketnes  make a new, empty save file\n"
"  live                  PocketNES: write the live 8 KB SRAM (-o FILE)\n"
"\n"
"Options:\n"
"  -o, --output PATH     output file (or directory for extract-all)\n"
"  -t, --title TITLE     title for 'add' (default: file name)\n"
"  -r, --rom FILE        PocketNES 'add': the .nes the save belongs to\n"
"                        (its checksum ties the save to the game)\n"
"  --checksum HEX        PocketNES 'add': give the ROM checksum directly\n"
"  --state               PocketNES 'add': the file is a save state\n"
"  --stored              PocketNES 'extract': use the stored copy even if\n"
"                        the live SRAM belongs to the same game\n"
"  -f, --force           overwrite existing output files\n"
"  -q, --quiet           less output\n"
"  -h, --help, -V, --version\n");
}

/* ------------------------------------------------------------------ */
/* FCA                                                                 */

static uint32_t fca_check(const uint8_t *d)
{
    uint32_t x = 0;
    int i;
    for (i = 0; i < FCA_CHECK_OFS / 4; i++)
        x ^= get_le32(d + i * 4);
    return x;
}

static int fca_valid(const uint8_t *d, size_t len)
{
    int i;
    if (len != SAVE_SIZE || get_le32(d) != FCA_SAVE_MAGIC)
        return 0;
    for (i = 0; i < FCA_SLOTS; i++) {
        uint32_t m = get_le32(d + 4 + i * 40);
        if (m != FCA_USED && m != FCA_FREE)
            return 0;
    }
    return get_le32(d + FCA_CHECK_OFS) == fca_check(d);
}

static void fca_update_check(void)
{
    put_le32(sav + FCA_CHECK_OFS, fca_check(sav));
}

static void fca_init(uint8_t *d)
{
    int i;
    memset(d, 0, SAVE_SIZE);
    put_le32(d, FCA_SAVE_MAGIC);
    for (i = 0; i < FCA_SLOTS; i++)
        put_le32(d + 4 + i * 40, FCA_FREE);
    put_le32(d + FCA_CHECK_OFS, fca_check(d));
}

static int fca_used(int slot)
{
    return get_le32(sav + 4 + slot * 40) == FCA_USED;
}

static char *fca_title(int slot)
{
    return euc_to_utf8((const char *)sav + 8 + slot * 40, 32);
}

static char *fca_ext(int slot)
{
    return xstrndup((const char *)sav + 40 + slot * 40,
                    strnlen((const char *)sav + 40 + slot * 40, 4));
}

/* ------------------------------------------------------------------ */
/* PocketNES                                                           */

typedef struct {
    size_t ofs;
    unsigned size, type;
    uint32_t usize, frames, checksum;
    char title[33];
} pnes_rec;

static size_t pnes_limit(void)
{
    return sav_len >= SAVE_SIZE ? 0xE000 : 0x6000;
}

static int pnes_valid(const uint8_t *d, size_t len)
{
    uint32_t id;
    if (len != SAVE_SIZE && len != 0x8000)
        return 0;
    id = get_le32(d);
    return id == PNES_ID || id == PNES_ID2;
}

/* Walks the record list.  Returns the number of records (up to max) or -1
 * if the list is damaged; *end receives the offset of the terminator. */
static int pnes_scan(pnes_rec *out, int max, size_t *end)
{
    size_t p = 4, limit = pnes_limit();
    int n = 0;
    while (p + 2 <= limit) {
        unsigned size = get_le16(sav + p);
        if (size == 0)
            break;
        if (size < PNES_HDR || (size & 3) || p + size > limit)
            return -1;
        if (n < max) {
            pnes_rec *r = &out[n];
            r->ofs = p;
            r->size = size;
            r->type = get_le16(sav + p + 2);
            r->usize = get_le32(sav + p + 4);
            r->frames = get_le32(sav + p + 8);
            r->checksum = get_le32(sav + p + 12);
            memcpy(r->title, sav + p + 16, 32);
            r->title[32] = 0;
        }
        n++;
        p += size;
    }
    if (end)
        *end = p;
    return n;
}

static void pnes_init(uint8_t *d, size_t len)
{
    memset(d, 0, len);
    put_le32(d, PNES_ID);
    put_le32(d + 4, 0);
    put_le32(d + 8, 0xFFFFFFFFu);
}

/* Rewrites the tail after the last record the way PocketNES does. */
static void pnes_terminate(size_t end)
{
    size_t limit = pnes_limit();
    if (end + 8 <= limit) {
        put_le32(sav + end, 0);
        put_le32(sav + end + 4, 0xFFFFFFFFu);
        memset(sav + end + 8, 0, limit - end - 8);
    } else if (end + 2 <= limit) {
        memset(sav + end, 0, limit - end);
    }
}

static uint32_t pnes_rom_checksum(const uint8_t *p, size_t len)
{
    uint32_t sum = 0;
    int i;
    for (i = 0; i < 128; i++, p += 128)
        if ((size_t)i * 128 + 4 <= len)
            sum += get_le32(p);
    return sum;
}

/* Checksum PocketNES computes for a game: over the ROM data after the
 * 16-byte iNES header.  fcabuild drops trainers, so skip one here too. */
static int checksum_of_rom(const char *path, uint32_t *out)
{
    uint8_t *d;
    size_t len, skip = 16;
    if (read_file(path, &d, &len) != 0)
        return -1;
    if (len < 16 || memcmp(d, "NES\x1a", 4)) {
        free(d);
        return -2;
    }
    if (d[6] & 4)
        skip += 512;
    if (len < skip + 128 * 128) {
        free(d);
        return -3;
    }
    *out = pnes_rom_checksum(d + skip, len - skip);
    free(d);
    return 0;
}

static int pnes_config(pnes_rec *recs, int n, uint32_t *owner)
{
    int i;
    for (i = 0; i < n; i++)
        if (recs[i].type == PNES_CONFIG) {
            *owner = get_le32(sav + recs[i].ofs + 8);
            return 1;
        }
    return 0;
}

/* Decompresses a record into a malloc'd buffer.  The header's size field
 * isn't reliable (PocketNES 9.8 stores the compressed length there, later
 * versions the uncompressed one, and both ignore it when loading), so the
 * record size bounds the input and the output may be up to 64 KB. */
static uint8_t *pnes_unpack(const pnes_rec *r, size_t *len)
{
    uint8_t *out = xmalloc(0x10000);
    *len = 0x10000;
    if (lzo1x_decompress_safe(sav + r->ofs + PNES_HDR, r->size - PNES_HDR, out, len) != 0) {
        free(out);
        return NULL;
    }
    return out;
}

static const char *pnes_type_name(unsigned t)
{
    return t == PNES_STATE ? "state" : t == PNES_SRAM ? "SRAM" : t == PNES_CONFIG ? "config" : "?";
}

/* ------------------------------------------------------------------ */

static void load(const char *path)
{
    if (read_file(path, &sav, &sav_len) != 0)
        die("cannot read %s", path);
    if (fca_valid(sav, sav_len))
        fmt = FMT_FCA;
    else if (pnes_valid(sav, sav_len))
        fmt = FMT_PNES;
    else if (sav_len == SAVE_SIZE && get_le32(sav) == FCA_SAVE_MAGIC)
        die("%s: FamicomAdvance save with a bad checksum or directory", path);
    else
        die("%s: not a FamicomAdvance or PocketNES save file "
            "(use 'create' to make a new one)", path);
}

static void save(void)
{
    if (write_file(sav_path, sav, sav_len) != 0)
        die("cannot write %s", sav_path);
}

static int parse_index(const char *s, int count)
{
    long n;
    if (parse_long(s, &n) != 0 || n < 1 || n > count)
        die("no save number %s (there %s %d)", s, count == 1 ? "is" : "are", count);
    return (int)n - 1;
}

static void sanitize_filename(char *s)
{
    for (; *s; s++)
        if (strchr("\\/:*?\"<>|", *s) || (unsigned char)*s < 0x20)
            *s = '_';
}

static void write_out(const char *path, const uint8_t *d, size_t len, int force)
{
    if (!force && file_exists(path))
        die("%s already exists (use --force to overwrite)", path);
    if (write_file(path, d, len) != 0)
        die("cannot write %s", path);
    note("Wrote %s (%zu bytes)", path, len);
}

static void cmd_list(void)
{
    if (fmt == FMT_FCA) {
        int i, used = 0;
        printf("%s: FamicomAdvance save\n", sav_path);
        for (i = 0; i < FCA_SLOTS; i++) {
            if (fca_used(i)) {
                char *t = fca_title(i), *e = fca_ext(i);
                printf("  %d  %s.%s\n", i + 1, t, e);
                free(t);
                free(e);
                used++;
            } else {
                printf("  %d  (empty)\n", i + 1);
            }
        }
        printf("%d of %d slots used\n", used, FCA_SLOTS);
    } else {
        pnes_rec recs[1024];
        size_t end;
        int n = pnes_scan(recs, 1024, &end), i;
        uint32_t owner = 0;
        int have_owner;
        if (n < 0)
            die("%s: the PocketNES save list is damaged", sav_path);
        have_owner = pnes_config(recs, n, &owner);
        printf("%s: PocketNES save\n", sav_path);
        for (i = 0; i < n; i++) {
            if (recs[i].type == PNES_CONFIG) {
                printf("  %d  [config]%s\n", i + 1,
                       have_owner && owner ? "" : " (no game owns the live SRAM)");
                continue;
            }
            {
                size_t dlen;
                uint8_t *d = pnes_unpack(&recs[i], &dlen);
                char sz[24];
                if (d)
                    snprintf(sz, sizeof sz, "%5zu bytes", dlen);
                else
                    snprintf(sz, sizeof sz, "  DAMAGED  ");
                free(d);
                printf("  %d  %-5s %-31s  %s  checksum %08X%s\n", i + 1,
                       pnes_type_name(recs[i].type), recs[i].title, sz,
                       (unsigned)recs[i].checksum,
                       recs[i].type == PNES_SRAM && have_owner && owner == recs[i].checksum
                           ? "  (live SRAM is newer)" : "");
            }
        }
        printf("%d record%s, %zu of %zu bytes used\n", n, n == 1 ? "" : "s",
               end + 8, pnes_limit());
    }
}

static void cmd_extract(int idx, const char *out, int stored, int force)
{
    char *name;
    if (fmt == FMT_FCA) {
        char *t, *e;
        if (idx < 0 || idx >= FCA_SLOTS || !fca_used(idx))
            die("slot %d is empty", idx + 1);
        t = fca_title(idx);
        e = fca_ext(idx);
        sanitize_filename(t);
        name = out ? xstrdup(out) : xasprintf("%s.%s", t, *e ? e : "sav");
        write_out(name, sav + (idx + 1) * FCA_SLOT_SIZE, FCA_SLOT_SIZE, force);
        free(t);
        free(e);
    } else {
        pnes_rec recs[1024];
        int n = pnes_scan(recs, 1024, NULL);
        uint32_t owner = 0;
        uint8_t *data;
        size_t dlen;
        pnes_rec *r;
        char title[33];

        if (n < 0)
            die("the PocketNES save list is damaged");
        if (idx < 0 || idx >= n)
            die("no record %d", idx + 1);
        r = &recs[idx];
        if (r->type == PNES_CONFIG)
            die("record %d is PocketNES's settings, not a save", idx + 1);
        memcpy(title, r->title, 33);
        sanitize_filename(title);
        name = out ? xstrdup(out)
                   : xasprintf("%s.%s", *title ? title : "save", r->type == PNES_STATE ? "sta" : "sav");
        if (r->type == PNES_SRAM && !stored && sav_len >= 0x10000 &&
            pnes_config(recs, n, &owner) && owner == r->checksum) {
            note("Using the live SRAM at 0xE000 (newer than the stored copy; --stored to skip)");
            write_out(name, sav + pnes_limit(), PNES_SRAM_SIZE, force);
            free(name);
            return;
        }
        data = pnes_unpack(r, &dlen);
        if (!data)
            die("record %d: compressed data is damaged", idx + 1);
        write_out(name, data, dlen, force);
        free(data);
    }
    free(name);
}

static void cmd_extract_all(const char *dir, int force)
{
    int i, count = 0;
    char *base;
    if (fmt == FMT_FCA) {
        for (i = 0; i < FCA_SLOTS; i++) {
            char *t, *e, *name;
            if (!fca_used(i))
                continue;
            t = fca_title(i);
            e = fca_ext(i);
            sanitize_filename(t);
            base = xasprintf("%s.%s", t, *e ? e : "sav");
            name = dir ? path_join(dir, base) : xstrdup(base);
            cmd_extract(i, name, 0, force);
            free(name);
            free(base);
            free(t);
            free(e);
            count++;
        }
    } else {
        pnes_rec recs[1024];
        int n = pnes_scan(recs, 1024, NULL);
        if (n < 0)
            die("the PocketNES save list is damaged");
        for (i = 0; i < n; i++) {
            char title[33], *name;
            if (recs[i].type == PNES_CONFIG)
                continue;
            memcpy(title, recs[i].title, 33);
            sanitize_filename(title);
            base = xasprintf("%02d %s.%s", i + 1, *title ? title : "save",
                             recs[i].type == PNES_STATE ? "sta" : "sav");
            name = dir ? path_join(dir, base) : xstrdup(base);
            cmd_extract(i, name, 0, force);
            free(name);
            free(base);
            count++;
        }
    }
    if (!count)
        note("No saves to extract.");
}

static void cmd_add(const char *file, const char *title, const char *rom,
                    const char *checksum_hex, int state)
{
    uint8_t *data;
    size_t len;
    char *stem;

    if (read_file(file, &data, &len) != 0)
        die("cannot read %s", file);
    stem = title ? xstrdup(title) : path_stem(file);

    if (fmt == FMT_FCA) {
        int i, slot = -1, lossy;
        char *name;
        if (state)
            die("FamicomAdvance saves have no save states");
        if (len > FCA_SLOT_SIZE)
            die("%s is %zu bytes; FamicomAdvance saves hold at most 8192", file, len);
        for (i = 0; i < FCA_SLOTS && slot < 0; i++)
            if (!fca_used(i))
                slot = i;
        if (slot < 0)
            die("all %d slots are in use; delete one first", FCA_SLOTS);
        /* FCA matches a save to its game by title, so it must equal the
         * title the game has in the image (NES2FCA's TITLE). */
        name = title_to_euc(stem, 21, 31, &lossy);
        if (lossy)
            warn("title has characters the FCA font can't show");
        memset(sav + 4 + slot * 40, 0, 40);
        put_le32(sav + 4 + slot * 40, FCA_USED);
        memcpy(sav + 8 + slot * 40, name, strlen(name));
        memcpy(sav + 40 + slot * 40, "sav", 3);
        memset(sav + (slot + 1) * FCA_SLOT_SIZE, 0, FCA_SLOT_SIZE);
        memcpy(sav + (slot + 1) * FCA_SLOT_SIZE, data, len);
        fca_update_check();
        note("Added \"%s\" to slot %d", stem, slot + 1);
        free(name);
    } else {
        pnes_rec recs[1024];
        size_t end, clen, rsize, limit = pnes_limit();
        int n = pnes_scan(recs, 1024, &end), i, replace = -1, lossy;
        uint32_t sum;
        uint8_t *rec;
        char *t;

        if (n < 0)
            die("the PocketNES save list is damaged");
        if (rom) {
            int r = checksum_of_rom(rom, &sum);
            if (r == -1) die("cannot read %s", rom);
            if (r == -2) die("%s is not an NES ROM", rom);
            if (r == -3) die("%s is too small to be an NES ROM", rom);
        } else if (checksum_hex) {
            char *e;
            sum = (uint32_t)strtoul(checksum_hex, &e, 16);
            if (*e || !*checksum_hex)
                die("bad checksum '%s'", checksum_hex);
        } else {
            die("PocketNES saves are tied to their game: give --rom GAME.nes or --checksum");
        }
        if (!state && len != PNES_SRAM_SIZE)
            warn("%s is %zu bytes; NES SRAM saves are normally 8192", file, len);
        if (len > 0xFFFF)
            die("%s is too big for a PocketNES save", file);

        rec = xcalloc(PNES_HDR + LZO1X_BOUND(len) + 4, 1);
        clen = lzo1x_compress(data, len, rec + PNES_HDR);
        rsize = (PNES_HDR + clen + 3) & ~(size_t)3;
        if (rsize > 0xFFFF)
            die("compressed save is too big");
        t = title_to_ascii(stem, 31, &lossy);
        put_le16(rec, (uint16_t)rsize);
        put_le16(rec + 2, (uint16_t)(state ? PNES_STATE : PNES_SRAM));
        put_le32(rec + 4, (uint32_t)len);
        put_le32(rec + 8, 0);
        put_le32(rec + 12, sum);
        memcpy(rec + 16, t, strlen(t));

        /* PocketNES keeps one SRAM save per game: replace an existing one. */
        if (!state)
            for (i = 0; i < n; i++)
                if (recs[i].type == PNES_SRAM && recs[i].checksum == sum)
                    replace = i;
        if (replace >= 0) {
            size_t at = recs[replace].ofs, old = recs[replace].size;
            if (end - old + rsize + 8 > limit)
                die("not enough free space in the save file");
            memmove(sav + at + rsize, sav + at + old, end - at - old);
            memcpy(sav + at, rec, rsize);
            end = end - old + rsize;
            note("Replaced the SRAM save of \"%s\" (checksum %08X)", t, (unsigned)sum);
        } else {
            if (end + rsize + 8 > limit)
                die("not enough free space in the save file (%zu bytes needed, %zu free)",
                    rsize, limit > end + 8 ? limit - end - 8 : 0);
            memcpy(sav + end, rec, rsize);
            end += rsize;
            note("Added %s \"%s\" (checksum %08X)", state ? "save state" : "SRAM save", t,
                 (unsigned)sum);
        }
        pnes_terminate(end);
        free(t);
        free(rec);
    }
    free(stem);
    free(data);
    save();
}

static void cmd_delete(int idx)
{
    if (fmt == FMT_FCA) {
        if (idx < 0 || idx >= FCA_SLOTS || !fca_used(idx))
            die("slot %d is empty", idx + 1);
        memset(sav + 4 + idx * 40, 0, 40);
        put_le32(sav + 4 + idx * 40, FCA_FREE);
        memset(sav + (idx + 1) * FCA_SLOT_SIZE, 0, FCA_SLOT_SIZE);
        fca_update_check();
    } else {
        pnes_rec recs[1024];
        size_t end;
        int n = pnes_scan(recs, 1024, &end);
        size_t at, size;
        if (n < 0)
            die("the PocketNES save list is damaged");
        if (idx < 0 || idx >= n)
            die("no record %d", idx + 1);
        if (recs[idx].type == PNES_CONFIG)
            die("record %d holds PocketNES's settings; it can't be deleted", idx + 1);
        at = recs[idx].ofs;
        size = recs[idx].size;
        memmove(sav + at, sav + at + size, end - at - size);
        pnes_terminate(end - size);
    }
    note("Deleted save %d", idx + 1);
    save();
}

static void cmd_rename(int idx, const char *title)
{
    int lossy = 0;
    if (fmt == FMT_FCA) {
        char *name;
        if (idx < 0 || idx >= FCA_SLOTS || !fca_used(idx))
            die("slot %d is empty", idx + 1);
        name = title_to_euc(title, 21, 31, &lossy);
        memset(sav + 8 + idx * 40, 0, 32);
        memcpy(sav + 8 + idx * 40, name, strlen(name));
        fca_update_check();
        free(name);
    } else {
        pnes_rec recs[1024];
        int n = pnes_scan(recs, 1024, NULL);
        char *t;
        if (n < 0)
            die("the PocketNES save list is damaged");
        if (idx < 0 || idx >= n || recs[idx].type == PNES_CONFIG)
            die("no save %d", idx + 1);
        t = title_to_ascii(title, 31, &lossy);
        memset(sav + recs[idx].ofs + 16, 0, 32);
        memcpy(sav + recs[idx].ofs + 16, t, strlen(t));
        free(t);
    }
    if (lossy)
        warn("some title characters were replaced");
    note("Renamed save %d", idx + 1);
    save();
}

static int count_entries(void)
{
    if (fmt == FMT_FCA)
        return FCA_SLOTS;
    {
        pnes_rec recs[1024];
        int n = pnes_scan(recs, 1024, NULL);
        if (n < 0)
            die("the PocketNES save list is damaged");
        return n;
    }
}

int main(int argc, char **argv)
{
    const char *args[4] = {0}, *out = NULL, *title = NULL, *rom = NULL, *checksum = NULL;
    int nargs = 0, i, state = 0, stored = 0, force = 0;
    const char *cmd;

    tool_init(&argc, &argv, "fcasvedt");
    for (i = 1; i < argc; i++) {
        char *a = argv[i], *val = NULL;
        if (a[0] != '-' || !a[1]) {
            if (nargs < 4)
                args[nargs++] = a;
            else
                die("too many arguments");
            continue;
        }
        if (a[1] == '-' && (val = strchr(a, '=')) != NULL)
            *val++ = 0;
#define IS(s, l) (!strcmp(a, s) || !strcmp(a, l))
#define VALUE() (val ? val : (i + 1 < argc ? argv[++i] : (die("%s needs a value", a), "")))
        if (IS("-h", "--help")) { usage(stdout); return 0; }
        else if (IS("-V", "--version")) { printf("fcasvedt %s\n", FCA_TOOLS_VERSION); return 0; }
        else if (IS("-o", "--output")) out = VALUE();
        else if (IS("-t", "--title")) title = VALUE();
        else if (IS("-r", "--rom")) rom = VALUE();
        else if (!strcmp(a, "--checksum")) checksum = VALUE();
        else if (!strcmp(a, "--state")) state = 1;
        else if (!strcmp(a, "--stored")) stored = 1;
        else if (IS("-f", "--force")) force = 1;
        else if (IS("-q", "--quiet")) quiet = 1;
        else {
            fprintf(stderr, "%s: unknown option %s\n\n", progname, a);
            usage(stderr);
            return 2;
        }
#undef IS
#undef VALUE
    }
    if (nargs < 1) {
        usage(stderr);
        return 2;
    }
    sav_path = args[0];
    cmd = nargs > 1 ? args[1] : "list";

    if (!strcmp(cmd, "create")) {
        const char *kind = nargs > 2 ? args[2] : NULL;
        if (!kind)
            die("create needs a format: fca or pocketnes");
        if (!force && file_exists(sav_path))
            die("%s already exists (use --force to overwrite)", sav_path);
        sav_len = SAVE_SIZE;
        sav = xmalloc(sav_len);
        if (str_ieq(kind, "fca"))
            fca_init(sav);
        else if (str_ieq(kind, "pocketnes") || str_ieq(kind, "pnes"))
            pnes_init(sav, sav_len);
        else
            die("unknown format '%s' (fca or pocketnes)", kind);
        save();
        note("Created %s", sav_path);
        return 0;
    }

    load(sav_path);
    if (!strcmp(cmd, "list") || !strcmp(cmd, "ls")) {
        cmd_list();
    } else if (!strcmp(cmd, "extract") || !strcmp(cmd, "x")) {
        if (nargs < 3)
            die("extract needs a save number (see 'list')");
        cmd_extract(parse_index(args[2], count_entries()), out, stored, force);
    } else if (!strcmp(cmd, "extract-all")) {
        if (out)
            make_dir(out);
        cmd_extract_all(out, force);
    } else if (!strcmp(cmd, "add")) {
        if (nargs < 3)
            die("add needs a file");
        cmd_add(args[2], title, rom, checksum, state);
    } else if (!strcmp(cmd, "delete") || !strcmp(cmd, "rm")) {
        if (nargs < 3)
            die("delete needs a save number");
        cmd_delete(parse_index(args[2], count_entries()));
    } else if (!strcmp(cmd, "rename")) {
        if (nargs < 4)
            die("rename needs a save number and a title");
        cmd_rename(parse_index(args[2], count_entries()), args[3]);
    } else if (!strcmp(cmd, "live")) {
        if (fmt != FMT_PNES || sav_len < SAVE_SIZE)
            die("'live' is for 64 KB PocketNES saves");
        write_out(out ? out : "live.sav", sav + pnes_limit(), PNES_SRAM_SIZE, force);
    } else {
        die("unknown command '%s' (see --help)", cmd);
    }
    return 0;
}
