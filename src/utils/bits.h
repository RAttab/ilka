/* bits.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   Bit ops header.
*/

#pragma once

inline size_t clz(uint64_t x) { return __builtin_clzll(x); }
inline size_t ctz(uint64_t x) { return __builtin_ctzll(x); }
inline size_t log2(uint64_t x) { return (sizeof(x) * 8) - clz(x); }

inline size_t pop(uint64_t x) { return __builtin_popcountll(x); }
