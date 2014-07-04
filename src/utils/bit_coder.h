/* bit_coder.h
   Rémi Attab (remi.attab@gmail.com), 10 May 2014
   FreeBSD-style copyright and disclaimer apply

   bit encoder decoder.

   \todo confirm that compiler can take advantage of the inlining.
*/

#pragma once

#include "bits.h"
#include "error.h"
#include "atomic.h"

// -----------------------------------------------------------------------------
// bit decoder
// -----------------------------------------------------------------------------

struct bit_decoder
{
    const uint8_t *start;
    const uint8_t *data;
    size_t size; // bytes
    size_t pos;  // bits
};


inline void
bit_decoder_init(struct bit_decoder *coder, const void *data, size_t data_n)
{
    coder->start = data;
    coder->data = data;
    coder->size = data_n;
    coder->pos = 0;
}

inline size_t
bit_decoder_offset(struct bit_decoder *coder)
{
    return (coder->data - coder->start) * 8 + coder->pos;
}

inline size_t
bit_decoder_leftover(struct bit_decoder *coder)
{
    return ((coder->size - 1) * 8) + (8 - coder->pos);
}

inline void
bit_decoder_check(struct bit_decoder *coder, size_t bits)
{
    ilka_assert(bits <= coder->size * 8 - coder->pos,
            "skipping <%zu> bits with only <%zu> bits available",
            bits, coder->size * 8 + coder->pos);
}

inline void bit_decode_skip(struct bit_decoder *coder, size_t bits)
{
    bit_decoder_check(coder, bits);

    coder->pos += bits;
    coder->data += coder->pos / 8;
    coder->size -= coder->pos / 8;
    coder->pos %= 8;
}

inline uint64_t
bit_decode(struct bit_decoder *coder, size_t bits)
{
    bit_decoder_check(coder, bits);

    uint64_t value = *((uint64_t*) coder->data) >> coder->pos;

    size_t avail = 64 - coder->pos;
    bit_decode_skip(coder, avail < bits ? avail : bits);

    if (avail < bits) {
        uint64_t leftover = *((uint64_t*) coder->data) >> coder->pos;
        value |= leftover << avail;

        bit_decode_skip(coder, bits - avail);
    }

    uint64_t mask = bits >= 64 ? -1UL : ((1UL << bits) - 1);
    return value & mask;
}

inline void
bit_decode_align(struct bit_decoder *coder)
{
    if (coder->pos) bit_decode_skip(coder, 8 - coder->pos);
}

inline uint64_t
bit_decode_atomic(struct bit_decoder *coder, size_t bits, enum memory_order order)
{
    bit_decoder_check(coder, bits);
    ilka_assert(bits + coder->pos <= 64,
            "misaligned atomic bit decoding <%zu>",
            bits + coder->pos);

    uint64_t value = ilka_atomic_load((uint64_t*) coder->data, order);
    value = (value >> coder->pos) & ((1UL << bits) - 1);

    bit_decode_skip(coder, bits);
    return value;
}


// -----------------------------------------------------------------------------
// bit encoder
// -----------------------------------------------------------------------------

struct bit_encoder
{
    uint8_t *start;
    uint8_t *data;
    size_t size; // bytes
    size_t pos;  // bits
};

inline void
bit_encoder_init(struct bit_encoder *coder, void *data, size_t data_n)
{
    coder->start = data;
    coder->data = data;
    coder->size = data_n;
    coder->pos = 0;
}

inline size_t
bit_encoder_offset(struct bit_encoder *coder)
{
    return (coder->data - coder->start) * 8 + coder->pos;
}

inline size_t
bit_encoder_leftover(struct bit_encoder *coder)
{
    return ((coder->size - 1) * 8) + (8 - coder->pos);
}

inline void
bit_encoder_check(struct bit_encoder *coder, size_t bits)
{
    ilka_assert(bits <= coder->size * 8 - coder->pos,
            "skipping <%zu> bits with only <%zu> bits available",
            bits, coder->size * 8 + coder->pos);
}

inline void
bit_encode_skip(struct bit_encoder *coder, size_t bits)
{
    bit_encoder_check(coder, bits);

    coder->pos += bits;
    coder->data += coder->pos / 8;
    coder->size -= coder->pos / 8;
    coder->pos %= 8;
}

inline void
bit_encode(struct bit_encoder *coder, uint64_t value, size_t bits)
{
    bit_encoder_check(coder, bits);

    uint64_t mask = bits >= 64 ? -1UL : ((1UL << bits) - 1);
    value &= mask;

    uint64_t *p = (uint64_t *) coder->data;
    *p = (*p & ~(mask << coder->pos)) | (value << coder->pos);

    size_t avail = 64 - coder->pos;
    bit_encode_skip(coder, avail < bits ? avail : bits);

    if (avail < bits) {
        uint64_t *p = (uint64_t *) coder->data;
        *p = (*p & ~(mask >> avail)) | (value >> avail);

        bit_encode_skip(coder, bits - avail);
    }
}

inline void
bit_encode_align(struct bit_encoder *coder)
{
    if (coder->pos) bit_encode_skip(coder, 8 - coder->pos);
}

inline void
bit_encode_atomic(
        struct bit_encoder *coder,
        uint64_t value, size_t bits,
        enum memory_order order)
{
    bit_encoder_check(coder, bits);
    ilka_assert(bits + coder->pos <= 64,
            "misaligned atomic bit encoding <%zu>",
            bits + coder->pos);

    uint64_t mask = ((1UL << bits) - 1) << coder->pos;
    value = (value << coder->pos) & mask;

    uint64_t* p = (uint64_t*) coder->data;
    ilka_atomic_store(p, (*p & ~mask) | value, order);

    bit_encode_skip(coder, bits);
}
