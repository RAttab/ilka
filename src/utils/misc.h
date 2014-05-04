/* misc.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   Random collection of stuff.
*/

#pragma once

#include "compiler.h"


// -----------------------------------------------------------------------------
// ceil div
// -----------------------------------------------------------------------------

inline size_t ilka_ceil_div_s(size_t n, size_t d) ilka_pure
{
    return n ? ((n - 1) / d) + 1 : 0;
}
