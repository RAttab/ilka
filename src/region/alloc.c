/* alloc.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "region.h"
#include "utils/arch.h"
#include "utils/atomic.h"

// -----------------------------------------------------------------------------
// config
// -----------------------------------------------------------------------------

static size_t alloc_min_pages = 1 * ILKA_PAGE_SIZE;


// -----------------------------------------------------------------------------
// alloc
// -----------------------------------------------------------------------------

struct ilka_alloc
{
    struct ilka_region * region;
    ilka_ptr start;
};

struct alloc_region
{
    size_t init;
    ilka_ptr current;
};

struct ilka_alloc * alloc_init(struct ilka_region *r, ilka_ptr start)
{
    struct ilka_alloc * a = malloc(sizeof(struct ilka_alloc));
    memset(a, 0, sizeof(struct ilka_alloc));

    a->region = r;
    a->start = start;

    struct alloc_region * ar =
        ilka_read(a->region, a->start, sizeof(struct alloc_region));

    if (!ar->init) {
        ar->init = 1;
        ar->current = ar->start + ILKA_PAGE_SIZE;
    }

    return a;
}

ilka_ptr alloc_new(struct ilka_alloc *a, size_t len)
{
    struct alloc_region * ar =
        ilka_read(a->region, a->start, sizeof(struct alloc_region));

    ilka_ptr ptr = ilka_atomic_fetch_add(ar->current, len, memory_order_relaxed);
    ilka_grow(a->region, ptr + len);
    return ptr;
}

void alloc_free(struct ilka_alloc *a, ilka_ptr ptr, size_t len)
{
    (void) a;
    (void) ptr;
    (void) len;

    // noop for current implementation.
}
