/* bits.c
   Rémi Attab (remi.attab@gmail.com), 07 May 2014
   FreeBSD-style copyright and disclaimer apply

   Bit ops implementation.
*/

#include "bits.h"

// -----------------------------------------------------------------------------
// bit coder
// -----------------------------------------------------------------------------
// \todo There are x86 assembly instruction that can code across two uint64_t
// word with the need for shifting and whatnot.

uint64_t bit_decode(struct bit_decoder *coder, size_t bits)
{
    if (coder->size < ceil_div(bits, 8)) {
        ilka_error("decoding <%zu> bits with only <%zu> bytes available",
                coder->size, bits);
    }

    uint64_t value = *coder->data >> pos;
    uint64_t mask = (1 << bits) - 1;

    if (bits + coder->pos >= 64) {
        coder->data++;
        coder->size -= sizeof(uint64_t);
        value |= *coder->data << (64 - coder->pos);
    }

    coder->pos = (coder->pos + bits) % sizeof(uint64_t);
    return value & mask;
}

void bit_decode_skip(struct bit_decoder *coder, size_t bits)
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


void bit_encode(struct bit_encoder *coder, uint64_t value, size_t bits)
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

void bit_encode_skip(struct bit_encode *coder, size_t bits)
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
