/* region.h
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   Ilka memory region management.
*/

#pragma once

#include "utils/compiler.h"
#include "utils/atomic.h"


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


inline ilka_ptr ilka_region_alloc(struct ilka_region *r, size_t n) ilka_malloc
{
    return malloc(n);
}
inline void ilka_region_free(struct ilka_region *r, ilka_ptr_t ptr)
{
    free(ptr);
}

inline void ilka_region_pin_read(struct ilka_region *r) {}
inline void * ilka_region_read(struct ilka_region *r, ilka_ptr_t ptr) { return ptr; }
inline void ilka_region_unpin_read(struct ilka_region *r) {}

inline void * ilka_region_pin_write(struct ilka_region *r, ilka_ptr_t ptr) { return ptr; }
inline void ilka_region_unpin_write(struct ilka_region *r, void *ptr) {}

inline void ilka_region_lock(struct ilka_region *r, uint8_t *data, uint8_t mask)
{
    /* register the lock in the region so that we can clean it up on restart. */
    (void) r;

    uint8_t new;
    uint8_t old = *data;
    const enum memory_order success = memory_order_release;
    const enum memory_order fail = memory_order_relaxed;

    do {
        if (old & mask) continue;
        new = old | mask;
    } while(!ilka_atomic_cmp_xchg(data, &old, new, success, fail));
}

inline void ilka_region_lock(struct ilka_region *r, uint8_t *data, uint8_t mask)
{

    uint8_t old = ilka_atomic_load(data, memory_order_relaxed);
    ilka_atomic_store(data, order & ~mask, memory_order_release);

    /* deregister the lock */
    (void) r;
}
