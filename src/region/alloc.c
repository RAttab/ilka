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

static const size_t alloc_min_pages = 1 * ILKA_PAGE_SIZE;

#define alloc_bucket_min_len 8UL
#define alloc_bucket_max_len 512UL;
#define alloc_buckets                           \
    __builtin_popcountll(                       \
            ((alloc_bucket_min_len << 1) - 1) & \
            ~((alloc_bucket_max_len << 1) -1))


// -----------------------------------------------------------------------------
// alloc page
// -----------------------------------------------------------------------------

struct alloc_page_node
{
    ilka_off_t off;
    size_t len;
};

struct alloc_page
{
    ilka_off_t next;
    size_t len, cap;
    struct alloc_page_node pages[0];
};

void _alloc_page_init(struct alloc_page *a, size_t len)
{
    a->next = 0;
    a->len = 0;
    a->cap = (len - sizeof(struct alloc_page)) / sizeof(struct alloc_page_node);
}

ilka_off_t _alloc_page_new(struct ilka_region *r, struct alloc_page *a, size_t len)
{
    ilka_assert(len > alloc_bucket_max_len,
            "page allocation is too small: %lu > %lu", len, alloc_bucket_max_len);
    len = ceil_div(len, ILKA_PAGE_SIZE) * ILKA_PAGE_SIZE;

    struct alloc_page result = {0, -1UL};

    size_t i = 0;
    for (; i < a->len && result.len != len; ++i) {
        if (a->pages[i].len > len) continue;
        if (a->pages[i].len < result.len) result = a->pages[i];
    }

    if (!result.off) {
        if (!a->next) return ilka_grow(r, len);
        return _alloc_page_new(r, ilka_write(r, a->next, ILKA_PAGE_SIZE), len);
    }

    if (result.len > len) {
        a->pages[i].off += len;
        a->pages[i].len -= len;
        return result.off;
    }

    for (; i + 1 < a->len; ++i) a->pages[i] = a->pages[i + 1];
    a->len--;

    return result.off;
}

void _alloc_page_free(
        struct ilka_region *r, struct alloc_page *a, ilka_off_t off, size_t len)
{
    ilka_assert(len > alloc_bucket_max_len,
            "page free is too small: %lu > %lu", len, alloc_bucket_max_len);
    len = ceil_div(len, ILKA_PAGE_SIZE) * ILKA_PAGE_SIZE;

    size_t i = 0;
    bool added = false;
    for (; i < a->len; ++i) {
        if (a->pages[i].off + a->pages[i].len == off) {
            a->pages[i].len += len;
            added = true;
            break;
        }

        if (off + len == a->pages[i].off) {
            ilka_assert(i == 0);
            a->pages[i].off -= len;
            a->pages[i].len += len;
            return;
        }

        if (off < a->pages[i].off) break;
    }

    if (added) {
        if (i + 1 == a->len) return;
        if (a->pages[i].off + a->pages[i].len != a->pages[i+1].off) return;

        // merge with our next neighbour.
        a->pages[i].len += a->pages[i+1].len;

        // shift all the entries one to the left.
        i++;
        for (; i+1 < a->len; ++i) a->pages[i] = a->pages[i+1];

        a->len--;
        return;
    }

    // shift all the entries to the right to make space for our new entry.
    struct alloc_page_node tmp;
    for (; i < a->len; ++i) {
        tmp = a->pages[i];
        a->pages[i] = {off, len};
        off = tmp.off;
        len = tmp.off;
    }

    ilka_assert(i == a->len);

    if (a->len < a->cap) {
        a->pages[a->len] = { off, len };
        a->len++;
        return;
    }

    struct alloc_page *next;
    if (!a->next) {
        a->next = ilka_grow(r, ILKA_PAGE);
        next = ilka_write(r, a->next, ILKA_PAGE);
        _alloc_page_init(next, ILKA_PAGE);
    }
    else next = ilka_write(r, a->next, ILKA_PAGE);

