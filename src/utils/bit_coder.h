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


// -----------------------------------------------------------------------------
// bit decoder
// -----------------------------------------------------------------------------

struct bit_decoder
{
    const uint64_t *start;
    const uint64_t *data;
    size_t size; // bytes
    size_t pos;  // bits
};


inline void bit_decoder_init(struct bit_decoder *coder, const uint64_t *data, size_t data_n)
{
    *coder = {
        .start = data,
        .data = data,
        .size = data_n
    };
}

inline void bit_decoder_offset(struct bit_decoder *coder)
{
    return (coder->data - coder->start) * 8 + coder->pos;
}

inline uint64_t bit_decode(struct bit_decoder *coder, size_t bits)
{
    if (coder->size < ceil_div(bits, 8)) {
        ilka_error("decoding <%zu> bits with only <%zu> bytes available",
                coder->size, bits);
    }

    uint64_t value = *coder->data >> coder->pos;
    uint64_t mask = (1 << bits) - 1;

    if (bits + coder->pos >= 64) {
        coder->data++;
        coder->size -= sizeof(uint64_t);
        value |= *coder->data << (64 - coder->pos);
    }

    coder->pos = (coder->pos + bits) % sizeof(uint64_t);
    return value & mask;
}

inline uint64_t bit_decode_atomic(
        struct bit_decoder *coder, size_t bits, enum memory_model model)
{
    if (bits > 64 - coder->pos) {
        ilka_error("decoding <%zu> atomic bits with only <%d> bits available",
                bits, 64 - coder->pos);
    }

    uint64_t value = ilka_atomic_load(coder->data, model);
    value >>= coder->pos;
    uint64_t mask = (1 << bits) - 1;

    if (bits + coder->pos >= 64) {
        coder->data++;
        coder->size -= sizeof(uint64_t);
    }

    coder->pos = (coder->pos + bits) % sizeof(uint64_t);
    return value & mask;
}

inline void bit_decode_skip(struct bit_decoder *coder, size_t bits)
{
    if (size < ceil_div(bits, 8)) {
        ilka_error("skipping <%zu> bits with only <%zu> bytes available",
                size, bits);
    }

    if (bits < (64 - coder->pos)) {
        coder->pos += bits;
        return;
    }

    bits -= 64 - coder->pos;

    size_t inc = ceil_div(bits, 64);
    coder->data += inc;
    coder->size -= inc * sizeof(uint64_t);
    coder->pos = bits % 64;
}


// -----------------------------------------------------------------------------
// bit encoder
// -----------------------------------------------------------------------------

struct bit_encoder
{
    uint64_t *start;
    uint64_t *data;
    size_t size; // bytes
    size_t pos;  // bits
};

inline void bit_encoder_init(struct bit_encoder *coder, const uint64_t *data, size_t data_n)
{
    *coder = {
        .start = data,
        .data = data,
        .size = data_n
    };
}

inline void bit_encoder_offset(struct bit_encoder *coder)
{
    return (coder->data - coder->start) * 8 + coder->pos;
}

inline void bit_encode(struct bit_encoder *coder, uint64_t value, size_t bits)
{
    if (size < ceil_div(bits, 8)) {
        ilka_error("encoding <%zu> bits with only <%zu> bytes available",
                size, bits);
    }

    value &= (1 << bits) - 1;
    *data |= value << coder->pos;

    if (bits + coder->pos >= 64) {
        coder->data++;
        coder->size -= sizeof(uint64_t);
        *coder->data |= value >> (64 - coder->pos);
    }

    coder->pos = (coder->pos + bits) % sizeof(uint64_t);
}


inline void bit_encode_atomic(
        struct bit_encoder *coder,
        uint64_t value, size_t bits,
        enum memory_model model)
{
    if (bits < 64 - coder->pos) {
        ilka_error("encoding <%zu> atomic bits with only <%d> bits available",
                bits, 64 - coder->pos);
    }

    value &= (1 << bits) - 1;
    (void) ilka_atom_or_fetch(&coder->data, value << coder->pos, model);

    if (bits + coder->pos >= 64) {
        coder->data++;
        coder->size -= sizeof(uint64_t);
    }

    coder->pos = (coder->pos + bits) % sizeof(uint64_t);
}

inline void bit_encode_skip(struct bit_encode *coder, size_t bits)
{
    if (size < ceil_div(bits, 8)) {
        ilka_error("skipping <%zu> bits with only <%zu> bytes available",
                size, bits);
    }

    if (bits < (64 - coder->pos)) {
        coder->pos += bits;
        return;
    }

    bits -= 64 - coder->pos;

    size_t inc = ceil_div(bits, 64);
    coder->data += inc;
    coder->size -= inc * sizeof(uint64_t);
    coder->pos = bits % 64;
}
