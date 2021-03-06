/* region.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "ilka.h"
#include "utils/utils.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

// Private interface.
static bool ilka_is_edge(struct ilka_region *r, ilka_off_t off);
const void * ilka_read_sys(struct ilka_region *r, ilka_off_t off, size_t len);
void * ilka_write_sys(struct ilka_region *r, ilka_off_t off, size_t len);

#include "file.c"
#include "mmap.c"
#include "alloc.c"
#include "journal.c"
#include "persist.c"
#include "epoch.c"
#include "mcheck.c"


// -----------------------------------------------------------------------------
// config
// -----------------------------------------------------------------------------

static const uint64_t ilka_magic = 0x31906C0FFC1FC856;
static const uint64_t ilka_version = 1;

#ifndef ILKA_MCHECK
# define ILKA_MCHECK 0
#endif

#ifndef ILKA_ALLOC_ZERO
# define ILKA_ALLOC_ZERO 0
#endif

#ifndef ILKA_ALLOC_FILL_ON_FREE
# define ILKA_ALLOC_FILL_ON_FREE 0
#endif

#ifndef ILKA_ALLOC_FILL_ON_ALLOC
# define ILKA_ALLOC_FILL_ON_ALLOC 0
#endif


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

struct ilka_packed meta
{
    uint64_t magic;
    uint64_t version;
    ilka_off_t alloc;
    ilka_off_t root;
};

struct ilka_region
{
    int fd;
    const char* file;
    struct ilka_options options;

    ilka_slock lock;

    size_t len;
    size_t header_len;

    struct ilka_mmap mmap;
    struct ilka_persist persist;
    struct ilka_alloc alloc;
    struct ilka_epoch epoch;

    struct ilka_mcheck mcheck;
};


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

const struct meta * meta_read(struct ilka_region *r)
{
    return ilka_read_sys(r, 0, sizeof(struct meta));
}

struct meta * meta_write(struct ilka_region *r)
{
    return ilka_write_sys(r, 0, sizeof(struct meta));
}

struct ilka_region * ilka_open(const char *file, struct ilka_options *options)
{
    journal_recover(file);

    struct ilka_region *r = calloc(1, sizeof(struct ilka_region));
    if (!r) {
        ilka_fail("out-of-memory for ilka_region struct: %lu",
                sizeof(struct ilka_region));
        return NULL;
    }

    slock_init(&r->lock);

    r->file = file;
    r->options = *options;

    if ((r->fd = file_open(file, &r->options)) == -1) goto fail_open;
    if ((r->len = file_grow(r->fd, ILKA_PAGE_SIZE)) == -1UL) goto fail_grow;
    if (!mmap_init(&r->mmap, r->fd, r->len, &r->options)) goto fail_mmap;
    if (!persist_init(&r->persist, r, r->file)) goto fail_persist;

    const struct meta * meta = meta_read(r);
    if (meta->magic != ilka_magic) {
        if (!r->options.create) {
            ilka_fail("invalid magic for file '%s'", file);
            goto fail_magic;
        }

        struct meta * m = meta_write(r);
        m->magic = ilka_magic;
        m->version = ilka_version;
        m->alloc = sizeof(struct meta);
    }

    if (meta->version != ilka_version) {
        ilka_fail("invalid version for file '%s': %lu != %lu",
                file, meta->version, ilka_version);
        goto fail_version;
    }

    if (!alloc_init(&r->alloc, r, &r->options, meta->alloc)) goto fail_alloc;
    if (!epoch_init(&r->epoch, r, &r->options)) goto fail_epoch;
    if (ILKA_MCHECK) mcheck_init(&r->mcheck);

    r->header_len = alloc_end(&r->alloc);

    return r;

  fail_epoch:
  fail_alloc:
  fail_version:
  fail_magic:
    persist_close(&r->persist);

  fail_persist:
    mmap_close(&r->mmap);

  fail_mmap:
  fail_grow:
    file_close(r->fd);

  fail_open:
    free(r);
    return NULL;
}

bool ilka_close(struct ilka_region *r)
{
    if (!ilka_save(r)) return false;

    epoch_close(&r->epoch);
    persist_close(&r->persist);

    if (!mmap_close(&r->mmap)) return false;
    if (!file_close(r->fd)) return false;
    free(r);

    return true;
}

bool ilka_rm(struct ilka_region *r)
{
    const char *file = r->file;
    if (!ilka_close(r)) return false;
    return file_rm(file);
}


size_t ilka_len(struct ilka_region *r)
{
    return ilka_atomic_load(&r->len, morder_relaxed);
}

ilka_off_t ilka_grow(struct ilka_region *r, size_t len)
{
    len = ceil_div(len, ILKA_PAGE_SIZE) * ILKA_PAGE_SIZE;

    slock_lock(&r->lock);

    size_t old_len = r->len;
    size_t new_len = old_len + len;

    file_grow(r->fd, new_len);
    if (!mmap_remap(&r->mmap, old_len, new_len)) goto fail_remap;

    // morder_release: ensure that the region is fully grown before publishing
    // the new size.
    ilka_atomic_store(&r->len, new_len, morder_release);

    slock_unlock(&r->lock);
    return old_len;

  fail_remap:
    slock_unlock(&r->lock);
    return 0;
}

ilka_off_t ilka_get_root(struct ilka_region *r)
{
    return meta_read(r)->root;
}

void ilka_set_root(struct ilka_region *r, ilka_off_t root)
{
    meta_write(r)->root = root;
}

static bool ilka_is_edge(struct ilka_region *r, ilka_off_t off)
{
    return mmap_is_edge(&r->mmap, off);
}

const void * ilka_read_sys(struct ilka_region *r, ilka_off_t off, size_t len)
{
    return mmap_access(&r->mmap, off, len);
}

void * ilka_write_sys(struct ilka_region *r, ilka_off_t off, size_t len)
{
    void *ptr = mmap_access(&r->mmap, off, len);
    if (ptr) persist_mark(&r->persist, off, len);
    return ptr;
}

const void * ilka_read(struct ilka_region *r, ilka_off_t off, size_t len)
{
    mcheck_tag_t tag = ILKA_MCHECK ? mcheck_untag(&off) : 0;
    ilka_assert(off >= r->header_len, "invalid read offset: %p", (void *) off);
    if (ILKA_MCHECK) mcheck_access(&r->mcheck, off, len, tag);

    return mmap_access(&r->mmap, off, len);
}

void * ilka_write(struct ilka_region *r, ilka_off_t off, size_t len)
{
    mcheck_tag_t tag = ILKA_MCHECK ? mcheck_untag(&off) : 0;
    ilka_assert(off >= r->header_len, "invalid write offset: %p", (void *) off);
    if (ILKA_MCHECK) mcheck_access(&r->mcheck, off, len, tag);

    void *ptr = mmap_access(&r->mmap, off, len);
    if (ptr) persist_mark(&r->persist, off, len);
    return ptr;
}

bool ilka_save(struct ilka_region *r)
{
    return persist_save(&r->persist);
}

ilka_off_t ilka_alloc(struct ilka_region *r, size_t len)
{
    return ilka_alloc_in(r, len, ilka_tid());
}

ilka_off_t ilka_alloc_in(struct ilka_region *r, size_t len, size_t area)
{
    ilka_off_t off = alloc_new(&r->alloc, len, area);

    ilka_assert(off + len <= ilka_len(r), "invalid alloc offset: %p", (void *) off);
    ilka_assert(!off || off >= r->header_len, "invalid alloc offset: %p", (void *) off);

    if (ILKA_MCHECK) {
        mcheck_tag_t tag = mcheck_tag_next();
        mcheck_alloc(&r->mcheck, off, len, tag);
        off = mcheck_tag(off, tag);
    }

    if (ILKA_ALLOC_ZERO && off)
        memset(ilka_write(r, off, len), 0, len);

    if (ILKA_ALLOC_FILL_ON_ALLOC && off)
        memset(ilka_write(r, off, len), 0xFF, len);

    return off;
}

void ilka_free(struct ilka_region *r, ilka_off_t off, size_t len)
{
    ilka_free_in(r, off, len, ilka_tid());
}

void ilka_free_in(struct ilka_region *r, ilka_off_t off, size_t len, size_t area)
{
    mcheck_tag_t tag = ILKA_MCHECK ? mcheck_untag(&off) : 0;

    ilka_assert(off + len <= ilka_len(r), "invalid free offset: %p", (void *) off);
    ilka_assert(off >= r->header_len, "invalid free offset: %p", (void *) off);

    if (ILKA_ALLOC_FILL_ON_FREE)
        memset(ilka_write(r, off, len), 0xFF, len);

    if (ILKA_MCHECK) mcheck_free(&r->mcheck, off, len, tag);

    alloc_free(&r->alloc, off, len, area);
}

bool ilka_defer_free(struct ilka_region *r, ilka_off_t off, size_t len)
{
    return ilka_defer_free_in(r, off, len, ilka_tid());
}

bool ilka_defer_free_in(
        struct ilka_region *r, ilka_off_t off, size_t len, size_t area)
{
    return epoch_defer_free(&r->epoch, off, len, area);
}

bool ilka_enter(struct ilka_region *r)
{
    return epoch_enter(&r->epoch);
}

void ilka_exit(struct ilka_region *r)
{
    epoch_exit(&r->epoch);
}

bool ilka_defer(struct ilka_region *r, void (*fn) (void *), void *data)
{
    return epoch_defer(&r->epoch, fn, data);
}

void ilka_world_stop(struct ilka_region *r)
{
    epoch_world_stop(&r->epoch);
    mmap_coalesce(&r->mmap);
}

void ilka_world_resume(struct ilka_region *r)
{
    epoch_world_resume(&r->epoch);
}
