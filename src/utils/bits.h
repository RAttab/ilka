/* bits.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   Bit ops header.
*/

#pragma once


// -----------------------------------------------------------------------------
// builtin
// -----------------------------------------------------------------------------

inline size_t clz(uint64_t x) { return __builtin_clzll(x); }
inline size_t ctz(uint64_t x) { return __builtin_ctzll(x); }
inline size_t pop(uint64_t x) { return __builtin_popcountll(x); }


// -----------------------------------------------------------------------------
// custom
// -----------------------------------------------------------------------------

inline int is_pow2(uint64_t x) { return pop(x) == 1; }
inline uint64_t ceil_pow2(size_t x)
{
    size_t max_bits = sizeof(uint64_t) * CHAR_BIT;
    return 1ULL << (max_bits - clz(x - 1) + 1);
}

inline size_t ceil_div(size_t n, size_t d)
{
    return n ? ((n - 1) / d) + 1 : 0;
}


// -----------------------------------------------------------------------------
// bit coder
// -----------------------------------------------------------------------------

struct bit_coder
{
    uint64_t *data;
    size_t size; // bytes
    size_t pos;  // bits
}

uint64_t bit_decode(struct bit_coder *coder, size_t bits);
void bit_encode(struct bit_coder *coder, uint64_t value, size_t bits);
