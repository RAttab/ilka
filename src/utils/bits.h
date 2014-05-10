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
// bitfields
// -----------------------------------------------------------------------------

// for (size_t i = bitfield_next(bf); i < 64; i = bitfield_next(bf, i + 1)) {}
static size_t bitfield_next(uint64_t bf, size_t bit = 0)
{
    bf &= (1ULL << bit) -1;
    return ctz(bf);
}
