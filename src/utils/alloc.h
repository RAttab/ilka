/* alloc.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   memory allocation utils.
*/

#pragma once

#include "error.h"
#include "compiler.h"


// alloc align
// -----------------------------------------------------------------------------

inline void * ilka_alloc_align(size_t n, size_t align = n)
    ilka_malloc
{
    void *ptr;
    if (ilka_likely(!posix_memalign(&ptr, align, n))) return ptr;

    ilka_error_errno("aligned alloc failed for size <%ul> and alignment <%ul>",
            n, align);
}
