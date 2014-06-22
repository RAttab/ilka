/* alloc.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   memory allocation utils.
*/

#pragma once

#include <stdlib.h>

// -----------------------------------------------------------------------------
// alloc align
// -----------------------------------------------------------------------------

inline void * ilka_aligned_alloc(size_t alignment, size_t size)
{
    return aligned_alloc(alignment, size);
}
