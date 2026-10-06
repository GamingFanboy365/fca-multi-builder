/*
 * text.h - title/text conversion.
 *
 * FamicomAdvance stores titles as EUC-JP (it has a Japanese font); the
 * original NES2FCA took Shift-JIS from the Windows UI and converted it.  The
 * PocketNES and PCEAdvance menus only render ASCII.  The new tools work in
 * UTF-8 and convert at the boundary.
 */
#ifndef FCA_TEXT_H
#define FCA_TEXT_H

#include <stddef.h>

/* Returns non-zero if s is well-formed UTF-8. */
int utf8_valid(const char *s);

/* Text read from old (Japanese Windows) list/ini files may be Shift-JIS.
 * Returns a malloc'd UTF-8 copy: unchanged if already UTF-8, otherwise
 * decoded as Shift-JIS (characters outside the FCA set become '?'). */
char *text_from_legacy(const char *s);

/* Convert a UTF-8 title to EUC-JP for the FamicomAdvance file system,
 * truncated like NES2FCA did: at most max_chars characters and max_bytes
 * bytes (a double-byte character is never split).  Characters the FCA font
 * cannot show become '?'.  *lossy is set if anything was replaced. */
char *title_to_euc(const char *utf8, int max_chars, int max_bytes, int *lossy);

/* Decode an EUC-JP title (as stored by FCA) to UTF-8 for display. */
char *euc_to_utf8(const char *euc, size_t max_bytes);

/* Convert a UTF-8 title to printable ASCII (accents folded, full-width
 * ASCII narrowed, anything else '?'), truncated to max_bytes. */
char *title_to_ascii(const char *utf8, int max_bytes, int *lossy);

#endif
