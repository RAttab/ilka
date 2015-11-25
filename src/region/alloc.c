/* alloc.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// config
// -----------------------------------------------------------------------------

#define alloc_min_pages 1

#define alloc_bucket_min_len sizeof(uint64_t)
#define alloc_bucket_max_len 2048UL
#define alloc_buckets                           \
    __builtin_popcountll(                       \
            ((alloc_bucket_max_len << 1) - 1) & \
            ~((alloc_bucket_min_len << 1) -1))


// -----------------------------------------------------------------------------
// alloc page
// -----------------------------------------------------------------------------

struct alloc_page_node
{
    ilka_off_t next;
    ilka_off_t off;
    size_t len;
};

static ilka_off_t _alloc_page_new(
        struct ilka_region *r, ilka_off_t prev_off, size_t len)
{
    ilka_assert(len > alloc_bucket_max_len,
            "page allocation is too small: %lu > %lu", len, alloc_bucket_max_len);
    len = ceil_div(len, ILKA_PAGE_SIZE) * ILKA_PAGE_SIZE;

    const ilka_off_t *prev = ilka_read_sys(r, prev_off, sizeof(ilka_off_t));
    ilka_off_t node_off = *prev;

    while (node_off) {
        const struct alloc_page_node *node =
            ilka_read_sys(r, node_off, sizeof(struct alloc_page_node));

        if (node->len < len) {
            prev_off = node_off;
            node_off = node->next;
            continue;
        }

        if (node->len == len) {
            ilka_off_t *wprev = ilka_write_sys(r, prev_off, sizeof(ilka_off_t));
            *wprev = node->next;
            return node->off;
        }

        if (node->len > len) {
            struct alloc_page_node *wnode =
                ilka_write_sys(r, node_off, sizeof(struct alloc_page_node));

            wnode->len -= len;
            return wnode->off + wnode->len;
        }

        ilka_unreachable();
    }

    return ilka_grow(r, len);
}

static void _alloc_page_free(
        struct ilka_region *r, ilka_off_t prev_off, ilka_off_t off, size_t len)
{
    ilka_assert(len > alloc_bucket_max_len,
            "page free is too small: %lu > %lu", len, alloc_bucket_max_len);
    len = ceil_div(len, ILKA_PAGE_SIZE) * ILKA_PAGE_SIZE;

    const ilka_off_t *prev = ilka_read_sys(r, prev_off, sizeof(ilka_off_t));
    ilka_off_t node_off = *prev;

    while (node_off) {
        const struct alloc_page_node *node =
            ilka_read_sys(r, node_off, sizeof(struct alloc_page_node));

        if (off + len == node->off && !ilka_is_edge(r, node->off)) {
            struct alloc_page_node *wnode =
                ilka_write_sys(r, off, sizeof(struct alloc_page_node));
            *wnode = (struct alloc_page_node) {
                .next = node->next,
                .off = off,
                .len = node->len + len,
            };

            ilka_off_t *wprev = ilka_write_sys(r, prev_off, sizeof(ilka_off_t));
            *wprev = off;

            return;
        }

        if (node->off + node->len == off && !ilka_is_edge(r, off)) {
            struct alloc_page_node *wnode =
                ilka_write_sys(r, node_off, sizeof(struct alloc_page_node));
            wnode->len += len;

            if (wnode->next) {
                const struct alloc_page_node *next =
                    ilka_read_sys(r, wnode->next, sizeof(struct alloc_page_node));

                if (wnode->off + wnode->len == next->off && !ilka_is_edge(r, next->off)) {
                    wnode->len += next->len;
                    wnode->next = next->next;
                }
            }

            return;
        }

        if (off > node->off) {
            prev_off = node_off;
            node_off = node->next;
            continue;
        }

        break;
    }

    ilka_off_t *wprev = ilka_write_sys(r, prev_off, sizeof(ilka_off_t));

    struct alloc_page_node *node =
        ilka_write_sys(r, off, sizeof(struct alloc_page_node));
    *node = (struct alloc_page_node) {*wprev, off, len};

    *wprev = off;
}


// -----------------------------------------------------------------------------
// alloc
// -----------------------------------------------------------------------------

struct ilka_alloc
{
    struct ilka_region * region;
    ilka_off_t start;
    ilka_slock lock;
};

struct ilka_packed alloc_bucket
{
    // Used to generate a 16 bit tag for bucket pointers which reduce the
    // likelihood of running into ABA problems when updating head.
    uint64_t tags;

    // Head of the bucket's free-list.
    ilka_off_t head;

    // Align to nearest cache line to avoid false sharing issues.
    uint64_t padding[6];
};

struct ilka_packed alloc_region
{
    ilka_off_t pages; // Must be first entry.
    struct alloc_bucket buckets[alloc_buckets];
};

static bool alloc_init(
        struct ilka_alloc *a, struct ilka_region *r, ilka_off_t start)
{
    memset(a, 0, sizeof(struct ilka_alloc));

    a->region = r;
    a->start = start;
    slock_init(&a->lock);

    return true;
}

static const size_t alloc_tag_bits = 16;

static ilka_off_t _alloc_tag(struct alloc_bucket *bucket, ilka_off_t off)
{
    ilka_off_t tag = ilka_atomic_fetch_add(&bucket->tags, 1, morder_relaxed);
    return off | (tag << (64 - alloc_tag_bits));
}

static ilka_off_t _alloc_untag(ilka_off_t off)
{
    ilka_off_t mask = ((1UL << alloc_tag_bits) - 1) << (64 - alloc_tag_bits);
    return off & ~mask;
}

static struct alloc_bucket * _alloc_bucket(struct alloc_region *ar, size_t *len)
{
    ilka_assert(*len <= ceil_pow2(*len), "unexpected bucket shrink");
    *len = *len < alloc_bucket_min_len ? alloc_bucket_min_len : ceil_pow2(*len);
    return &ar->buckets[ctz(*len) - ctz(alloc_bucket_min_len)];
}

static ilka_off_t _alloc_bucket_fill(
        struct ilka_alloc *a, struct alloc_bucket *bucket, size_t len)
{
    const size_t page_size = ILKA_PAGE_SIZE;
    const size_t nodes = page_size / len;
    ilka_assert(nodes >= 2, "inssuficient nodes in bucket: %lu < 2", nodes);

    ilka_off_t page;
    {
        slock_lock(&a->lock);
        page = _alloc_page_new(a->region, a->start, page_size);
        slock_unlock(&a->lock);
    }

    if (!page) return 0;

    ilka_off_t start = page;
    ilka_off_t end = start + (nodes * len);

    for (ilka_off_t node = start + len; node + len < end; node += len) {
        ilka_off_t *pnode = ilka_write_sys(a->region, node, sizeof(ilka_off_t));
        *pnode = _alloc_tag(bucket, node + len);
    }

    ilka_off_t *last = ilka_write_sys(a->region, end - len, sizeof(ilka_off_t));

    ilka_off_t head = ilka_atomic_load(&bucket->head, morder_relaxed);
    do {
        ilka_atomic_store(last, head, morder_relaxed);

        // morder_release: ensure that the link list is committed before
        // publishing it.
    } while(!ilka_atomic_cmp_xchg(&bucket->head, &head, start + len, morder_release));

    return page;
}

static ilka_off_t alloc_new(struct ilka_alloc *a, size_t len)
{
    if (len > alloc_bucket_max_len) {
        slock_lock(&a->lock);
        ilka_off_t result = _alloc_page_new(a->region, a->start, len);
        slock_unlock(&a->lock);
        return result;
    }

    struct alloc_region *ar =
        ilka_write_sys(a->region, a->start, sizeof(struct alloc_region));

    struct alloc_bucket *bucket = _alloc_bucket(ar, &len);

    ilka_off_t next = 0;
    ilka_off_t head = ilka_atomic_load(&bucket->head, morder_relaxed);
    do {
        if (!head) {
            head = _alloc_bucket_fill(a, bucket, len);
            next = 0;
            break;
        }

        const ilka_off_t *node =
            ilka_read_sys(a->region, _alloc_untag(head), sizeof(ilka_off_t));
        next = ilka_atomic_load(node, morder_relaxed);

        // morder_relaxed: allocating doesn't require any writes to the block
        // and everything is fully dependent on the result of the cas op.
    } while (!ilka_atomic_cmp_xchg(&bucket->head, &head, next, morder_relaxed));

    return _alloc_untag(head);
}

static void alloc_free(struct ilka_alloc *a, ilka_off_t off, size_t len)
{
    if (len > alloc_bucket_max_len) {
        slock_lock(&a->lock);
        _alloc_page_free(a->region, a->start, off, len);
        slock_unlock(&a->lock);
        return;
    }

    struct alloc_region *ar =
        ilka_write_sys(a->region, a->start, sizeof(struct alloc_region));

    struct alloc_bucket *bucket = _alloc_bucket(ar, &len);

    ilka_off_t *node = ilka_write_sys(a->region, off, sizeof(ilka_off_t));
    ilka_off_t head = ilka_atomic_load(&bucket->head, morder_relaxed);
    off = _alloc_tag(bucket, off);

    do {
        ilka_atomic_store(node, head, morder_relaxed);

        // morder_release: make sure the head and any other user write are
        // committed before we make the block available again.
    } while (!ilka_atomic_cmp_xchg(&bucket->head, &head, off, morder_release));
}
