/* region.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

#include "file.c"
#include "alloc.c"

#include "region.h"
#include "utils/arch.h"
#include "utils/lock.h"
#include "utils/atomic.h"

#include <string.h>
#include <stdlib.h>


// -----------------------------------------------------------------------------
// config
// -----------------------------------------------------------------------------

static uint64_t ilka_magic = 0x31906C0FFC1FC856;
static uint64_t ilka_version = 1;

static size_t ilka_min_size = (1 + alloc_min_pages) * ILKA_PAGE_SIZE;
static size_t ilka_alloc_start = 1 * ILKA_PAGE_SIZE;


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

struct meta
{
    uint64_t magic;
    uint64_t version;
    ilka_ptr alloc;
};

struct ilka_region
{
    int fd;
    enum ilka_mode mode;

    ilka_slock lock;

    size_t len;
    void *start;
    struct ilka_alloc *alloc;
};


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

struct meta * region_meta(struct ilka_region *r)
{
    return ilka_read(r, 0, sizeof(struct meta));
}

struct ilka_region * ilka_new_region(const char *file, enum ilka_open_mode mode)
{
    struct ilka_region *r = malloc(sizeof(struct ilka_region));
    memset(r, 0, sizeof(struct ilka_region));
    slock_init(&r->lock);

    r->mode = mode;
    r->fd = file_open(file, r->mode);
    r->len = file_grow(r->fd, ilka_min_size);
    mmap_map(r->fd, r->len);

    struct meta * meta = region_meta(r);
    if (meta->magic != ilka_magic) {
        if (!(r->mode & ilka_create)) ilka_error("invalid magic for file '%s'", file);

        meta->magic = ilka_magic;
        meta->version = ilka_version;
    }

    r->alloc = alloc_init(r, meta->alloc = ilka_alloc_start);

    return r;
}

void ilka_close_region(struct ilka_region *r)
{
    mmap_unmap(r->start, r->len);
    file_close(r->fd);
}

void ilka_grow(struct ilka_region *r, size_t len)
{
    // round up to nearest page.
    size_t modulo = len & (ILKA_PAGE_SIZE - 1);
    if (modulo) len += (ILKA_PAGE_SIZE - modulo);

    if (ilka_atomic_load(&r->len, memory_order_relaxed) >= len) return;

    struct meta * meta = region_meta(r);
    slock_lock(&meta->lock);

    file_grow(r, len);
    if (!mmap_remap_soft(r->start, r->len, len)) {
        ilka_world_stop(r);

        new = mmap_remap_hard(r->start, r->len, len);
        ilka_atomic_store(&r->start, new, memory_order_relaxed);

        ilka_world_resume(r);
    }

    ilka_atomic_store(&r->len, len, memory_order_relaxed);

    slock_unlock(&meta->lock);
}

void * ilka_read(struct ilka_region *r, ilka_ptr off, size_t len)
{
    size_t rlen = ilka_atomic_load(r->len, memory_order_relaxed);
    ilka_assert(off + len <= rlen,
            "invalid read pointer: %lu + %lu > %lu", off, len, rlen);

    // todo: check that we're in an epoch.

    return ilka_atomic_load(r->start, memory_order_relaxed) + off;
}

void * ilka_write(struct ilka_region *r, ilka_ptr off, size_t len)
{
    void *ptr = ilka_read(r, off, len);

    // todo: mark the pages for write-back.

    return ptr;
}


// -----------------------------------------------------------------------------
// epoch
// -----------------------------------------------------------------------------

ilka_epoch ilka_enter(struct ilka_region *r)
{
    ilka_todo();
}

void ilka_exit(struct ilka_region *r, ilka_epoch h)
{
    ilka_todo();
}

void ilka_world_stop(struct ilka_region *r)
{
    ilka_todo();
}

void ilka_world_resume(struct ilka_region *r)
{
    ilka_todo();
}


// -----------------------------------------------------------------------------
// alloc
// -----------------------------------------------------------------------------

ilka_ptr ilka_alloc(struct ilka_region *r, size_t len)
{
    return alloc_new(r->alloc, len);
}

void ilka_free(struct ilka_region *r, ilka_ptr ptr, size_t len)
{
    return alloc_free(r->alloc, len);
}

void ilka_defer_free(struct ilka_region *r, ilka_ptr ptr, size_t len)
{
    ilka_todo();
}
