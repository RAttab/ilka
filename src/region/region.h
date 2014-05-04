/* region.h
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   Ilka memory region management.
*/

#pragma once

#include "compiler.h"

// -----------------------------------------------------------------------------
// regionn
// -----------------------------------------------------------------------------

typedef uint64_t ilka_ptr;

struct ilka_region;

ilka_ptr ilka_region_alloc(struct ilka_region *r, size_t n) ilka_malloc
{
    return malloc(n);
}

void ilka_region_free(struct ilka_region *r, ilka_ptr ptr)
{
    free(ptr);
}

void * ilka_region_pin(struct ilka_region *r, ilka_ptr ptr, size_t n) {}
void ilka_region_unpin(struct ilka_region *r, ilka_ptr ptr) {}
