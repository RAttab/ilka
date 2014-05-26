/* bit_coder.h
   Rémi Attab (remi.attab@gmail.com), 10 May 2014
   FreeBSD-style copyright and disclaimer apply

   bit encoder decoder.

   Note that we want bit_decode and bit_encode to be inline because the bits
   parameter will usually be constant I'm hoping the compiler will be able to do
   lots of DCE and constant propagation over long running sequences of call.
   I need to confirm alot of this or optimize this code more carefully.

   \todo There are x86 assembly instruction that can code across two uint64_t
   word with the need for shifting and whatnot.

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

inline void bit_decode_skip(struct bit_decoder *coder, size_t bits)
{
    ilka_assert(bits <= coder->size + coder->pos,
            "skipping <%zu> bits with only <%zu> bits available",
            bits, coder->size * 8 + coder->pos);

    coder->pos += bits;
    coder->data += coder->pos / 8;
    coder->size -= coder->pos / 8;
    coder->pos %= 8;
}

inline uint64_t
bit_decode(struct bit_decoder *coder, size_t bits)
{
    ilka_assert(bits <= coder->size + coder->pos,
            "decoding <%zu> bits with only <%zu> bits available",
            bits, coder->size * 8 + coder->pos);

    uint64_t value = *((uint64_t*) coder->data);
    value = (value >> coder->pos) & ((1 << bits) - 1);

    bit_decode_skip(coder, bits);
    return value;
}

inline uint64_t
bit_decode_atomic(struct bit_decoder *coder, size_t bits, enum memory_order order)
{
    ilka_assert(bits <= coder->size + coder->pos,
            "decoding <%zu> bits with only <%zu> bits available",
            bits, coder->size * 8 + coder->pos);

    uint64_t value = ilka_atomic_load((uint64_t*) coder->data, order);
    value = (value >> coder->pos) & ((1 << bits) - 1);

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


inline void
bit_encode_skip(struct bit_encoder *coder, size_t bits)
{
    ilka_assert(bits <= coder->size + coder->pos,
            "skipping <%zu> bits with only <%zu> bits available",
            bits, coder->size * 8 + coder->pos);

    coder->pos += bits;
    coder->data += coder->pos / 8;
    coder->size -= coder->pos / 8;
    coder->pos %= 8;
}

inline void
bit_encode(struct bit_encoder *coder, uint64_t value, size_t bits)
{
    ilka_assert(bits <= coder->size + coder->pos,
            "encoding <%zu> bits with only <%zu> bits available",
            bits, coder->size * 8 + coder->pos);

    uint64_t mask = ((1ULL << bits) - 1) << coder->pos;
    value = (value << coder->pos) & mask;

    uint64_t* p = (uint64_t*) coder->data;
    *p = (*p & ~mask) | value;

    bit_encode_skip(coder, bits);
}


inline void
bit_encode_atomic(
        struct bit_encoder *coder,
        uint64_t value, size_t bits,
        enum memory_order order)
{
    ilka_assert(bits <= coder->size + coder->pos,
            "encoding <%zu> bits with only <%zu> bits available",
            bits, coder->size * 8 + coder->pos);

    uint64_t mask = ((1ULL << bits) - 1) << coder->pos;
    value = (value << coder->pos) & mask;

    uint64_t* p = (uint64_t*) coder->data;
    ilka_atomic_store(p, (*p & ~mask) | value, order);

    bit_encode_skip(coder, bits);
}
