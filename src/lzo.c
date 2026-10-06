/*
 * lzo.c - LZO1X compression and decompression.
 *
 * A small independent implementation of the LZO1X stream format.  The
 * compressor is a straightforward greedy hash-chain matcher (it does not
 * reproduce liblzo's exact output, but any LZO1X decoder, including the one
 * built into PocketNES, reads it).  The decompressor checks every read and
 * write against the buffer bounds.
 */
#include "lzo.h"

#include <stdlib.h>
#include <string.h>

#define M2_MAX_LEN 8
#define M2_MAX_OFFSET 0x0800
#define M3_MAX_OFFSET 0x4000
#define M4_MAX_OFFSET 0xBFFF
#define MIN_MATCH 3
#define HASH_BITS 14
#define CHAIN_LIMIT 32

static uint8_t *put_count(uint8_t *op, size_t n)
{
    /* Extended length: zero bytes each worth 255, then the remainder. */
    while (n > 255) {
        *op++ = 0;
        n -= 255;
    }
    *op++ = (uint8_t)n;
    return op;
}

/* Emits pending literals.  prev_match points at the low distance byte of the
 * previous match (NULL if none yet), whose two low bits can carry a literal
 * count of 1..3. */
static uint8_t *put_literals(uint8_t *op, uint8_t *out, uint8_t *prev_match,
                             const uint8_t *lit, size_t n)
{
    if (n == 0)
        return op;
    if (op == out && n <= 238) {
        *op++ = (uint8_t)(17 + n);
    } else if (n <= 3 && prev_match) {
        *prev_match |= (uint8_t)n;
    } else if (n <= 18) {
        *op++ = (uint8_t)(n - 3);
    } else {
        *op++ = 0;
        op = put_count(op, n - 18);
    }
    memcpy(op, lit, n);
    return op + n;
}

static uint8_t *put_match(uint8_t *op, size_t len, size_t dist, uint8_t **low)
{
    if (len <= M2_MAX_LEN && dist <= M2_MAX_OFFSET) {
        dist -= 1;
        *op++ = (uint8_t)(((len - 1) << 5) | ((dist & 7) << 2));
        *low = op - 1;
        *op++ = (uint8_t)(dist >> 3);
        return op;
    }
    if (dist <= M3_MAX_OFFSET) {
        dist -= 1;
        if (len <= 33) {
            *op++ = (uint8_t)(32 | (len - 2));
        } else {
            *op++ = 32;
            op = put_count(op, len - 33);
        }
    } else {
        dist -= 0x4000;
        if (len <= 9) {
            *op++ = (uint8_t)(16 | ((dist & 0x4000) >> 11) | (len - 2));
        } else {
            *op++ = (uint8_t)(16 | ((dist & 0x4000) >> 11));
            op = put_count(op, len - 9);
        }
    }
    *low = op;
    *op++ = (uint8_t)((dist & 63) << 2);
    *op++ = (uint8_t)(dist >> 6);
    return op;
}

static unsigned hash3(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    return (unsigned)((v * 2654435761u) >> (32 - HASH_BITS));
}

size_t lzo1x_compress(const uint8_t *in, size_t in_len, uint8_t *out)
{
    static long head[1 << HASH_BITS];
    long *prev = NULL;
    const uint8_t *ip = in, *lit = in, *end = in + in_len;
    uint8_t *op = out, *low = NULL;
    size_t i;

    for (i = 0; i < (1u << HASH_BITS); i++)
        head[i] = -1;
    if (in_len >= MIN_MATCH) {
        prev = malloc(in_len * sizeof(long));
    }

    while (prev && ip + MIN_MATCH <= end) {
        size_t pos = (size_t)(ip - in), best_len = 0, best_dist = 0;
        unsigned h = hash3(ip);
        long cand = head[h];
        int chain = 0;

        while (cand >= 0 && chain++ < CHAIN_LIMIT) {
            size_t dist = pos - (size_t)cand, len = 0;
            if (dist > M4_MAX_OFFSET)
                break;
            while (ip + len < end && in[cand + len] == ip[len])
                len++;
            if (len > best_len) {
                best_len = len;
                best_dist = dist;
            }
            cand = prev[cand];
        }
        /* Far matches cost more; skip ones that barely pay off. */
        if (best_len >= MIN_MATCH && best_dist > M2_MAX_OFFSET && best_len < 4)
            best_len = 0;

        prev[pos] = head[h];
        head[h] = (long)pos;

        if (best_len < MIN_MATCH) {
            ip++;
            continue;
        }
        op = put_literals(op, out, low, lit, (size_t)(ip - lit));
        op = put_match(op, best_len, best_dist, &low);
        /* Index the bytes covered by the match too. */
        for (i = 1; i < best_len && ip + i + MIN_MATCH <= end; i++) {
            size_t p = pos + i;
            unsigned hh = hash3(in + p);
            prev[p] = head[hh];
            head[hh] = (long)p;
        }
        ip += best_len;
        lit = ip;
    }
    op = put_literals(op, out, low, lit, (size_t)(end - lit));
    /* End-of-stream marker: an M4 match with distance 0. */
    *op++ = 16 | 1;
    *op++ = 0;
    *op++ = 0;
    free(prev);
    return (size_t)(op - out);
}

