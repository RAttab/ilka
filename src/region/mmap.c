/* mmap.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include <sys/mman.h>
#include <errno.h>

// -----------------------------------------------------------------------------
// mmap
// -----------------------------------------------------------------------------

void * mmap_map(int fd, size_t len, struct ilka_options *options)
{
    int prot = PROT_READ;
    if (options->writable) prot |= PROT_WRITE;

    int flags = MAP_PRIVATE;
    if (options->huge_tlb) flags |= MAP_HUGETLB;
    if (options->populate) flags |= MAP_POPULATE;

    void * start = mmap(NULL, len, prot, flags, fd, 0);
    if (start == MAP_FAILED) {
        ilka_error_errno("unable to mmap fd '%d' with length '%lu'", fd, len);
    }

    return start;
}

void mmap_unmap(void *start, size_t len)
{
    if (munmap(start, len) == -1) {
        ilka_error_errno("unable to unmap '%p' with length '%lu'", start, len);
    }
}

bool mmap_remap_soft(void *start, size_t old, size_t new)
{
    void * ret = mremap(start, old, new, 0);
    if (ret != MAP_FAILED) return true;
    if (errno == ENOMEM) return false;

    ilka_error_errno("unable to soft remap '%p' from '%lu' to '%lu'",
            start, old, new);
}

void * mmap_remap_hard(void *start, size_t old, size_t new)
{
    void * ret = mremap(start, old, new, MREMAP_MAYMOVE);
    if (ret == MAP_FAILED) {
        ilka_error_errno("unable to hard remap '%p' from '%lu' to '%lu'",
                start, old, new);
    }

    return ret;
}
