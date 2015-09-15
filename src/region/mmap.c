/* mmap.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "utils/atomic.h"

#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>


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
        if (m->anon && munmap(m->anon, m->anon_len) == -1)
            ilka_error_errno("unable to munmap anon");

        m->anon_len = m->reserved;
        m->anon = mmap(NULL, m->anon_len, m->prot, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (m->anon == MAP_FAILED)
            ilka_error_errno("unable to mmap anon: %p", ((void*) m->anon_len));
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
        ilka_error_errno("unable to mmap '%p' at '%p' for length '%p'",
                ptr, (void*) off, (void*) len);
    }

    return ptr;
}

static bool _mmap_remap(
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
    if (munmap(m->anon, diff) == -1) ilka_error_errno("unable to munmap anon");
    m->anon = ((uint8_t *) m->anon) + diff;
    m->anon_len -= diff;

    if (mremap(ptr, old_len, new_len, 0) != MAP_FAILED) return true;
    if (errno == ENOMEM) return false;

    ilka_error_errno("unable to remap '%p' from '%p' to '%p'",
            ptr, (void*) old_len, (void*) new_len);
    return true;
}

static void mmap_init(
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
}

static void mmap_close(struct ilka_mmap *m)
{
    struct mmap_node *node = &m->head;
    do {
        if (munmap(node->ptr, node->len) == -1) {
            ilka_error_errno("unable to unmap '%p' with length '%p'",
                    m->head.ptr, (void *) node->len);
        }

        struct mmap_node *next = node->next;
        if (node != &m->head) free(node);
        node = next;
    } while (node);
}

static void mmap_remap(struct ilka_mmap *m, size_t old, size_t new)
{
    size_t off = 0;
    struct mmap_node *node = &m->head;

    while (node->next) {
        off += node->len;
        node = node->next;
    }
    ilka_assert(off + node->len == old, "inconsistent size: %p + %p != %p",
            (void *) off, (void *) node->len, (void *) old);

    if (_mmap_remap(m, node->ptr, node->len, new - off)) {
        node->len = new - off;
        return;
    }

    off += node->len;

    struct mmap_node *tail = calloc(1, sizeof(struct mmap_node));
    tail->len = new - off;
    tail->ptr = _mmap_map(m, off, tail->len);

    ilka_atomic_store(&node->next, tail, morder_release);
}

static void mmap_coalesce(struct ilka_mmap *m)
{
    if (!m->head.next) return;

    size_t len = 0;
    struct mmap_node *node = &m->head;

    do {
        len += node->len;
        node = node->next;
    } while (node);

    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    uint8_t *ptr = mmap(NULL, len + m->reserved, 0, flags, 0, 0);
    if (ptr == MAP_FAILED) ilka_error_errno("unable to create anonymous mapping");

    if (munmap(m->anon, m->anon_len) == -1) ilka_error_errno("unable to munmap anon");
    m->anon = ((uint8_t*) ptr) + len;
    m->anon_len = m->reserved;

    size_t off = 0;
    node = &m->head;

    do {
        int flags = MREMAP_MAYMOVE | MREMAP_FIXED;
        void *ret = mremap(node->ptr, node->len, node->len, flags, ptr + off);
        if (ret == MAP_FAILED) ilka_error_errno("unable to mremap fixed");

        off += node->len;
        struct mmap_node *next = node->next;
        if (node != &m->head) free(node);
        node = next;
    } while (node);

    m->head = (struct mmap_node) { ptr, len, NULL };
}

static void * mmap_access(struct ilka_mmap *m, ilka_off_t off, size_t len)
{
    ilka_off_t off_rel = off;
    struct mmap_node *node = &m->head;

    while (true) {
        size_t rlen = ilka_atomic_load(&node->len, morder_relaxed);

        if (ilka_likely(off_rel < rlen)) {
            ilka_assert(off_rel + len <= rlen, "invalid cross-map access");
            return ((uint8_t*) node->ptr) + off_rel;
        }

        if (node->next) {
            off_rel -= rlen;
            node = ilka_atomic_load(&node->next, morder_relaxed);
            continue;
        }

        ilka_error("out-of-bounds access: %p + %p", (void *) off, (void *) len);
    }
}
