/* region.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

#include "file.c"
#include "mmap.c"
#include "alloc.c"
#include "journal.c"
#include "persist.c"
#include "epoch.c"

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
    ilka_off_t alloc;
    ilka_off_t epoch;
};

struct ilka_region
{
    int fd;
    const char* file;
    struct ilka_options options;

    ilka_slock lock;

    size_t len;
    void *start;

    struct ilka_persist *persist;
    struct ilka_alloc *alloc;
    struct ilka_epoch *epoch;
};


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

struct ilka_region * ilka_open(const char *file, struct ilka_options *options)
{
    journal_recover(file);

    struct ilka_region *r = calloc(1, sizeof(struct ilka_region));
    slock_init(&r->lock);

    r->file = file;
    r->options = *options;
    r->fd = file_open(file, &r->options);
    r->len = file_grow(r->fd, ilka_min_size);
    mmap_map(r->fd, r->len);

    const struct meta * meta = ilka_read(r, 0, sizeof(struct meta));
    if (meta->magic != ilka_magic) {
        if (!r->options.create) ilka_error("invalid magic for file '%s'", file);

        struct meta * m = ilka_write(r, 0, sizeof(struct meta));
        m->magic = ilka_magic;
        m->version = ilka_version;
    }

    if (meta->version != ilka_version) {
        ilka_error("invalid version for file '%s': %lu != %lu",
                file, meta->version, ilka_version);
    }

    r->persist = persist_init(r);
    r->alloc = alloc_init(r, meta->alloc = ilka_alloc_start);
    r->epoch = epoch_init(r, &meta->epoch);

    return r;
}

void ilka_close(struct ilka_region *r)
{
    ilka_save(r);

    epoch_close(r->epoch);
    alloc_close(r->alloc);
    persist_close(r->persist);

    mmap_unmap(r->start, r->len);
    file_close(r->fd);
}

ilka_off_t ilka_grow(struct ilka_region *r, size_t len)
{
    len = ceil_div(len, ILKA_PAGE_SIZE) * ILKA_PAGE_SIZE;

    if (ilka_atomic_load(&r->len, memory_order_relaxed) >= len) return;

    struct meta * meta = region_meta(r);
    slock_lock(&r->lock);

    size_t old_len = r->len;
    size_t new_len = old + len;

    file_grow(r->fd, new_len);
    if (!mmap_remap_soft(r->start, old_len, new_len)) {
        ilka_world_stop(r);

        new_start = mmap_remap_hard(r->start, old_len, new_len);
        ilka_atomic_store(&r->start, new_start, memory_order_relaxed);

        ilka_world_resume(r);
    }

    ilka_atomic_store(&r->len, new_len, memory_order_relaxed);

    slock_unlock(&r->lock);

    return old_len;
}

void * ilka_read(struct ilka_region *r, ilka_off_t off, size_t len)
{
    size_t rlen = ilka_atomic_load(r->len, memory_order_relaxed);
    ilka_assert(off + len <= rlen,
            "invalid read pointer: %lu + %lu > %lu", off, len, rlen);

    return ilka_atomic_load(r->start, memory_order_relaxed) + off;
}

void * ilka_write(struct ilka_region *r, ilka_off_t off, size_t len)
{
    void *ptr = ilka_read(r, off, len);
    persist_mark(r->persist, off, len);
    return ptr;
}

void ilka_save(struct ilka_region *r)
{
    persist_save(r->persist);
}

ilka_off_t ilka_alloc(struct ilka_region *r, size_t len)
{
    return alloc_new(r->alloc, off, len);
}

void ilka_free(struct ilka_region *r, ilka_off_t off, size_t len)
{
    return alloc_free(r->alloc, off, len);
}

void ilka_defer_free(struct ilka_region *r, ilka_off_t off, size_t len)
{
    epoch_defer_free(r->epoch, off, len);
}

ilka_epoch_t ilka_enter(struct ilka_region *r)
{
    return epoch_enter(r->epoch);
}

void ilka_exit(struct ilka_region *r, ilka_epoch_t epoch)
{
    epoch_exit(r->epoch, epoch);
}

void ilka_world_stop(struct ilka_region *r)
{
    epoch_world_stop(r->epoch);
}

void ilka_world_resume(struct ilka_region *r)
{
    epoch_world_resume(r->epoch);
}
