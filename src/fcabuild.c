/*
 * fcabuild - build GBA compilation images for FamicomAdvance (FCA),
 * PocketNES, PCEAdvance and GBonGBA.
 *
 * A cross-platform replacement for NES2FCA 0.92 (studio KAZAMA, 2003) that
 * produces the same image layouts, plus the conveniences of the old
 * PocketNES/PCEAdvance ROM Builder (splash screens, title clean-up, cart
 * size checks and splitting a big ROM set over several images).
 *
 * Image layouts (all integers little-endian):
 *
 *   FCA file system (FamicomAdvance, fca-mkfs, Easy MB2GBA):
 *     shell, then per file: u32 magic 0x04174170, char name[32] (EUC-JP),
 *     char ext[4], u32 length, data padded to 4 bytes.
 *   PocketNES:
 *     shell, [splash], then per ROM: char title[32], u32 size, u32 flags,
 *     u32 follow, u32 reserved, .nes file.
 *   PCEAdvance:
 *     shell, [splash], then per ROM: char title[32], u32 size, u32 flags,
 *     u32 follow, u32 reserved, "NES\x1a", "@" + 11 spaces, then the HuCard
 *     data without any 512-byte copier header; size = data + 16.
 *   GBonGBA:
 *     shell, then the .gb files back to back.
 *
 * NES2FCA always ended the image with an FCA end-of-list record (u32
 * 0x41700417 + 40 zero bytes); fcabuild does the same.
 */
#include "common.h"
#include "gba.h"
#include "image.h"
#include "ini.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

enum { T_FCA = 0, T_PNES = 1, T_PCEA = 2, T_GBONGBA = 3, T_PGB = 4 };

#define FCA_MAGIC 0x04174170u
#define FCA_END_MAGIC 0x41700417u
#define FCA_HEADER_LEN 44
#define PNES_HEADER_LEN 48
#define PCEA_HEADER_LEN 64
#define MULTIBOOT_LIMIT (192 * 1024)
#define DEFAULT_CART_SIZE (32L * 1024 * 1024)

/* The nes2fca.cfg shipped with NES2FCA, used when none is found on disk. */
static const char builtin_cfg[] =
    "[list]\n"
    "num=6\n"
    "list0=fca-v01\n"
    "list1=fca-v02\n"
    "list2=PocketNES\n"
    "list3=PCEAdvance\n"
    "list4=GBonGBA\n"
    "list5=Easymb2gba\n"
    "[fca-v01]\n"
    "shell=shell.bin\n"
    "file0=emu.bin,emu,bin\n"
    "file1=emuslow.bin,emuslow,bin\n"
    "file2=font.dat,font,dat\n"
    "file3=mapper0.bin,mapper0,bin\n"
    "file4=mapper1.bin,mapper1,bin\n"
    "file5=mapper2.bin,mapper2,bin\n"
    "file6=mapper3.bin,mapper3,bin\n"
    "file7=mapper4.bin,mapper4,bin\n"
    "[fca-v02]\n"
    "shell=fca.gba\n"
    "[PocketNES]\n"
    "shell=PocketNES.GBA\n"
    "type=PNES\n"
    "[PCEAdvance]\n"
    "shell=PCEAdvance.GBA\n"
    "type=PCEA\n"
    "[GBonGBA]\n"
    "shell=gbongba-0.4.gba\n"
    "type=GBONGBA\n"
    "[Easymb2GBA]\n"
    "shell=Easymb2gba.gba\n"
    "type=Easymb2GBA\n";

typedef struct {
    char *src, *name, *ext;
} fixed_file;

typedef struct {
    char *name;
    char *shell;
    int type;
    fixed_file *files;
    int nfiles;
} target_t;

typedef struct {
    char *path;        /* as resolved on disk */
    char *title;       /* UTF-8, before conversion */
    int title_set;     /* given explicitly (list file / ini) */
    long flags, follow, reserved;
    int flags_set, follow_set, reserved_set;
    int flags_from_list;
    long file_size;
    buf_t entry;       /* the finished header + data */
} rom_t;

enum { FIX_AUTO, FIX_YES, FIX_NO };
enum { SORT_NONE, SORT_FILENAME, SORT_TITLE };
enum { CASE_KEEP, CASE_UPPER, CASE_LOWER, CASE_PROPER };
enum { UNSCRAMBLE_AUTO, UNSCRAMBLE_YES, UNSCRAMBLE_NO };

static struct {
    const char *output, *target, *data_dir, *config, *ini, *splash;
    const char *save_list, *gba_title, *gba_code, *gba_maker;
    int no_ini, fix_header, sort, title_case, unscramble;
    int clean_titles, underscores, number, mark_multiboot, auto_flags;
    int split, dry_run, force, splash_scale;
    long cart_size, pad_to;
    long flags, follow, reserved;
    int flags_set, follow_set, reserved_set;
    long or_flags;
} opt;

static target_t *targets;
static int ntargets;

/* ------------------------------------------------------------------ */

