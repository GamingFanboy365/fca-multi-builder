/*
 * ini.c - minimal INI reader.
 */
#include "ini.h"
#include "common.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

static void add_entry(ini_file *ini, const char *section, const char *key,
                      const char *value)
{
    if (ini->count == ini->cap) {
        ini->cap = ini->cap ? ini->cap * 2 : 32;
        ini->entries = xrealloc(ini->entries, (size_t)ini->cap * sizeof(ini_entry));
    }
    ini->entries[ini->count].section = text_from_legacy(section);
    ini->entries[ini->count].key = text_from_legacy(key);
    ini->entries[ini->count].value = text_from_legacy(value);
    ini->count++;
}

int ini_load(ini_file *ini, const char *path)
{
    uint8_t *data;
    size_t len;
    char *text;

    memset(ini, 0, sizeof *ini);
    if (read_file(path, &data, &len) != 0)
        return -1;
    text = xmalloc(len + 1);
    memcpy(text, data, len);
    text[len] = 0;
    free(data);
    ini_load_text(ini, text);
    free(text);
    return 0;
}

void ini_load_text(ini_file *ini, const char *source)
{
    char *text = xstrdup(source), *line, *next, *section;

    section = xstrdup("");
    for (line = text; line; line = next) {
        char *eq, *s;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        s = str_trim(line);
        /* Skip a UTF-8 byte order mark on the first line. */
        if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
            (unsigned char)s[2] == 0xBF)
            s += 3;
        if (!*s || *s == ';')
            continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) {
                *end = 0;
                free(section);
                section = xstrdup(str_trim(s + 1));
            }
            continue;
        }
        eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = 0;
        add_entry(ini, section, str_trim(s), str_trim(eq + 1));
    }
    free(section);
    free(text);
}

const char *ini_get(const ini_file *ini, const char *section, const char *key)
{
    int i;
    /* Last definition wins, as with the Windows API reading a hand-edited file. */
    for (i = ini->count - 1; i >= 0; i--)
        if (str_ieq(ini->entries[i].section, section) &&
            str_ieq(ini->entries[i].key, key))
            return ini->entries[i].value;
    return NULL;
}

long ini_get_long(const ini_file *ini, const char *section, const char *key, long def)
{
    const char *v = ini_get(ini, section, key);
    long n;
    if (!v || parse_long(v, &n) != 0)
        return def;
    return n;
}

void ini_free(ini_file *ini)
{
    int i;
    for (i = 0; i < ini->count; i++) {
        free(ini->entries[i].section);
        free(ini->entries[i].key);
        free(ini->entries[i].value);
    }
    free(ini->entries);
    memset(ini, 0, sizeof *ini);
}
