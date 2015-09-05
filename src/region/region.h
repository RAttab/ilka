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
typedef uint64_t ilka_off_t;
typedef uint16_t ilka_epoch_t;

struct ilka_options
{
    bool open;
    bool create;
    bool writable;
    bool huge_tlb;
    bool populate;
}

struct ilka_region * ilka_open(const char *file, struct ilka_options *options);
void ilka_close(struct ilka_region *r);
ilka_off_t ilka_grow(struct ilka_region *r, size_t len);

const void * ilka_read(struct ilka_region *r, ilka_off_t off, size_t len);
void * ilka_write(struct ilka_region *r, ilka_off_t off, size_t len);

void ilka_save(struct ilka_region *r);

ilka_off_t ilka_alloc(struct ilka_region *r, size_t len);
void ilka_free(struct ilka_region *r, ilka_off_t off, size_t len);
void ilka_defer_free(struct ilka_region *r, ilka_off_t off, size_t len);

ilka_epoch_t ilka_enter(struct ilka_region *r);
void ilka_exit(struct ilka_region *r, ilka_epoch_t h);

void ilka_world_stop(struct ilka_region *r);
void ilka_world_resume(struct ilka_region *r);