static void usage(FILE *f)
{
    fprintf(f,
"Usage: fcabuild [options] ROM... -o OUTPUT.gba\n"
"\n"
"Builds a GBA image holding an emulator plus a set of ROMs, like NES2FCA.\n"
"\n"
"Target:\n"
"  -t, --target NAME     fca-v02 (alias fca), fca-v01, PocketNES (pnes),\n"
"                        PCEAdvance (pce), GBonGBA (gb), Easymb2gba, or any\n"
"                        entry in nes2fca.cfg.  Default: guessed from the ROM\n"
"                        file extensions.\n"
"  --list-targets        show the targets and the emulator files they use\n"
"  -d, --data-dir DIR    where the emulators and nes2fca.cfg live\n"
"                        (default: next to fcabuild, then the current dir)\n"
"  --config FILE         use this nes2fca.cfg\n"
"\n"
"ROMs and per-game settings:\n"
"  -l, --list FILE       read ROMs from a list file (NES2FCA .lst files work).\n"
"                        Each line is PATH or PATH|flags|follow|reserved|title\n"
"  --ini FILE            per-game settings in NES2FCA.ini format (default:\n"
"                        NES2FCA.ini in the data dir, if present)\n"
"  --no-ini              ignore NES2FCA.ini\n"
"  --save-list FILE      write the final ROM list and settings to FILE\n"
"\n"
"Emulator options (applied to every ROM without its own setting):\n"
"  --flags N --follow N --reserved N    raw header values\n"
"  PocketNES: --ppu-hack --no-cpu-hack --pal --follow-memory\n"
"  PCEAdvance: --cpu-50 --no-cpu-hack --us-rom --follow-memory\n"
"  --no-auto-flags       don't set PAL for (E) NES games or US for (U)/(USA)\n"
"                        PC Engine games\n"
"  --pce-unscramble MODE auto|yes|no: bit-reverse US TurboGrafx-16 HuCards\n"
"                        (\"encrypted\" dumps). auto checks the reset vector.\n"
"\n"
"Titles:\n"
"  --clean-titles        drop GoodTools tags like (U) [!] from titles\n"
"  --underscores         turn underscores into spaces\n"
"  --title-case MODE     upper|lower|proper\n"
"  --number              number the games (01 ..., 02 ...)\n"
"  --mark-multiboot      prefix '* ' to games small enough for link transfer\n"
"  --sort MODE           none|filename|title (default none)\n"
"\n"
"Output:\n"
"  -o, --output FILE     output image (required unless --dry-run)\n"
"  -s, --splash FILE     splash screen: 240x160 BMP or 76800-byte .raw\n"
"                        (PocketNES and PCEAdvance)\n"
"  --splash-scale        shrink large BMP files to fit instead of cropping\n"
"  --cart-size SIZE      flash cart capacity, e.g. 32m (Mbit), 4mb, 8192k\n"
"                        (default 256m, the GBA maximum)\n"
"  --split               split into OUTPUT-1.gba, OUTPUT-2.gba ... when the\n"
"                        ROMs don't fit on one cart\n"
"  --pad-to SIZE         pad the image with 0xFF up to SIZE\n"
"  --fix-header / --no-fix-header\n"
"                        rewrite the GBA header (boot logo, title, checksum).\n"
"                        Default: only when the emulator's header is invalid\n"
"  --gba-title T --gba-code C --gba-maker M   header fields when fixing\n"
"  -n, --dry-run         show what would be built, write nothing\n"
"  -f, --force           add files even if they don't look like ROMs\n"
"  -q, --quiet           only print warnings and errors\n"
"  -h, --help            this help\n"
"  -V, --version         version\n");
}

static const char *type_name(int type)
{
    switch (type) {
    case T_PNES: return "PocketNES";
    case T_PCEA: return "PCEAdvance";
    case T_GBONGBA: return "GBonGBA";
    case T_PGB: return "PGB";
    default: return "FCA file system";
    }
}

/* NES2FCA's mapping from the cfg "type" value; anything else is FCA. */
static int parse_type(const char *s)
{
    if (!s)
        return T_FCA;
    if (str_ieq(s, "PNES")) return T_PNES;
    if (str_ieq(s, "PCEA")) return T_PCEA;
    if (str_ieq(s, "GBONGBA")) return T_GBONGBA;
    if (str_ieq(s, "PGB")) return T_PGB;
    return T_FCA;
}

static void load_targets(const ini_file *cfg)
{
    long num = ini_get_long(cfg, "list", "num", 0), i;
    if (num <= 0)
        die("nes2fca.cfg has no [list] entries");
    targets = xcalloc((size_t)num, sizeof(target_t));
    for (i = 0; i < num; i++) {
        char key[32];
        const char *name;
        target_t *t;
        int j;
        snprintf(key, sizeof key, "list%ld", i);
        name = ini_get(cfg, "list", key);
        if (!name || !*name)
            continue;
        t = &targets[ntargets++];
        t->name = xstrdup(name);
        t->shell = xstrdup(ini_get(cfg, name, "shell") ? ini_get(cfg, name, "shell") : "fca.gba");
        t->type = parse_type(ini_get(cfg, name, "type"));
        for (j = 0;; j++) {
            const char *v;
            char *copy, *c1, *c2;
            snprintf(key, sizeof key, "file%d", j);
            v = ini_get(cfg, name, key);
            if (!v)
                break;
            copy = xstrdup(v);
            c1 = strchr(copy, ',');
            c2 = c1 ? strchr(c1 + 1, ',') : NULL;
            if (!c1 || !c2)
                die("bad %s entry in nes2fca.cfg section [%s]: %s", key, name, v);
            *c1 = *c2 = 0;
            t->files = xrealloc(t->files, (size_t)(t->nfiles + 1) * sizeof(fixed_file));
            t->files[t->nfiles].src = xstrdup(str_trim(copy));
            t->files[t->nfiles].name = xstrdup(str_trim(c1 + 1));
            t->files[t->nfiles].ext = xstrdup(str_trim(c2 + 1));
            t->nfiles++;
            free(copy);
        }
    }
}

