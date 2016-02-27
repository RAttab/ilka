/* mmap.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// structs
// -----------------------------------------------------------------------------

struct mmap_node
{
    void *ptr;
    size_t len;

    struct mmap_node *next;
};

struct ilka_mmap
{
    int fd;
    int prot, flags;
    size_t reserved;

    void *anon;
    size_t anon_len;

    struct mmap_node head;

    struct mmap_node *vmas;
    struct mmap_node *last_vma;
};


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static void * mmap_map(struct ilka_mmap *m, ilka_off_t off, size_t len)
{
    if (m->anon && munmap(m->anon, m->anon_len) == -1) {
        ilka_fail_errno("unable to munmap anon");
        return NULL;
    }

    m->anon_len = len + m->reserved;
    m->anon = mmap(NULL, m->anon_len, m->prot, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (m->anon == MAP_FAILED) {
        ilka_fail_errno("unable to mmap anon: %p", ((void *) m->anon_len));
        return NULL;
    }

    void *ptr = mmap(m->anon, len, m->prot, m->flags | MAP_FIXED, m->fd, off);
    if (ptr == MAP_FAILED) {
        ilka_fail_errno("unable to mmap '%p' at '%p' for length '%p'",
                ptr, (void *) off, (void *) len);
        return NULL;
    }

    struct mmap_node *vma = calloc(1, sizeof(struct mmap_node));
    *vma = (struct mmap_node) { .ptr = ptr, .len = len };
    if (!m->vmas) m->vmas = vma;
    else m->last_vma->next = vma;
    m->last_vma = vma;

    m->anon_len -= len;
    m->anon = m->anon_len ? ((uint8_t *) m->anon) + len : NULL;

    return ptr;
}

static int mmap_expand(
        struct ilka_mmap *m, void *ptr, size_t old_len, size_t new_len)
{
    size_t diff = new_len - old_len;
    if (diff > m->anon_len) return false;

    // This has a potential race condition where a mapping takes the freed-up
    // anon slot before we can remap. Sadly you can't MREMAP_FIXED without
    // moving the region so there's no good alternative that aren't racy as
    // well.
    //
    // Note that if the vma is snagged before we can grab it, we just fall-back
    // on creating a new vma.

    if (munmap(m->anon, diff) == -1) {
        ilka_fail_errno("unable to munmap anon");
        return -1;
    }
    m->anon_len -= diff;
    m->anon = m->anon_len ? ((uint8_t *) m->anon) + diff : NULL;

    // Since our current node might be a composite of multiple vmas, we only
    // specify the last page of the node and the let the kernel figure out which
    // vma it belongs to and adjust the values accordingly.
    void *last_page = ((uint8_t *) ptr ) + old_len - ILKA_PAGE_SIZE;
    size_t adj_old_len = ILKA_PAGE_SIZE;
    size_t adj_new_len = diff + ILKA_PAGE_SIZE;

    if (mremap(last_page, adj_old_len, adj_new_len, 0) != MAP_FAILED) {
        m->last_vma->len += diff;
        return 1;
    }
    if (errno == ENOMEM) return 0;

    ilka_fail_errno("unable to remap '%p' from '%p' to '%p'",
            ptr, (void*) old_len, (void*) new_len);
    return -1;
}


// -----------------------------------------------------------------------------
// basics
// -----------------------------------------------------------------------------

static bool mmap_init(
        struct ilka_mmap *m, int fd, size_t len, struct ilka_options *options)
{
    memset(m, 0, sizeof(struct ilka_mmap));
    m->fd = fd;

    m->reserved = options->vma_reserved ? options->vma_reserved : 1 << (12 + 10);

    m->prot = PROT_READ;
    if (!options->read_only) m->prot |= PROT_WRITE;

    m->flags = options->type == ilka_public ? MAP_SHARED : MAP_PRIVATE;
    if (options->huge_tlb) m->flags |= MAP_HUGETLB;
    if (options->populate) m->flags |= MAP_POPULATE;

    m->head.len = len;
    m->head.ptr = mmap_map(m, 0, len);

    return m->head.ptr != NULL;
}

static bool mmap_close(struct ilka_mmap *m)
{
    struct mmap_node *node = m->vmas;
    while (node) {
        if (munmap(node->ptr, node->len) == -1) {
            ilka_fail_errno("unable to unmap '%p' with length '%p'",
                    node->ptr, (void *) node->len);
            return false;
        }

        struct mmap_node *next = node->next;
        if (node != &m->head) free(node);
        node = next;
    } while (node);

    node = m->head.next;
    while (node) {
        struct mmap_node *next = node->next;
        free(node);
        node = next;
    }

    return true;
}


// -----------------------------------------------------------------------------
// interface
// -----------------------------------------------------------------------------

static bool mmap_remap(struct ilka_mmap *m, size_t old, size_t new)
{
    size_t off = 0;
    struct mmap_node *node = &m->head;

    while (node->next) {
        off += node->len;
        node = node->next;
    }
    ilka_assert(off + node->len == old, "inconsistent size: %p + %p != %p",
            (void *) off, (void *) node->len, (void *) old);

    int ret = mmap_expand(m, node->ptr, node->len, new - off);
    if (ret == -1) return false;
    if (ret) {
        node->len = new - off;
        return true;
    }

    off += node->len;

    struct mmap_node *tail = calloc(1, sizeof(struct mmap_node));
    if (!tail) {
        ilka_fail("out-of-memory for new remap node");
        return false;
    }

    tail->len = new - off;
    tail->ptr = mmap_map(m, off, tail->len);
    if (!tail->ptr) goto fail;

    ilka_atomic_store(&node->next, tail, morder_release);

    return true;

  fail:
    free(tail);
    return false;
}

static bool mmap_coalesce(struct ilka_mmap *m)
{
    if (!m->head.next) return true;

    size_t len = 0;
    struct mmap_node *node = &m->head;

    do {
        len += node->len;
        node = node->next;
    } while (node);

    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    uint8_t *ptr = mmap(NULL, len + m->reserved, 0, flags, 0, 0);
    if (ptr == MAP_FAILED) {
        ilka_fail_errno("unable to create anonymous mapping");
        return false;
    }

    if (m->anon && munmap(m->anon, m->anon_len) == -1) {
        ilka_fail_errno("unable to munmap anon");
        goto fail;
    }
    m->anon = ((uint8_t *) ptr) + len;
    m->anon_len = m->reserved;

    size_t off = 0;
    node = m->vmas;

    do {
        int flags = MREMAP_MAYMOVE | MREMAP_FIXED;
        void *ret = mremap(node->ptr, node->len, node->len, flags, ptr + off);
        if (ret == MAP_FAILED) {
            // If this fails then something has gone horribly wrong.
            ilka_fail_errno("unable to mremap fixed - can't recover");
            ilka_abort();
        }

        node->ptr = ptr + off;
        off += node->len;
        node = node->next;
    } while (node);

    node = m->head.next;
    while (node) {
        struct mmap_node *next = node->next;
        free(node);
        node = next;
    }
    m->head = (struct mmap_node) { ptr, len, NULL };

    return true;

  fail:
    munmap(ptr, len + m->reserved);
    return false;
}

static void * mmap_access(struct ilka_mmap *m, ilka_off_t off, size_t len)
{
    ilka_off_t off_rel = off;
    struct mmap_node *node = &m->head;

    while (true) {
        size_t rlen = ilka_atomic_load(&node->len, morder_relaxed);

        if (ilka_likely(off_rel < rlen)) {
            ilka_assert(off_rel + len <= rlen,
                    "invalid cross-map access: %p + %p > %p",
                    (void *) off_rel, (void *) len, (void *) rlen);

            return ((uint8_t*) node->ptr) + off_rel;
        }

        struct mmap_node *next = ilka_atomic_load(&node->next, morder_relaxed);
        if (!next) {
            ilka_fail("out-of-bounds access: %p + %p", (void *) off, (void *) len);
            ilka_abort();
        }

        off_rel -= rlen;
        node = next;
    }
}

static bool mmap_is_edge(struct ilka_mmap *m, ilka_off_t off)
{
    struct mmap_node *node = &m->head;

    while (true) {
        size_t rlen = ilka_atomic_load(&node->len, morder_relaxed);
        if (off == rlen) return true;
        if (off <= rlen || !node->next) return false;

        off -= rlen;
        node = ilka_atomic_load(&node->next, morder_relaxed);
    }

    ilka_unreachable();
}
