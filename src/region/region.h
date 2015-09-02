/* region.h
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   Ilka memory region management.
*/

#pragma once


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

struct ilka_region;

enum ilka_open_mode
{
    ilka_open   = 1 << 0,
    ilka_create = 1 << 1,
    ilka_write  = 1 << 2,

    ilka_huge_tlb = 1 << 3,
    ilka_populate = 1 << 3,
};

struct ilka_region * ilka_new_region(const char *file, enum ilka_open_mode mode);
void ilka_close_region(struct ilka_region *r);
void ilka_grow(struct ilka_region *r, size_t len);

void * ilka_read(struct ilka_region *r, ilka_ptr off, size_t len);
void * ilka_write(struct ilka_region *r, ilka_ptr off, size_t len);


// -----------------------------------------------------------------------------
// epoch
// -----------------------------------------------------------------------------

typedef int ilka_epoch;

ilka_epoch ilka_enter(struct ilka_region *r);
void ilka_exit(struct ilka_region *r, ilka_epoch h);
void ilka_world_stop(struct ilka_region *r);
void ilka_world_resume(struct ilka_region *r);


// -----------------------------------------------------------------------------
// alloc
// -----------------------------------------------------------------------------

typedef uint64_t ilka_ptr;

ilka_ptr ilka_alloc(struct ilka_region *r, size_t len);
void ilka_free(struct ilka_region *r, ilka_ptr ptr, size_t len);
void ilka_defer_free(struct ilka_region *r, ilka_ptr ptr, size_t len);