static target_t *find_target(const char *name)
{
    static const struct { const char *alias, *real; } aliases[] = {
        {"fca", "fca-v02"}, {"fca2", "fca-v02"}, {"fca-v2", "fca-v02"},
        {"fca1", "fca-v01"}, {"fca-v1", "fca-v01"},
        {"pnes", "PocketNES"}, {"pocketnes", "PocketNES"}, {"nes", "PocketNES"},
        {"pce", "PCEAdvance"}, {"pcea", "PCEAdvance"}, {"pceadvance", "PCEAdvance"},
        {"gb", "GBonGBA"}, {"gbongba", "GBonGBA"},
        {"easymb2gba", "Easymb2gba"}, {"easymb", "Easymb2gba"},
    };
    int i;
    size_t a;
    for (i = 0; i < ntargets; i++)
        if (str_ieq(targets[i].name, name))
            return &targets[i];
    for (a = 0; a < sizeof aliases / sizeof aliases[0]; a++)
        if (str_ieq(aliases[a].alias, name))
            for (i = 0; i < ntargets; i++)
                if (str_ieq(targets[i].name, aliases[a].real))
                    return &targets[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* ROM list                                                            */

static rom_t *roms;
static int nroms, caproms;

static rom_t *add_rom(const char *path)
{
    rom_t *r;
    if (nroms == caproms) {
        caproms = caproms ? caproms * 2 : 64;
        roms = xrealloc(roms, (size_t)caproms * sizeof(rom_t));
    }
    r = &roms[nroms++];
    memset(r, 0, sizeof *r);
    r->path = xstrdup(path);
    return r;
}

/* Paths in old list files are absolute Windows paths; when they don't exist
 * here, look for the file next to the list instead. */
static char *resolve_list_path(const char *list_dir, const char *p)
{
    char *cand;
    int absolute = p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
    if (absolute && file_exists(p))
        return xstrdup(p);
    if (!absolute) {
        cand = path_join(list_dir, p);
        if (file_exists(cand))
            return cand;
        free(cand);
    }
    cand = find_file_ci(list_dir, path_basename(p));
    if (cand)
        return cand;
    return absolute ? xstrdup(p) : path_join(list_dir, p);
}

static void parse_field_long(const char *s, long *v, int *set, const char *what,
                             const char *file, int line)
{
    char *t = xstrdup(s), *u = str_trim(t);
    if (*u) {
        if (parse_long(u, v) != 0)
            die("%s:%d: bad %s value '%s'", file, line, what, u);
        *set = 1;
    }
    free(t);
}

static void load_list(const char *file)
{
    uint8_t *data;
    size_t len;
    char *text, *line, *next, *dir;
    int lineno = 0;

    if (read_file(file, &data, &len) != 0)
        die("cannot read list file %s", file);
    text = xmalloc(len + 1);
    memcpy(text, data, len);
    text[len] = 0;
    free(data);
    dir = path_dirname(file);

    for (line = text; line; line = next) {
        char *s, *fields[5], *u;
        int nf = 0;
        rom_t *r;
        char *resolved;

        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        lineno++;
        u = text_from_legacy(line);
        s = str_trim(u);
        if (lineno == 1 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB)
            s += 3;
        if (!*s || *s == '#' || *s == ';') {
            free(u);
            continue;
        }
        fields[nf++] = s;
        while (nf < 5 && (s = strchr(s, '|')) != NULL) {
            *s++ = 0;
            fields[nf++] = s;
        }
        resolved = resolve_list_path(dir, str_trim(fields[0]));
        r = add_rom(resolved);
        free(resolved);
        if (nf > 1) parse_field_long(fields[1], &r->flags, &r->flags_set, "flags", file, lineno);
        r->flags_from_list = r->flags_set;
        if (nf > 2) parse_field_long(fields[2], &r->follow, &r->follow_set, "follow", file, lineno);
        if (nf > 3) parse_field_long(fields[3], &r->reserved, &r->reserved_set, "reserved", file, lineno);
        if (nf > 4 && *str_trim(fields[4])) {
            r->title = xstrdup(str_trim(fields[4]));
            r->title_set = 1;
        }
        free(u);
    }
    free(dir);
    free(text);
}

static void save_list(const char *file)
{
    FILE *f = fopen_utf8(file, "wb");
    int i;
    if (!f)
        die("cannot write %s", file);
    fprintf(f, "# fcabuild list: path|flags|follow|reserved|title\n");
    for (i = 0; i < nroms; i++)
        fprintf(f, "%s|%ld|%ld|%ld|%s\n", roms[i].path, roms[i].flags,
                roms[i].follow, roms[i].reserved, roms[i].title);
    if (fclose(f) != 0)
        die("cannot write %s", file);
    note("Saved list: %s", file);
}

/* ------------------------------------------------------------------ */
/* Per-game settings                                                   */

static const char *ini_section_for(int type)
{
    switch (type) {
    case T_PNES: return "PocketNES";
    case T_PCEA: return "PCEAdvance";
    case T_GBONGBA: case T_PGB: return "GBonGBA";
    default: return NULL;
    }
}

/* NES2FCA.ini stores "flags|follow|reserved|title" keyed by file name. */
static void apply_ini(rom_t *r, const ini_file *ini, int type)
{
    const char *key = path_basename(r->path);
    const char *section = ini_section_for(type);
    const char *v;

    if (type == T_FCA) {
        v = ini_get(ini, "TITLE", key);
        if (v && *v && !r->title_set) {
            r->title = xstrdup(v);
            r->title_set = 1;
        }
        return;
    }
    if (!section || !(v = ini_get(ini, section, key)))
        return;
    {
        char *copy = xstrdup(v), *p = copy, *fields[4];
        int nf = 0;
        long n;
        fields[nf++] = p;
        while (nf < 4 && (p = strchr(p, '|')) != NULL) {
            *p++ = 0;
            fields[nf++] = p;
        }
        if (nf > 0 && !r->flags_set && !opt.flags_set && parse_long(fields[0], &n) == 0) {
            r->flags = n;
            r->flags_set = 1;
        }
        if (nf > 1 && !r->follow_set && !opt.follow_set && parse_long(fields[1], &n) == 0) {
            r->follow = n;
            r->follow_set = 1;
        }
        if (nf > 2 && !r->reserved_set && !opt.reserved_set && parse_long(fields[2], &n) == 0) {
            r->reserved = n;
            r->reserved_set = 1;
        }
        if (nf > 3 && *fields[3] && !r->title_set) {
            r->title = xstrdup(fields[3]);
            r->title_set = 1;
        }
        free(copy);
    }
}

static int has_tag(const char *name, const char *const *tags)
{
    for (; *tags; tags++)
        if (str_icontains(name, *tags))
            return 1;
    return 0;
}

static void apply_auto_flags(rom_t *r, int type)
{
    static const char *const pal_tags[] = {"(E)", "(Europe)", "(EUR)", "(PAL)", NULL};
    static const char *const us_tags[] = {"(U)", "(USA)", "(US)", "(TG16)", NULL};
    const char *name = path_basename(r->path);
    if (r->flags_set || opt.flags_set)
        return;
    if (type == T_PNES && has_tag(name, pal_tags))
        r->flags |= 4;
    if (type == T_PCEA && has_tag(name, us_tags))
        r->flags |= 4;
}

/* "Super Mario Bros. (U) [!]" -> "Super Mario Bros." */
static void clean_title(char *t)
{
    char *p = t, *s;
    while ((p = strpbrk(p, "([")) != NULL) {
        if (p > t && p[-1] == ' ') {
            *p = 0;
            break;
        }
        p++;
    }
    s = str_trim(t);
    memmove(t, s, strlen(s) + 1);
}

static void transform_title(rom_t *r, int index, int count)
{
    char *t = xstrdup(r->title), *p;
    if (opt.clean_titles) {
        char *orig = xstrdup(t);
        clean_title(t);
        if (!*t) {
            free(t);
            t = orig;
        } else {
            free(orig);
        }
    }
    if (opt.underscores)
        for (p = t; *p; p++)
            if (*p == '_')
                *p = ' ';
    if (opt.title_case != CASE_KEEP) {
        int start = 1;
        for (p = t; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c >= 0x80) {
                start = 0;
                continue;
            }
            if (opt.title_case == CASE_UPPER || (opt.title_case == CASE_PROPER && start))
                *p = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
            else
                *p = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
            start = (c == ' ' || c == '-' || c == '(' || c == '[' || c == '.');
        }
    }
    if (opt.mark_multiboot && r->file_size >= 0 && r->file_size <= MULTIBOOT_LIMIT) {
        char *m = xasprintf("* %s", t);
        free(t);
        t = m;
    }
    if (opt.number) {
        int width = count >= 100 ? 3 : 2;
        char *m = xasprintf("%0*d %s", width, index + 1, t);
        free(t);
        t = m;
    }
    free(r->title);
    r->title = t;
}

static int cmp_filename(const void *a, const void *b)
{
    const rom_t *x = a, *y = b;
    const char *p = path_basename(x->path), *q = path_basename(y->path);
    for (; *p && *q; p++, q++) {
        int c = (unsigned char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
        int d = (unsigned char)(*q >= 'A' && *q <= 'Z' ? *q + 32 : *q);
        if (c != d)
            return c - d;
    }
    return (unsigned char)*p - (unsigned char)*q;
}

static int cmp_title(const void *a, const void *b)
{
    const rom_t *x = a, *y = b;
    const char *p = x->title, *q = y->title;
    for (; *p && *q; p++, q++) {
        int c = (unsigned char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
        int d = (unsigned char)(*q >= 'A' && *q <= 'Z' ? *q + 32 : *q);
        if (c != d)
            return c - d;
    }
    return (unsigned char)*p - (unsigned char)*q;
}

/* ------------------------------------------------------------------ */
/* Entries                                                             */

static void fca_entry(buf_t *b, const char *name_bytes, const char *ext,
                      const uint8_t *data, size_t len)
{
    uint8_t h[FCA_HEADER_LEN];
    size_t i;
    memset(h, 0, sizeof h);
    put_le32(h, FCA_MAGIC);
    strncpy((char *)h + 4, name_bytes, 31);
    for (i = 0; i < 3 && ext[i]; i++)
        h[36 + i] = (uint8_t)ext[i];
    put_le32(h + 40, (uint32_t)len);
    buf_append(b, h, sizeof h);
    buf_append(b, data, len);
    buf_pad_to(b, 4);
}

/* NES2FCA cleared byte 7 of an iNES header when its low nibble was set
 * (typically "DiskDude!" junk), so the mapper number reads correctly. */
static void clean_ines(uint8_t *data, size_t len, const char *path)
{
    if (len >= 16 && !memcmp(data, "NES\x1a", 4) && (data[7] & 0x0F)) {
        note("  %s: cleared junk in iNES header byte 7", path_basename(path));
        data[7] = 0;
    }
}

static uint8_t bitrev(uint8_t v)
{
    v = (uint8_t)(((v & 0xF0) >> 4) | ((v & 0x0F) << 4));
    v = (uint8_t)(((v & 0xCC) >> 2) | ((v & 0x33) << 2));
    v = (uint8_t)(((v & 0xAA) >> 1) | ((v & 0x55) << 1));
    return v;
}

/* US TurboGrafx-16 HuCards are wired with the data bus reversed, so dumps
 * have every byte's bits reversed.  The reset vector (end of bank 0) must
 * point into $E000-$FFFF; if only the reversed byte does, it's scrambled. */
static int pce_is_scrambled(const uint8_t *d, size_t len)
{
    uint8_t hi;
    if (len < 0x2000)
        return 0;
    hi = d[0x1FFF];
    return hi < 0xE0 && bitrev(hi) >= 0xE0;
}

static int build_rom_entry(rom_t *r, int type)
{
    uint8_t *data;
    size_t len;
    const char *ext = path_ext(r->path);
    int lossy = 0;

    if (read_file(r->path, &data, &len) != 0) {
        warn("cannot read %s, skipped", r->path);
        return -1;
    }
    r->file_size = (long)len;

    switch (type) {
    case T_FCA: {
        char *name = title_to_euc(r->title, 21, 31, &lossy);
        char lext[4] = {0};
        size_t i;
        for (i = 0; i < 3 && ext[i]; i++)
            lext[i] = (char)(ext[i] >= 'A' && ext[i] <= 'Z' ? ext[i] + 32 : ext[i]);
        if (!lext[0])
            memcpy(lext, "???", 3);
        if (str_ieq(lext, "nes")) {
            if (len < 16 || memcmp(data, "NES\x1a", 4)) {
                if (!opt.force) {
                    warn("%s is not an NES file, skipped (use --force)", r->path);
                    free(name);
                    free(data);
                    return -1;
                }
            }
            clean_ines(data, len, r->path);
        }
        if (lossy)
            warn("%s: title has characters the FCA font can't show", path_basename(r->path));
        fca_entry(&r->entry, name, lext, data, len);
        free(name);
        break;
    }
    case T_PNES: {
        uint8_t h[PNES_HEADER_LEN];
        char *title = title_to_ascii(r->title, 31, &lossy);
        size_t padded = (len + 3) & ~(size_t)3;
        if (len < 16 || memcmp(data, "NES\x1a", 4)) {
            if (!opt.force) {
                warn("%s is not an NES file, skipped (use --force)", r->path);
                free(title);
                free(data);
                return -1;
            }
        }
        clean_ines(data, len, r->path);
        /* PocketNES 9.8 doesn't skip a 512-byte trainer and reads the
         * trainer as PRG data, so drop it (they're almost always junk). */
        if (len >= 16 + 512 && (data[6] & 0x04)) {
            memmove(data + 16, data + 16 + 512, len - 16 - 512);
            len -= 512;
            data[6] &= (uint8_t)~0x04;
            padded = (len + 3) & ~(size_t)3;
            note("  %s: removed 512-byte trainer (PocketNES can't use it)", path_basename(r->path));
        }
        memset(h, 0, sizeof h);
        memcpy(h, title, strlen(title));
        put_le32(h + 32, (uint32_t)padded);
        put_le32(h + 36, (uint32_t)r->flags);
        put_le32(h + 40, (uint32_t)r->follow);
        put_le32(h + 44, (uint32_t)r->reserved);
        buf_append(&r->entry, h, sizeof h);
        buf_append(&r->entry, data, len);
        buf_append_zero(&r->entry, padded - len);
        if (lossy)
            warn("%s: non-ASCII title characters replaced", path_basename(r->path));
        free(title);
        break;
    }
    case T_PCEA: {
        uint8_t h[PCEA_HEADER_LEN];
        char *title = title_to_ascii(r->title, 31, &lossy);
        size_t skip = len & 0x1FFF, body;
        uint8_t *rom;
        int scrambled;

        if (skip && skip != 512) {
            /* Not a copier header: keep everything and pad to 8 KB. */
            warn("%s: size isn't a multiple of 8 KB; padding", path_basename(r->path));
            skip = 0;
        }
        if (len - skip < 0x2000 && !opt.force) {
            warn("%s is too small for a HuCard image, skipped (use --force)", r->path);
            free(title);
            free(data);
            return -1;
        }
        rom = data + skip;
        body = len - skip;
        scrambled = pce_is_scrambled(rom, body);
        if (opt.unscramble == UNSCRAMBLE_YES ||
            (opt.unscramble == UNSCRAMBLE_AUTO && scrambled)) {
            size_t i;
            for (i = 0; i < body; i++)
                rom[i] = bitrev(rom[i]);
            note("  %s: unscrambled (bit-reversed US HuCard dump)", path_basename(r->path));
        } else if (scrambled) {
            warn("%s looks like a scrambled US HuCard dump; it will likely show a black screen (try --pce-unscramble yes)",
                 path_basename(r->path));
        }
        memset(h, 0, sizeof h);
        memcpy(h, title, strlen(title));
        put_le32(h + 36, (uint32_t)r->flags);
        put_le32(h + 40, (uint32_t)r->follow);
        put_le32(h + 44, (uint32_t)r->reserved);
        memcpy(h + 48, "NES\x1a@           ", 16);
        {
            size_t padded = (body + 0x1FFF) & ~(size_t)0x1FFF;
            put_le32(h + 32, (uint32_t)(padded + 16));
            buf_append(&r->entry, h, sizeof h);
            buf_append(&r->entry, rom, body);
            buf_append_zero(&r->entry, padded - body);
        }
        if (lossy)
            warn("%s: non-ASCII title characters replaced", path_basename(r->path));
        free(title);
        break;
    }
    default: /* GBonGBA / PGB: the ROMs as they are */
        if ((len < 0x150 || data[0x104] != 0xCE || data[0x105] != 0xED) && !opt.force) {
            warn("%s is not a Game Boy ROM, skipped (use --force)", r->path);
            free(data);
            return -1;
        }
        buf_append(&r->entry, data, len);
        break;
    }
    free(data);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Image assembly                                                      */

static void build_base(buf_t *base, const target_t *t, const char *data_dir,
                       const uint8_t *splash)
{
    char *shell = find_file_ci(data_dir, t->shell);
    uint8_t *data;
    size_t len;
    int i;

    if (!shell)
        die("emulator file %s not found in %s (use --data-dir)", t->shell, data_dir);
    if (read_file(shell, &data, &len) != 0)
        die("cannot read %s", shell);
    buf_append(base, data, len);
    free(data);
    note("Emulator: %s (%zu bytes)", shell, len);
    free(shell);

    for (i = 0; i < t->nfiles; i++) {
        char *p = find_file_ci(data_dir, t->files[i].src);
        if (!p)
            die("%s (needed by %s) not found in %s", t->files[i].src, t->name, data_dir);
        if (read_file(p, &data, &len) != 0)
            die("cannot read %s", p);
        if (t->type == T_FCA)
            fca_entry(base, t->files[i].name, t->files[i].ext, data, len);
        else
            buf_append(base, data, len);
        free(data);
        free(p);
    }
    if (splash)
        buf_append(base, splash, SPLASH_SIZE);
}

static void end_record(buf_t *b)
{
    uint8_t h[FCA_HEADER_LEN];
    memset(h, 0, sizeof h);
    put_le32(h, FCA_END_MAGIC);
    buf_append(b, h, sizeof h);
}

static char *gba_title_default(const char *output)
{
    char *stem = path_stem(output), *t, *p;
    t = title_to_ascii(stem, 12, NULL);
    for (p = t; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 32);
    free(stem);
    return t;
}

static void maybe_fix_header(buf_t *img, const char *output)
{
    int valid = gba_has_logo(img->data, img->len) && gba_checksum_ok(img->data, img->len);
    char existing_title[13] = {0};
    const char *code = opt.gba_code, *maker = opt.gba_maker;
    char *title;

    if (opt.fix_header == FIX_NO || (opt.fix_header == FIX_AUTO && valid))
        return;
    if (img->len < GBA_HEADER_SIZE)
        return;
    memcpy(existing_title, img->data + 0xA0, 12);
    if (opt.gba_title)
        title = title_to_ascii(opt.gba_title, 12, NULL);
    else if (existing_title[0])
        title = xstrdup(existing_title);
    else
        title = gba_title_default(output);
    /* NES2FCA's defaults were game code "FCA" and maker "KK". */
    if (!code && !img->data[0xAC])
        code = "FCA";
    if (!maker && !img->data[0xB0])
        maker = "KK";
    gba_fix_header(img->data, img->len, title, code, maker);
    note("Fixed GBA header (title \"%s\")%s", title,
         valid ? "" : " - the emulator's header wasn't valid for real hardware");
    free(title);
}

static char *part_name(const char *output, int part, int parts)
{
    char *dir, *stem, *name, *full;
    const char *ext;
    if (parts <= 1)
        return xstrdup(output);
    dir = path_dirname(output);
    stem = path_stem(output);
    ext = path_ext(output);
    name = *ext ? xasprintf("%s-%d.%s", stem, part, ext) : xasprintf("%s-%d", stem, part);
    full = path_basename(output) == output ? xstrdup(name) : path_join(dir, name);
    free(dir);
    free(stem);
    free(name);
    return full;
}

static void print_flags(char *out, size_t n, const rom_t *r, int type)
{
    out[0] = 0;
    if (type != T_PNES && type != T_PCEA)
        return;
    snprintf(out, n, "flags=%ld follow=%ld", r->flags, r->follow);
}

/* ------------------------------------------------------------------ */

static const char *need_arg(int argc, char **argv, int *i, const char *name,
                            const char *inline_val)
{
    if (inline_val)
        return inline_val;
    if (*i + 1 >= argc)
        die("%s needs a value", name);
    return argv[++*i];
}

static long need_long(const char *v, const char *name)
{
    long n;
    if (parse_long(v, &n) != 0)
        die("bad value for %s: %s", name, v);
    return n;
}

int main(int argc, char **argv)
{
    char **inputs = NULL;
    int ninputs = 0, i, type;
    char **lists = NULL;
    int nlists = 0;
    ini_file cfg, ini = {0};
    int have_ini = 0;
    char *data_dir, *cfg_path = NULL;
    target_t *target;
    uint8_t splash[SPLASH_SIZE], *splash_ptr = NULL;
    buf_t base = {0};
    size_t end_len = FCA_HEADER_LEN;
    int parts, built = 0;
    long *part_of;
    int list_targets = 0;

    tool_init(&argc, &argv, "fcabuild");
    opt.cart_size = DEFAULT_CART_SIZE;
    opt.auto_flags = 1;

    for (i = 1; i < argc; i++) {
        char *a = argv[i], *val = NULL;
        if (a[0] != '-' || !a[1]) {
            inputs = xrealloc(inputs, (size_t)(ninputs + 1) * sizeof(char *));
            inputs[ninputs++] = a;
            continue;
        }
        if (!strcmp(a, "--")) {
            for (i++; i < argc; i++) {
                inputs = xrealloc(inputs, (size_t)(ninputs + 1) * sizeof(char *));
                inputs[ninputs++] = argv[i];
            }
            break;
        }
        if (a[1] == '-' && (val = strchr(a, '=')) != NULL)
            *val++ = 0;
#define IS(s, l) (!strcmp(a, s) || !strcmp(a, l))
        if (IS("-h", "--help")) { usage(stdout); return 0; }
        else if (IS("-V", "--version")) { printf("fcabuild %s\n", FCA_TOOLS_VERSION); return 0; }
        else if (IS("-o", "--output")) opt.output = need_arg(argc, argv, &i, a, val);
        else if (IS("-t", "--target")) opt.target = need_arg(argc, argv, &i, a, val);
        else if (IS("-d", "--data-dir")) opt.data_dir = need_arg(argc, argv, &i, a, val);
        else if (!strcmp(a, "--config")) opt.config = need_arg(argc, argv, &i, a, val);
        else if (!strcmp(a, "--list-targets")) list_targets = 1;
        else if (IS("-l", "--list")) {
            lists = xrealloc(lists, (size_t)(nlists + 1) * sizeof(char *));
            lists[nlists++] = (char *)need_arg(argc, argv, &i, a, val);
        }
        else if (!strcmp(a, "--ini")) opt.ini = need_arg(argc, argv, &i, a, val);
        else if (!strcmp(a, "--no-ini")) opt.no_ini = 1;
        else if (!strcmp(a, "--save-list")) opt.save_list = need_arg(argc, argv, &i, a, val);
        else if (!strcmp(a, "--flags")) { opt.flags = need_long(need_arg(argc, argv, &i, a, val), a); opt.flags_set = 1; }
        else if (!strcmp(a, "--follow")) { opt.follow = need_long(need_arg(argc, argv, &i, a, val), a); opt.follow_set = 1; }
        else if (!strcmp(a, "--reserved")) { opt.reserved = need_long(need_arg(argc, argv, &i, a, val), a); opt.reserved_set = 1; }
        else if (!strcmp(a, "--ppu-hack") || !strcmp(a, "--cpu-50")) opt.or_flags |= 1;
        else if (!strcmp(a, "--no-cpu-hack")) opt.or_flags |= 2;
        else if (!strcmp(a, "--pal") || !strcmp(a, "--us-rom")) opt.or_flags |= 4;
        else if (!strcmp(a, "--follow-memory")) opt.or_flags |= 32;
        else if (!strcmp(a, "--no-auto-flags")) opt.auto_flags = 0;
        else if (!strcmp(a, "--pce-unscramble")) {
            const char *m = need_arg(argc, argv, &i, a, val);
            if (str_ieq(m, "auto")) opt.unscramble = UNSCRAMBLE_AUTO;
            else if (str_ieq(m, "yes") || str_ieq(m, "on")) opt.unscramble = UNSCRAMBLE_YES;
            else if (str_ieq(m, "no") || str_ieq(m, "off")) opt.unscramble = UNSCRAMBLE_NO;
            else die("--pce-unscramble takes auto, yes or no");
        }
        else if (!strcmp(a, "--clean-titles")) opt.clean_titles = 1;
        else if (!strcmp(a, "--underscores")) opt.underscores = 1;
        else if (!strcmp(a, "--number")) opt.number = 1;
        else if (!strcmp(a, "--mark-multiboot")) opt.mark_multiboot = 1;
        else if (!strcmp(a, "--title-case")) {
            const char *m = need_arg(argc, argv, &i, a, val);
            if (str_ieq(m, "upper")) opt.title_case = CASE_UPPER;
            else if (str_ieq(m, "lower")) opt.title_case = CASE_LOWER;
            else if (str_ieq(m, "proper")) opt.title_case = CASE_PROPER;
            else die("--title-case takes upper, lower or proper");
        }
        else if (!strcmp(a, "--sort")) {
            const char *m = need_arg(argc, argv, &i, a, val);
            if (str_ieq(m, "none")) opt.sort = SORT_NONE;
            else if (str_ieq(m, "filename")) opt.sort = SORT_FILENAME;
            else if (str_ieq(m, "title")) opt.sort = SORT_TITLE;
            else die("--sort takes none, filename or title");
        }
        else if (IS("-s", "--splash")) opt.splash = need_arg(argc, argv, &i, a, val);
        else if (!strcmp(a, "--splash-scale")) opt.splash_scale = 1;
        else if (!strcmp(a, "--cart-size")) {
            const char *m = need_arg(argc, argv, &i, a, val);
            if (parse_size(m, &opt.cart_size) != 0 || opt.cart_size <= 0)
                die("bad --cart-size: %s", m);
        }
        else if (!strcmp(a, "--split")) opt.split = 1;
        else if (!strcmp(a, "--pad-to")) {
            const char *m = need_arg(argc, argv, &i, a, val);
            if (parse_size(m, &opt.pad_to) != 0 || opt.pad_to <= 0)
                die("bad --pad-to: %s", m);
        }
        else if (!strcmp(a, "--fix-header")) opt.fix_header = FIX_YES;
        else if (!strcmp(a, "--no-fix-header")) opt.fix_header = FIX_NO;
        else if (!strcmp(a, "--gba-title")) opt.gba_title = need_arg(argc, argv, &i, a, val);
        else if (!strcmp(a, "--gba-code")) opt.gba_code = need_arg(argc, argv, &i, a, val);
        else if (!strcmp(a, "--gba-maker")) opt.gba_maker = need_arg(argc, argv, &i, a, val);
        else if (IS("-n", "--dry-run")) opt.dry_run = 1;
        else if (IS("-f", "--force")) opt.force = 1;
        else if (IS("-q", "--quiet")) quiet = 1;
        else {
            fprintf(stderr, "%s: unknown option %s\n\n", progname, a);
            usage(stderr);
            return 2;
        }
#undef IS
    }

    /* Data directory: explicit, else next to the program, else here. */
    if (opt.data_dir) {
        data_dir = xstrdup(opt.data_dir);
    } else {
        data_dir = exe_dir(argv[0]);
        if (!(cfg_path = find_file_ci(data_dir, "nes2fca.cfg"))) {
            char *shell = find_file_ci(data_dir, "pocketnes.gba");
            if (!shell) {
                free(data_dir);
                data_dir = xstrdup(".");
            }
            free(shell);
        }
        free(cfg_path);
        cfg_path = NULL;
    }
    if (opt.config) {
        if (ini_load(&cfg, opt.config) != 0)
            die("cannot read %s", opt.config);
    } else if ((cfg_path = find_file_ci(data_dir, "nes2fca.cfg")) != NULL) {
        if (ini_load(&cfg, cfg_path) != 0)
            die("cannot read %s", cfg_path);
    } else {
        memset(&cfg, 0, sizeof cfg);
        ini_load_text(&cfg, builtin_cfg);
    }
    load_targets(&cfg);

    if (list_targets) {
        printf("Targets (%s):\n", opt.config ? opt.config : cfg_path ? cfg_path : "built-in nes2fca.cfg");
        for (i = 0; i < ntargets; i++) {
            char *p = find_file_ci(data_dir, targets[i].shell);
            printf("  %-12s %-16s %-16s %s\n", targets[i].name, type_name(targets[i].type),
                   targets[i].shell, p ? "" : "(emulator file not found)");
            free(p);
        }
        return 0;
    }

    for (i = 0; i < nlists; i++)
        load_list(lists[i]);
    for (i = 0; i < ninputs; i++)
        add_rom(inputs[i]);
    if (nroms == 0) {
        usage(stderr);
        return 2;
    }
    if (!opt.output && !opt.dry_run)
        die("no output file given (-o OUTPUT.gba)");

    /* Target. */
    if (opt.target) {
        target = find_target(opt.target);
        if (!target)
            die("unknown target '%s' (see --list-targets)", opt.target);
    } else {
        const char *guess = NULL;
        for (i = 0; i < nroms; i++) {
            const char *e = path_ext(roms[i].path), *g;
            if (str_ieq(e, "nes")) g = "PocketNES";
            else if (str_ieq(e, "pce")) g = "PCEAdvance";
            else if (str_ieq(e, "gb") || str_ieq(e, "gbc")) g = "GBonGBA";
            else die("can't tell which emulator %s is for; use --target", roms[i].path);
            if (guess && strcmp(guess, g))
                die("the ROMs are for different systems; use --target");
            guess = g;
        }
        target = find_target(guess);
        if (!target)
            die("no %s target in nes2fca.cfg", guess);
    }
    type = target->type;
    note("Target: %s (%s)", target->name, type_name(type));

    /* Per-game settings. */
    if (!opt.no_ini) {
        char *p = opt.ini ? xstrdup(opt.ini) : find_file_ci(data_dir, "NES2FCA.ini");
        if (p) {
            if (ini_load(&ini, p) == 0) {
                have_ini = 1;
                if (!opt.ini)
                    note("Per-game settings: %s (use --no-ini to ignore)", p);
            } else if (opt.ini) {
                die("cannot read %s", p);
            }
            free(p);
        }
    }
    for (i = 0; i < nroms; i++) {
        rom_t *r = &roms[i];
        r->file_size = file_size(r->path);
        if (r->file_size < 0)
            die("%s: no such file", r->path);
        if (!r->flags_set && opt.flags_set) r->flags = opt.flags;
        if (!r->follow_set && opt.follow_set) r->follow = opt.follow;
        if (!r->reserved_set && opt.reserved_set) r->reserved = opt.reserved;
        if (have_ini)
            apply_ini(r, &ini, type);
        if (opt.auto_flags)
            apply_auto_flags(r, type);
        /* Named options (--pal ...) beat NES2FCA.ini but not the list file. */
        if (!r->flags_from_list)
            r->flags |= opt.or_flags;
        if (!r->title)
            r->title = path_stem(r->path);
    }
    if (opt.sort == SORT_FILENAME)
        qsort(roms, (size_t)nroms, sizeof(rom_t), cmp_filename);
    else if (opt.sort == SORT_TITLE)
        qsort(roms, (size_t)nroms, sizeof(rom_t), cmp_title);
    for (i = 0; i < nroms; i++)
        transform_title(&roms[i], i, nroms);

    if (opt.save_list)
        save_list(opt.save_list);

    /* Splash screen. */
    if (opt.splash) {
        const char *err;
        if (type != T_PNES && type != T_PCEA)
            die("splash screens are only supported by PocketNES and PCEAdvance");
        if (load_splash(opt.splash, opt.splash_scale, splash, &err) != 0)
            die("%s: %s", opt.splash, err);
        splash_ptr = splash;
    }

    build_base(&base, target, data_dir, splash_ptr);

    /* Build each ROM's entry. */
    for (i = 0; i < nroms; i++)
        if (build_rom_entry(&roms[i], type) != 0)
            roms[i].entry.len = 0;

    /* Pack the entries into one or more images. */
    part_of = xcalloc((size_t)nroms, sizeof(long));
    parts = 1;
    {
        size_t used = base.len + end_len;
        for (i = 0; i < nroms; i++) {
            size_t n = roms[i].entry.len;
            if (!n) {
                part_of[i] = -1;
                continue;
            }
            if (base.len + n + end_len > (size_t)opt.cart_size)
                die("%s alone doesn't fit in a %ld-byte cart", roms[i].path, opt.cart_size);
            if (used + n > (size_t)opt.cart_size) {
                if (!opt.split)
                    die("the image would be %zu bytes (or more), over the %ld-byte cart size; "
                        "drop some ROMs, raise --cart-size or use --split",
                        used + n, opt.cart_size);
                parts++;
                used = base.len + end_len;
            }
            part_of[i] = parts;
            used += n;
        }
    }

    for (built = 1; built <= parts; built++) {
        buf_t img = {0};
        char *name = part_name(opt.output ? opt.output : "compilation.gba", built, parts);
        int count = 0;
        char line[64];

        buf_append(&img, base.data, base.len);
        note("%s%s:", opt.dry_run ? "Would write " : "", name);
        for (i = 0; i < nroms; i++) {
            if (part_of[i] != built)
                continue;
            buf_append(&img, roms[i].entry.data, roms[i].entry.len);
            print_flags(line, sizeof line, &roms[i], type);
            note("  %3d  %-31s %8ld  %s", ++count, roms[i].title, roms[i].file_size, line);
        }
        end_record(&img);
        if (opt.pad_to > 0 && img.len < (size_t)opt.pad_to) {
            size_t old = img.len;
            buf_append_zero(&img, (size_t)opt.pad_to - old);
            memset(img.data + old, 0xFF, (size_t)opt.pad_to - old);
        }
        maybe_fix_header(&img, name);
        if (type == T_PCEA && count > 2 && img.len <= 256 * 1024)
            note("  Note: emulators such as mGBA run images under 256 KB from RAM, where\n"
                 "  PCEAdvance can only start the first two games. Flash carts are fine;\n"
                 "  add --pad-to 512k for emulator use.");
        note("  %d ROM%s, %zu bytes (%.2f Mbit, cart %.0f Mbit, %ld bytes free)",
             count, count == 1 ? "" : "s", img.len, img.len * 8.0 / (1024 * 1024),
             opt.cart_size * 8.0 / (1024 * 1024), opt.cart_size - (long)img.len);
        if (!opt.dry_run && write_file(name, img.data, img.len) != 0)
            die("cannot write %s", name);
        buf_free(&img);
        free(name);
    }
    for (i = 0, built = 0; i < nroms; i++)
        if (part_of[i] < 0)
            built++;
    if (built)
        warn("%d file%s skipped", built, built == 1 ? "" : "s");
    return built ? 1 : 0;
}