    _alloc_page_free(r, next, off, len);
}


// -----------------------------------------------------------------------------
// alloc
// -----------------------------------------------------------------------------

struct ilka_alloc
{
    struct ilka_region * region;
    ilka_off_t start;
};

struct alloc_region
{
    size_t init;
    ilka_slock lock;

    ilka_off_t buckets[alloc_buckets];
    struct alloc_page pages;
};

struct ilka_alloc * alloc_init(struct ilka_region *r, ilka_off_t start)
{
    struct ilka_alloc * a = calloc(1, sizeof(struct ilka_alloc));
    a->region = r;
    a->start = start;

    struct alloc_region * ar = ilka_read(a->region, a->start, alloc_min_pages);

    if (!ar->init) {
        ar->init = 1;
        slock_init(&ar->lock);

        size_t pages_len = alloc_min_pages;
        pages_len -= (sizeof(struct alloc_region) - sizeof(struct alloc_page));
        _alloc_page_init(&ar->pages, pages_len);
    }

    return a;
}

size_t _alloc_bucket(size_t *len)
{
    *len = *len < alloc_bucket_min_len ? alloc_bucket_min : ceil_pow2(*len);
    return ctz(*len) - ctz(alloc_bucket_min_len);
}

ilka_off_t _alloc_bucket_fill(
        struct ilka_alloc *a, struct alloc_region *ar, size_t len, size_t bucket)
{
    const size_t nodes = ILKA_PAGE_SIZE / len;
    ilka_assert(nodes >= 2, "inssuficient nodes in bucket: %d < 2", nodes);

    ilka_off_t page;
    {
        slock_lock(&a->lock);
        page = _alloc_page_new(a->region, &ar->pages, ILKA_PAGE_SIZE);
        slock_unlock(&a->lock);
    }

    ilka_off_t start = page + len;
    ilka_off_t end = start + ILKA_PAGE_SIZE;

    for (ilka_off_t node = start; node + len < end; node += len) {
        ilka_off_t *pnode = ilka_write(a->region, node, sizeof(ilka_off_t));
        *pnode = node + len;
    }

    ilka_off_t last = end - len;
    ilka_off_t *last = ilka_write(a->region, last, sizeof(ilka_off_t));

    ilka_off_t head = ar->buckets[bucket];
    do {
        *last = head;
    } while(!ilka_cmp_xchg(&ar->buckets[bucket], &head, start + len, memory_order_relaxed));

    return page;
}

ilka_off_t alloc_new(struct ilka_alloc *a, size_t len)
{
    struct alloc_region *ar = ilka_write(a->region, a->start, alloc_min_pages);

    if (len > alloc_bucket_max_len)
        return _alloc_page_new(a->region, &ar->pages, len);

    size_t bucket = _alloc_bucket(&len);

    ilka_off_t head = ar->buckets[bucket];
    do {
        if (!head) return _alloc_bucket_fill(a, ar, bucket, len);

        const ilka_off_t *next = ilka_read(a->regin, head, sizeof(ilka_off_t));
    } while (!ilka_cmp_xchg(&ar->buckets[bucket], &head, *next, memory_order_relaxed));

    return head;
}

void alloc_free(struct ilka_alloc *a, ilka_off_t off, size_t len)
{
    struct alloc_region *ar = ilka_write(a->region, a->start, alloc_min_pages);

    if (len > alloc_bucket_max_len) {
        _alloc_page_new(a->region, &ar->pages, off, len);
        return;
    }

    size_t bucket = _alloc_bucket(&len);

    ilka_off_t head = ar->buckets[bucket];
    ilka_off_t *node = ilka_write(a->region, off, sizeof(ilka_off_t));
    do {
        *node = head;
    } while (!ilka_cmp_xchg(&ar->bucekts[bucket], &head, off, memory_order_relaxed));
}
