/*
 * common.c - shared helpers for the fca-multi-builder tools.
 */
#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <shellapi.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

const char *progname = "fca-tools";
int quiet = 0;

/* ------------------------------------------------------------------ */
/* Windows UTF-8 plumbing                                              */

#ifdef _WIN32
static char *utf16_to_utf8(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = xmalloc(n > 0 ? (size_t)n : 1);
    if (n <= 0 || !WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL))
        s[0] = 0;
    return s;
}

static wchar_t *utf8_to_utf16(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = xmalloc((n > 0 ? (size_t)n : 1) * sizeof(wchar_t));
    if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n))
        w[0] = 0;
    return w;
}
#endif

void tool_init(int *argc, char ***argv, const char *name)
{
    progname = name;
#ifdef _WIN32
    {
        int n = 0, i;
        wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &n);
        if (wargv) {
            char **av = xcalloc((size_t)n + 1, sizeof(char *));
            for (i = 0; i < n; i++)
                av[i] = utf16_to_utf8(wargv[i]);
            LocalFree(wargv);
            *argc = n;
            *argv = av;
        }
        SetConsoleOutputCP(CP_UTF8);
    }
#else
    (void)argc;
    (void)argv;
#endif
}

/* ------------------------------------------------------------------ */
/* Messages                                                            */

void die(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s: error: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void warn(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s: warning: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void note(const char *fmt, ...)
{
    va_list ap;
    if (quiet)
        return;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
}

/* ------------------------------------------------------------------ */
/* Memory                                                              */

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

void *xcalloc(size_t n, size_t m)
{
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p)
        die("out of memory");
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n)
{
    char *d = xmalloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

char *xasprintf(const char *fmt, ...)
{
    va_list ap;
    int n;
    char *s;
    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        die("formatting error");
    s = xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

/* ------------------------------------------------------------------ */
/* Files                                                               */

FILE *fopen_utf8(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t *wp = utf8_to_utf16(path), *wm = utf8_to_utf16(mode);
    FILE *f = _wfopen(wp, wm);
    free(wp);
    free(wm);
    return f;
#else
    return fopen(path, mode);
#endif
}

long file_size(const char *path)
{
#ifdef _WIN32
    struct _stat64 st;
    wchar_t *wp = utf8_to_utf16(path);
    int r = _wstat64(wp, &st);
    free(wp);
    if (r != 0 || (st.st_mode & _S_IFDIR))
        return -1;
    return st.st_size > LONG_MAX ? LONG_MAX : (long)st.st_size;
#else
    struct stat st;
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode))
        return -1;
    return st.st_size > LONG_MAX ? LONG_MAX : (long)st.st_size;
#endif
}

int file_exists(const char *path)
{
    return file_size(path) >= 0;
}

int read_file(const char *path, uint8_t **data, size_t *len)
{
    FILE *f = fopen_utf8(path, "rb");
    buf_t b = {0};
    uint8_t chunk[65536];
    size_t n;

    if (!f)
        return -1;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
        buf_append(&b, chunk, n);
    if (ferror(f)) {
        fclose(f);
        buf_free(&b);
        return -1;
    }
    fclose(f);
    if (!b.data)
        b.data = xmalloc(1);
    *data = b.data;
    *len = b.len;
    return 0;
}

int write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen_utf8(path, "wb");
    if (!f)
        return -1;
    if (len && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}

int make_dir(const char *path)
{
#ifdef _WIN32
    wchar_t *wp = utf8_to_utf16(path);
    int r = _wmkdir(wp);
    free(wp);
#else
    int r = mkdir(path, 0777);
#endif
    return (r == 0 || errno == EEXIST) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Paths                                                               */

static int is_sep(char c)
{
    return c == '/' || c == '\\';
}

const char *path_basename(const char *path)
{
    const char *p, *base = path;
    for (p = path; *p; p++)
        if (is_sep(*p) || (*p == ':' && p == path + 1))
            base = p + 1;
    return base;
}

char *path_dirname(const char *path)
{
    const char *base = path_basename(path);
    size_t n = (size_t)(base - path);
    if (n == 0)
        return xstrdup(".");
    while (n > 1 && is_sep(path[n - 1]))
        n--;
    return xstrndup(path, n);
}

char *path_join(const char *dir, const char *name)
{
    size_t n;
    if (!dir || !*dir || !strcmp(dir, "."))
        return xstrdup(name);
    n = strlen(dir);
    if (is_sep(dir[n - 1]))
        return xasprintf("%s%s", dir, name);
    return xasprintf("%s%c%s", dir, PATH_SEP, name);
}

const char *path_ext(const char *path)
{
    const char *base = path_basename(path);
    const char *dot = strrchr(base, '.');
    return (dot && dot != base) ? dot + 1 : "";
}

char *path_stem(const char *path)
{
    const char *base = path_basename(path);
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base)
        return xstrdup(base);
    return xstrndup(base, (size_t)(dot - base));
}

char *exe_dir(const char *argv0)
{
#ifdef _WIN32
    wchar_t wbuf[32768];
    DWORD n = GetModuleFileNameW(NULL, wbuf, sizeof wbuf / sizeof wbuf[0]);
    (void)argv0;
    if (n > 0 && n < sizeof wbuf / sizeof wbuf[0]) {
        char *full = utf16_to_utf8(wbuf);
        char *dir = path_dirname(full);
        free(full);
        return dir;
    }
    return xstrdup(".");
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = 0;
        return path_dirname(buf);
    }
    return path_dirname(argv0 ? argv0 : ".");
#endif
}

