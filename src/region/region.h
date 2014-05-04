/* region.h
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   Ilka memory region management.
*/

#pragma once

#include "compiler.h"


// -----------------------------------------------------------------------------
// ilka ptr
// -----------------------------------------------------------------------------

// Must be pinned by the region before it can be accessed through a regular
// pointer. This allows the region to keep track of ongoing reads and writes and
// block them when necessary.
typedef uint64_t ilka_ptr;


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

struct ilka_region;


inline ilka_ptr ilka_region_alloc(struct ilka_region *r, size_t n) ilka_malloc
{
    return malloc(n);
}
inline void ilka_region_free(struct ilka_region *r, ilka_ptr ptr)
{
    free(ptr);
}

inline void * ilka_region_pin_r(struct ilka_region *r, ilka_ptr ptr) { return ptr; }
inline void * ilka_region_pin_w(struct ilka_region *r, ilka_ptr ptr) {return ptr; }
inline void ilka_region_pin_upgrade(struct ilka_region *r) {}
inline void ilka_region_unpin_r(struct ilka_region *r) {}
inline void ilka_region_unpin_w(struct ilka_region *r) {}
