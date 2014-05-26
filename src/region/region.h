/* region.h
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   Ilka memory region management.
*/

#pragma once

#include "utils/alloc.h"
#include "utils/compiler.h"
#include "utils/atomic.h"

#include <stdlib.h>


// -----------------------------------------------------------------------------
// ilka ptr
// -----------------------------------------------------------------------------

/* Must be pinned by the region before it can be accessed through a regular
 * pointer. This allows the region to keep track of ongoing reads and writes and
 * block them when necessary. */
typedef uint64_t ilka_ptr_t;


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

struct ilka_region;


inline ilka_ptr_t ilka_region_alloc(struct ilka_region *r, size_t n, size_t align)
{
    (void) r;
    return (ilka_ptr_t) ilka_aligned_alloc(align, n);
}
inline void ilka_region_free(struct ilka_region *r, ilka_ptr_t ptr)
{
    (void) r;
    free((void *) ptr);
}

inline void ilka_region_unpin_read(struct ilka_region *r) { (void) r; }
inline void ilka_region_pin_read(struct ilka_region *r) { (void) r; }
inline void * ilka_region_read(struct ilka_region *r, ilka_ptr_t ptr)
{
    (void) r;
    return (void *) ptr;
}

inline void * ilka_region_pin_write(struct ilka_region *r, ilka_ptr_t ptr)
{
    (void) r;
    return (void *) ptr;
}
inline void ilka_region_unpin_write(struct ilka_region *r, void *ptr)
{
    (void) r;
    (void) ptr;
}
