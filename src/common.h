/*
 * common.h - shared helpers for the fca-multi-builder tools.
 *
 * Everything here is plain C99 and builds the same way for native Linux,
 * 32-bit Windows and 64-bit Windows.  All strings handled by the tools are
 * UTF-8; on Windows the command line and file names are converted from and
 * to UTF-16 at the edges so non-ASCII file names work everywhere.
 */
#ifndef FCA_COMMON_H
#define FCA_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define FCA_TOOLS_VERSION "1.0.0"

#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

/* Program name used as a prefix for messages. */
extern const char *progname;

/* Call first thing in main(): sets progname, and on Windows replaces argv
 * with a UTF-8 copy of the real (UTF-16) command line. */
void tool_init(int *argc, char ***argv, const char *name);

#if defined(__MINGW32__)
/* Built with __USE_MINGW_ANSI_STDIO, so C99 formats such as %zu work. */
#define FCA_NORETURN __attribute__((noreturn))
#define FCA_PRINTF(f, a) __attribute__((format(gnu_printf, f, a)))
#elif defined(__GNUC__)
#define FCA_NORETURN __attribute__((noreturn))
#define FCA_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define FCA_NORETURN
#define FCA_PRINTF(f, a)
#endif

FCA_NORETURN void die(const char *fmt, ...) FCA_PRINTF(1, 2);
void warn(const char *fmt, ...) FCA_PRINTF(1, 2);
void note(const char *fmt, ...) FCA_PRINTF(1, 2);
extern int quiet;

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xasprintf(const char *fmt, ...) FCA_PRINTF(1, 2);

/* UTF-8 aware file access. */
FILE *fopen_utf8(const char *path, const char *mode);
int file_exists(const char *path);
long file_size(const char *path);           /* -1 if missing */
int read_file(const char *path, uint8_t **data, size_t *len);  /* 0 = ok */
int write_file(const char *path, const void *data, size_t len); /* 0 = ok */
int make_dir(const char *path);

/* Path helpers.  Both '/' and '\\' are treated as separators so lists
 * written on Windows still work on Linux.  Returned strings are malloc'd. */
const char *path_basename(const char *path);   /* pointer into path */
char *path_dirname(const char *path);
char *path_join(const char *dir, const char *name);
char *path_stem(const char *path);             /* basename without extension */
const char *path_ext(const char *path);        /* "" or pointer past the dot */
char *exe_dir(const char *argv0);
/* Finds name inside dir, ignoring case on case-sensitive file systems
 * (the original config files were written on Windows).  Returns a malloc'd
 * path or NULL. */
char *find_file_ci(const char *dir, const char *name);

/* String helpers. */
int str_ieq(const char *a, const char *b);
int str_iprefix(const char *s, const char *prefix);
char *str_trim(char *s);
int str_icontains(const char *hay, const char *needle);
int parse_long(const char *s, long *out);
/* Accepts "4194304", "32m"/"32mbit", "4mib"/"4mb", "512k", "512kib".
 * A bare "m"/"mbit" means megabits (cart sizes are traditionally quoted in
 * megabits); "mb"/"mib" means megabytes. */
int parse_size(const char *s, long *out);

/* Little-endian helpers. */
static inline uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

/* Growable byte buffer used to assemble images in memory. */
typedef struct {
    uint8_t *data;
    size_t len, cap;
} buf_t;

void buf_reserve(buf_t *b, size_t extra);
void buf_append(buf_t *b, const void *data, size_t len);
void buf_append_zero(buf_t *b, size_t len);
void buf_pad_to(buf_t *b, size_t align);
void buf_free(buf_t *b);

#endif
