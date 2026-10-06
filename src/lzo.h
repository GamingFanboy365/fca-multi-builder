/*
 * lzo.h - LZO1X compression, stream-compatible with the LZO 1.07 library
 * PocketNES uses for its save records.
 */
#ifndef FCA_LZO_H
#define FCA_LZO_H

#include <stddef.h>
#include <stdint.h>

/* Worst-case compressed size for n input bytes. */
#define LZO1X_BOUND(n) ((n) + (n) / 16 + 64 + 3)

/* Compresses in[0..in_len) into out (at least LZO1X_BOUND(in_len) bytes).
 * Returns the compressed length. */
size_t lzo1x_compress(const uint8_t *in, size_t in_len, uint8_t *out);

/* Decompresses with full bounds checking.  On entry *out_len is the size of
 * out; on success it is set to the decompressed length and 0 is returned.
 * Bytes after the end-of-stream marker (padding) are ignored.  Returns -1
 * on corrupt or truncated input, or if out is too small. */
int lzo1x_decompress_safe(const uint8_t *in, size_t in_len,
                          uint8_t *out, size_t *out_len);

#endif