int lzo1x_decompress_safe(const uint8_t *in, size_t in_len,
                          uint8_t *out, size_t *out_len)
{
    const uint8_t *ip = in, *ip_end = in + in_len;
    uint8_t *op = out, *op_end = out + *out_len;
    size_t t;
    int state = 0; /* literals copied by the previous instruction */

#define NEED_IN(n) do { if ((size_t)(ip_end - ip) < (size_t)(n)) return -1; } while (0)
#define NEED_OUT(n) do { if ((size_t)(op_end - op) < (size_t)(n)) return -1; } while (0)
#define COPY_LIT(n) do { NEED_IN(n); NEED_OUT(n); memcpy(op, ip, n); op += n; ip += n; } while (0)
#define EXTEND(base) do { \
        while (1) { NEED_IN(1); if (*ip) break; t += 255; ip++; } \
        t += (base) + *ip++; } while (0)

    NEED_IN(1);
    if (*ip > 17) {
        t = (size_t)(*ip++ - 17);
        COPY_LIT(t);
        state = t < 4 ? (int)t : 4;
    }
    for (;;) {
        const uint8_t *m_pos;
        size_t m_len;

        NEED_IN(1);
        t = *ip++;
        if (t < 16) {
            if (state == 0) {
                /* Literal run. */
                if (t == 0)
                    EXTEND(15);
                COPY_LIT(t + 3);
                state = 4;
                continue;
            }
            if (state == 4) {
                /* Three-byte match just after a long literal run. */
                NEED_IN(1);
                m_len = 3;
                t = 1 + M2_MAX_OFFSET + (t >> 2) + ((size_t)*ip++ << 2);
            } else {
                /* Two-byte match just after a short literal tail. */
                NEED_IN(1);
                m_len = 2;
                t = 1 + (t >> 2) + ((size_t)*ip++ << 2);
            }
            if (t > (size_t)(op - out))
                return -1;
            m_pos = op - t;
        } else if (t >= 64) {
            NEED_IN(1);
            m_len = (t >> 5) + 1;
            t = 1 + ((t >> 2) & 7) + ((size_t)*ip++ << 3);
            if (t > (size_t)(op - out))
                return -1;
            m_pos = op - t;
        } else if (t >= 32) {
            t &= 31;
            if (t == 0)
                EXTEND(31);
            m_len = t + 2;
            NEED_IN(2);
            t = 1 + ((size_t)ip[0] >> 2) + ((size_t)ip[1] << 6);
            ip += 2;
            if (t > (size_t)(op - out))
                return -1;
            m_pos = op - t;
        } else {
            size_t dist = (t & 8) << 11;
            t &= 7;
            if (t == 0)
                EXTEND(7);
            m_len = t + 2;
            NEED_IN(2);
            dist += ((size_t)ip[0] >> 2) + ((size_t)ip[1] << 6);
            ip += 2;
            if (dist == 0) {
                /* End of stream. */
                /* Bytes after the end marker (word padding) are ignored. */
                *out_len = (size_t)(op - out);
                return 0;
            }
            dist += 0x4000;
            if (dist > (size_t)(op - out))
                return -1;
            m_pos = op - dist;
        }
        NEED_OUT(m_len);
        while (m_len--)
            *op++ = *m_pos++; /* may overlap: byte by byte on purpose */

        /* Low two bits of the last distance byte = literals that follow. */
        state = ip[-2] & 3;
        if (state)
            COPY_LIT((size_t)state);
    }
#undef NEED_IN
#undef NEED_OUT
#undef COPY_LIT
#undef EXTEND
}
