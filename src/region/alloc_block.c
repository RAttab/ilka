/* alloc_block.c
   Rémi Attab (remi.attab@gmail.com), 05 Dec 2015
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// struct
// -----------------------------------------------------------------------------

struct ilka_packed alloc_blocks
{
    ilka_off_t head;
    uint64_t tags;
};


// -----------------------------------------------------------------------------
// classes
// -----------------------------------------------------------------------------

static const size_t alloc_block_min_len = 8;
static const size_t alloc_block_mid_inc = 16;
static const size_t alloc_block_mid_len = 256;
static const size_t alloc_block_max_len = 2048;

// [  0,    8] -> 1
// ]  8,  256] -> 16
// ]256, 2048] -> 3
static const size_t alloc_block_classes = 20;

static size_t alloc_block_class(size_t *len)
{
    if (*len <= alloc_block_min_len) {
        *len = alloc_block_min_len;
        return 0;
    }

    // [16, 256] we go by increments of 16 bytes.
    if (*len <= alloc_block_mid_len) {
        size_t class = ceil_div(*len, alloc_block_mid_inc);
        *len = class * alloc_block_mid_inc;
        return class;
    }

    // ]256, 2048] we go by powers of 2.
    *len = ceil_pow2(*len);
    size_t bits = leading_bit(*len) - leading_bit(alloc_block_mid_len);
    return bits + (alloc_block_mid_len / alloc_block_mid_inc) + 1;
}


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static inline size_t alloc_block_area_len()
{
    size_t len = alloc_block_classes * sizeof(struct alloc_blocks);
    return ceil_div(len, ILKA_CACHE_LINE);
}

static size_t alloc_block_len(struct ilka_alloc *alloc)
{
    return alloc_block_area_len() * alloc->areas;
}

static inline ilka_off_t alloc_block_index_off(
        struct ilka_alloc *alloc, size_t area, size_t class)
{
    ilka_assert(area < alloc->areas,
            "invalid area: %lu >= %lu", area, alloc->areas);

    ilka_assert(class < alloc_block_classes,
            "invalid class: %lu >= %lu", class, alloc_block_classes);

    return alloc->blocks_off
        + area * alloc_block_area_len()
        + class * sizeof(struct alloc_blocks);
}

static struct alloc_blocks * alloc_block_write(
        struct ilka_alloc *alloc, size_t area, size_t class)
{
    ilka_off_t off = alloc_block_index_off(alloc, area, class);
    return ilka_write_sys(alloc->region, off, sizeof(struct alloc_blocks));
}


// -----------------------------------------------------------------------------
// tag
// -----------------------------------------------------------------------------

static const size_t alloc_block_tag_bits = 16;

static ilka_off_t alloc_block_tag(struct alloc_blocks *blocks, ilka_off_t off)
{
    ilka_off_t tag = ilka_atomic_fetch_add(&blocks->tags, 1, morder_relaxed);
    return off | (tag << (64 - alloc_block_tag_bits));
}

static ilka_off_t alloc_block_untag(ilka_off_t off)
{
    ilka_off_t mask = ((1UL << alloc_block_tag_bits) - 1) << (64 - alloc_block_tag_bits);
    return off & ~mask;
}


// -----------------------------------------------------------------------------
// allocator
// -----------------------------------------------------------------------------

static ilka_off_t alloc_block_fill(
        struct ilka_alloc *alloc,
        struct alloc_blocks *blocks,
        size_t len,
        size_t area)
{
    const size_t page_size = ILKA_PAGE_SIZE;
    const size_t nodes = page_size / len;
    ilka_assert(nodes >= 2, "inssuficient nodes in page: %lu < 2", nodes);

    ilka_off_t page = alloc_new(alloc, page_size, area);
    if (!page) return 0;

    ilka_off_t start = page;
    ilka_off_t end = start + (nodes * len);

    for (ilka_off_t node = start + len; node + len < end; node += len) {
        ilka_off_t *pnode = ilka_write_sys(alloc->region, node, sizeof(ilka_off_t));
        *pnode = alloc_block_tag(blocks, node + len);
    }

    ilka_off_t *last = ilka_write_sys(alloc->region, end - len, sizeof(ilka_off_t));

    ilka_off_t head = ilka_atomic_load(&blocks->head, morder_acquire);
    do {
        ilka_atomic_store(last, head, morder_relaxed);

        // morder_release: ensure that the link list is committed before
        // publishing it.
    } while(!ilka_atomic_cmp_xchg(&blocks->head, &head, start + len, morder_release));

    return page;
}

static ilka_off_t alloc_block_new(
        struct ilka_alloc *alloc, size_t len, size_t area)
{
    area %= alloc->areas;
    size_t class = alloc_block_class(&len);
    struct alloc_blocks *blocks = alloc_block_write(alloc, area, class);

    ilka_off_t next = 0;
    ilka_off_t head = ilka_atomic_load(&blocks->head, morder_acquire);
    do {
        if (!head) {
            head = alloc_block_fill(alloc, blocks, len, area);
            break;
        }

        const ilka_off_t *node =
            ilka_read_sys(alloc->region, alloc_block_untag(head), sizeof(ilka_off_t));
        next = ilka_atomic_load(node, morder_relaxed);

        // morder_relaxed: allocating doesn't require any writes to the block
        // and everything is fully dependent on the result of the cas op.
    } while (!ilka_atomic_cmp_xchg(&blocks->head, &head, next, morder_relaxed));

    return alloc_block_untag(head);
}

static void alloc_block_free(
        struct ilka_alloc *alloc, ilka_off_t off, size_t len, size_t area)
{
    area %= alloc->areas;
    size_t class = alloc_block_class(&len);
    struct alloc_blocks *blocks = alloc_block_write(alloc, area, class);

    ilka_off_t *node = ilka_write_sys(alloc->region, off, sizeof(ilka_off_t));
    ilka_off_t head = ilka_atomic_load(&blocks->head, morder_relaxed);
    off = alloc_block_tag(blocks, off);

    do {
        ilka_atomic_store(node, head, morder_relaxed);

        // morder_release: make sure the head and any other user write are
        // committed before we make the block available again.
    } while (!ilka_atomic_cmp_xchg(&blocks->head, &head, off, morder_release));
}