char *find_file_ci(const char *dir, const char *name)
{
    char *path = path_join(dir, name);
#ifndef _WIN32
    DIR *d;
    struct dirent *de;
#endif
    if (file_exists(path))
        return path;
    free(path);
#ifndef _WIN32
    d = opendir(dir && *dir ? dir : ".");
    if (!d)
        return NULL;
    while ((de = readdir(d)) != NULL) {
        if (str_ieq(de->d_name, name)) {
            path = path_join(dir, de->d_name);
            closedir(d);
            if (file_exists(path))
                return path;
            free(path);
            return NULL;
        }
    }
    closedir(d);
#endif
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Strings                                                             */

int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int str_iprefix(const char *s, const char *prefix)
{
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

int str_icontains(const char *hay, const char *needle)
{
    for (; *hay; hay++)
        if (str_iprefix(hay, needle))
            return 1;
    return !*needle;
}

char *str_trim(char *s)
{
    char *e;
    while (*s && isspace((unsigned char)*s))
        s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

int parse_long(const char *s, long *out)
{
    char *end;
    long v;
    if (!s || !*s)
        return -1;
    errno = 0;
    v = strtol(s, &end, 0);
    if (errno || *end)
        return -1;
    *out = v;
    return 0;
}

int parse_size(const char *s, long *out)
{
    char *end;
    double v;
    double mult = 1;
    if (!s || !*s)
        return -1;
    v = strtod(s, &end);
    if (end == s || v < 0)
        return -1;
    while (*end == ' ')
        end++;
    if (!*end || str_ieq(end, "b"))
        mult = 1;
    else if (str_ieq(end, "k") || str_ieq(end, "kb") || str_ieq(end, "kib"))
        mult = 1024;
    else if (str_ieq(end, "kbit"))
        mult = 1024 / 8.0;
    else if (str_ieq(end, "m") || str_ieq(end, "mbit"))
        mult = 1024 * 1024 / 8.0;
    else if (str_ieq(end, "mb") || str_ieq(end, "mib") || str_ieq(end, "mbyte"))
        mult = 1024 * 1024;
    else
        return -1;
    v *= mult;
    if (v > (double)LONG_MAX)
        return -1;
    *out = (long)v;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Buffers                                                             */

void buf_reserve(buf_t *b, size_t extra)
{
    if (b->len + extra > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->len + extra)
            cap *= 2;
        b->data = xrealloc(b->data, cap);
        b->cap = cap;
    }
}

void buf_append(buf_t *b, const void *data, size_t len)
{
    if (!len)
        return;
    buf_reserve(b, len);
    memcpy(b->data + b->len, data, len);
    b->len += len;
}

void buf_append_zero(buf_t *b, size_t len)
{
    if (!len)
        return;
    buf_reserve(b, len);
    memset(b->data + b->len, 0, len);
    b->len += len;
}

void buf_pad_to(buf_t *b, size_t align)
{
    if (align > 1 && b->len % align)
        buf_append_zero(b, align - b->len % align);
}

void buf_free(buf_t *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}
