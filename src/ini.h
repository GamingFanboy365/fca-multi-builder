/*
 * ini.h - minimal reader for Windows-style INI files (nes2fca.cfg,
 * NES2FCA.ini).  Section and key lookups are case-insensitive, matching
 * GetPrivateProfileString().  Text that is not UTF-8 is treated as
 * Shift-JIS, since the original tools ran on Japanese Windows.
 */
#ifndef FCA_INI_H
#define FCA_INI_H

typedef struct ini_entry {
    char *section, *key, *value;
} ini_entry;

typedef struct ini_file {
    ini_entry *entries;
    int count, cap;
} ini_file;

/* Returns 0 on success, -1 if the file can't be read. */
int ini_load(ini_file *ini, const char *path);
/* Parses INI text (appending to ini, which must be zeroed or loaded). */
void ini_load_text(ini_file *ini, const char *text);
const char *ini_get(const ini_file *ini, const char *section, const char *key);
long ini_get_long(const ini_file *ini, const char *section, const char *key, long def);
void ini_free(ini_file *ini);

#endif
