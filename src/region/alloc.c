/* alloc.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// struct
// -----------------------------------------------------------------------------

struct ilka_alloc
{
    struct ilka_region * region;
    ilka_slock lock;

    ilka_off_t pages_off;
    ilka_off_t blocks_off;

    size_t areas;
};


// -----------------------------------------------------------------------------
// implementation
// -----------------------------------------------------------------------------

static ilka_off_t alloc_new(struct ilka_alloc *alloc, size_t len, size_t area);

#include "alloc_page.c"
#include "alloc_block.c"


// -----------------------------------------------------------------------------
// interface
// -----------------------------------------------------------------------------

static ilka_off_t alloc_end(struct ilka_alloc *alloc)
{
    ilka_off_t off = alloc->blocks_off + alloc_block_len(alloc);
    return ceil_div(off, ILKA_PAGE_SIZE) * ILKA_PAGE_SIZE;
}

static bool alloc_init(
        struct ilka_alloc *alloc,
        struct ilka_region *region,
        struct ilka_options *options,
        ilka_off_t off)
{
    memset(alloc, 0, sizeof(struct ilka_alloc));

    alloc->region = region;
    slock_init(&alloc->lock);

    alloc->pages_off = off + sizeof(uint64_t);
    alloc->blocks_off = alloc->pages_off + sizeof(uint64_t);
    alloc->blocks_off = ceil_div(alloc->blocks_off, ILKA_CACHE_LINE) * ILKA_CACHE_LINE;

    if (options->alloc_areas && !options->create) {
        ilka_fail("alloc_areas option can only be set when creating a region");
        return false;
    }
    alloc->areas = options->alloc_areas ? options->alloc_areas : ilka_cpus();

    const uint64_t *areas = ilka_read_sys(region, off, sizeof(uint64_t));
    if (*areas) alloc->areas = *areas;
    else {
        uint64_t *wareas = ilka_write_sys(region, off, sizeof(uint64_t));
        *wareas = alloc->areas;
    }

    ilka_off_t end_off = alloc_end(alloc);
    if (end_off > ILKA_PAGE_SIZE) {
        ilka_off_t off = ilka_grow(region, end_off - ILKA_PAGE_SIZE);
        if (!off) return false;
        ilka_assert(off != ILKA_PAGE_SIZE,
                "disjointed allocator region detected: %lu != 4096", off);
    }

    return true;
}

static ilka_off_t alloc_new(struct ilka_alloc *alloc, size_t len, size_t area)
{
    if (ilka_likely(len <= alloc_block_max_len))
        return alloc_block_new(alloc, len, area);

    slock_lock(&alloc->lock);
    ilka_off_t result = alloc_page_new(alloc, alloc->pages_off, len);
    slock_unlock(&alloc->lock);
    return result;
}

static void alloc_free(
        struct ilka_alloc *alloc, ilka_off_t off, size_t len, size_t area)
{
    if (ilka_likely(len <= alloc_block_max_len)) {
        alloc_block_free(alloc, off, len, area);
        return;
    }

    slock_lock(&alloc->lock);
    alloc_page_free(alloc, alloc->pages_off, off, len);
    slock_unlock(&alloc->lock);
}
