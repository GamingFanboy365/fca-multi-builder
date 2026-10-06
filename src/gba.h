/*
 * gba.h - GBA cartridge header helpers ("Fix GBA header" in NES2FCA).
 */
#ifndef FCA_GBA_H
#define FCA_GBA_H

#include <stddef.h>
#include <stdint.h>

#define GBA_HEADER_SIZE 0xC0

/* Returns non-zero if the image carries the boot logo real hardware checks. */
int gba_has_logo(const uint8_t *rom, size_t len);
/* Header complement check byte over 0xA0..0xBC. */
uint8_t gba_header_checksum(const uint8_t *rom);
int gba_checksum_ok(const uint8_t *rom, size_t len);

/*
 * Rewrites the header the way NES2FCA's "Fix GBA header" did: inserts the
 * boot logo, sets the 12-byte title, 4-byte game code and 2-byte maker code,
 * the fixed 0x96 byte, clears the reserved bytes and recomputes the
 * complement check.  NULL code/maker keep what the image already has.
 */
void gba_fix_header(uint8_t *rom, size_t len, const char *title,
                    const char *code, const char *maker);

#endif
