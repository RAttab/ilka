/* region.h
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   Ilka memory region management.
*/

#pragma once

// -----------------------------------------------------------------------------
// options
// -----------------------------------------------------------------------------

struct ilka_options
{
    bool open;
    bool create;
    bool read_only;

    bool huge_tlb;
    bool populate;
    size_t vma_reserved;

    size_t alloc_areas;
    size_t epoch_gc_freq_usec;
};


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

struct ilka_region;
typedef uint64_t ilka_off_t;
enum { ilka_off_bits = 64 - ILKA_MCHECK_TAG_BITS };

struct ilka_region * ilka_open(const char *file, struct ilka_options *options);
bool ilka_close(struct ilka_region *r);
bool ilka_rm(struct ilka_region *r);

size_t ilka_len(struct ilka_region *r);
ilka_off_t ilka_grow(struct ilka_region *r, size_t len);

ilka_off_t ilka_get_root(struct ilka_region *r);
void ilka_set_root(struct ilka_region *r, ilka_off_t root);

const void * ilka_read(struct ilka_region *r, ilka_off_t off, size_t len);
void * ilka_write(struct ilka_region *r, ilka_off_t off, size_t len);

bool ilka_save(struct ilka_region *r);

ilka_off_t ilka_alloc(struct ilka_region *r, size_t len);
ilka_off_t ilka_alloc_in(struct ilka_region *r, size_t len, size_t area);
void ilka_free(struct ilka_region *r, ilka_off_t off, size_t len);
void ilka_free_in(struct ilka_region *r, ilka_off_t off, size_t len, size_t area);
bool ilka_defer_free(struct ilka_region *r, ilka_off_t off, size_t len);
bool ilka_defer_free_in(struct ilka_region *r, ilka_off_t off, size_t len, size_t area);

bool ilka_enter(struct ilka_region *r);
void ilka_exit(struct ilka_region *r);
bool ilka_defer(struct ilka_region *r, void (*fn) (void *), void *data);

void ilka_world_stop(struct ilka_region *r);
void ilka_world_resume(struct ilka_region *r);
