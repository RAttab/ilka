/* mmap.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// mmap
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
};

static void * _mmap_map(struct ilka_mmap *m, ilka_off_t off, size_t len)
{
    if (m->anon_len < m->reserved) {
        if (m->anon && munmap(m->anon, m->anon_len) == -1) {
            ilka_fail_errno("unable to munmap anon");
            return NULL;
        }

        m->anon_len = m->reserved;
        m->anon = mmap(NULL, m->anon_len, m->prot, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (m->anon == MAP_FAILED) {
            ilka_fail_errno("unable to mmap anon: %p", ((void*) m->anon_len));
            return NULL;
        }
    }

    void *ptr;
    if (len > m->anon_len)
        ptr = mmap(NULL, len, m->prot, m->flags, m->fd, off);
    else {
        ptr = mmap(m->anon, len, m->prot, m->flags | MAP_FIXED, m->fd, off);
        m->anon = ((uint8_t*) m->anon) + len;
        m->anon_len -= len;
    }

    if (ptr == MAP_FAILED) {
        ilka_fail_errno("unable to mmap '%p' at '%p' for length '%p'",
                ptr, (void*) off, (void*) len);
        return NULL;
    }

    return ptr;
}

static int _mmap_remap(
        struct ilka_mmap *m, void *ptr, size_t old_len, size_t new_len)
{
    if (new_len > m->anon_len) return false;

    // This has a potential race condition where a mapping takes the freed-up
    // anon slot before we can remap. Sadly you can't MREMAP_FIXED without
    // moving the region so there's no good alternative that aren't racy as
    // well.
    //
    // Note that if the vma is snagged before we can grab it, we just fall-back
    // on moving the entire region.

    size_t diff = new_len - old_len;
    if (munmap(m->anon, diff) == -1) {
        ilka_fail_errno("unable to munmap anon");
        return -1;
    }
    m->anon = ((uint8_t *) m->anon) + diff;
    m->anon_len -= diff;

    if (mremap(ptr, old_len, new_len, 0) != MAP_FAILED) return 1;
    if (errno == ENOMEM) return 0;

    ilka_fail_errno("unable to remap '%p' from '%p' to '%p'",
            ptr, (void*) old_len, (void*) new_len);
    return -1;
}

static bool mmap_init(
        struct ilka_mmap *m, int fd, size_t len, struct ilka_options *options)
{
    memset(m, 0, sizeof(struct ilka_mmap));
    m->fd = fd;

    m->reserved = options->vma_reserved ? options->vma_reserved : 1 << (12 + 10);

    m->prot = PROT_READ;
    if (!options->read_only) m->prot |= PROT_WRITE;

    m->flags = MAP_PRIVATE;
    if (options->huge_tlb) m->flags |= MAP_HUGETLB;
    if (options->populate) m->flags |= MAP_POPULATE;

    m->head.len = len;
    m->head.ptr = _mmap_map(m, 0, len);

    return m->head.ptr != NULL;
}

static bool mmap_close(struct ilka_mmap *m)
{
    struct mmap_node *node = &m->head;
    do {
        if (munmap(node->ptr, node->len) == -1) {
            ilka_fail_errno("unable to unmap '%p' with length '%p'",
                    node->ptr, (void *) node->len);
            return false;
        }

        struct mmap_node *next = node->next;
        if (node != &m->head) free(node);
        node = next;
    } while (node);

    return true;
}

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

    int ret = _mmap_remap(m, node->ptr, node->len, new - off);
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
    tail->ptr = _mmap_map(m, off, tail->len);
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

    if (munmap(m->anon, m->anon_len) == -1) {
        ilka_fail_errno("unable to munmap anon");
        goto fail;
    }
    m->anon = ((uint8_t*) ptr) + len;
    m->anon_len = m->reserved;

    size_t off = 0;
    node = &m->head;

    do {
        int flags = MREMAP_MAYMOVE | MREMAP_FIXED;
        void *ret = mremap(node->ptr, node->len, node->len, flags, ptr + off);
        ilka_assert(ret != MAP_FAILED, "unable to mremap fixed - can't recover");

        off += node->len;
        struct mmap_node *next = node->next;
        if (node != &m->head) free(node);
        node = next;
    } while (node);

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

        if (node->next) {
            off_rel -= rlen;
            node = ilka_atomic_load(&node->next, morder_relaxed);
            continue;
        }

        ilka_fail("out-of-bounds access: %p + %p", (void *) off, (void *) len);
        ilka_abort();
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
