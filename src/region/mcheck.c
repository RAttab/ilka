/* mcheck.c
   Rémi Attab (remi.attab@gmail.com), 17 Nov 2015
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// config
// -----------------------------------------------------------------------------

static const size_t mcheck_max_size = 1UL << 32;

static const uint8_t mcheck_header = 1 << 0;
static const uint8_t mcheck_allocated = 1 << 1;

#ifndef ILKA_MCHECK_ACCESS
# define ILKA_MCHECK_ACCESS 0
#endif


// -----------------------------------------------------------------------------
// mcheck
// -----------------------------------------------------------------------------

struct ilka_mcheck
{
    bool disabled;

    uint8_t *flags;
    void *blocks;
};

static __thread bool mcheck_in_alloc = false;

static void *mcheck_mmap()
{
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;

    void *area = mmap(0, mcheck_max_size, prot, flags, 0, 0);
    if (!area) {
        ilka_fail_errno("unable to mmap mcheck area");
        ilka_abort();
    }

    return area;
}

static void mcheck_init(struct ilka_mcheck *mcheck)
{
    memset(mcheck, 0, sizeof(struct ilka_mcheck));
    mcheck->flags = mcheck_mmap();
    mcheck->blocks = mcheck_mmap();
}

static void mcheck_close(struct ilka_mcheck *mcheck)
{
    munmap(mcheck->flags, mcheck_max_size);
    munmap(mcheck->blocks, mcheck_max_size);
}

static void mcheck_disable(struct ilka_mcheck *mcheck)
{
    mcheck->disabled = true;
}


static inline size_t * mcheck_block(struct ilka_mcheck *mcheck, ilka_off_t off)
{
    return (size_t *) (((uint8_t *) mcheck->blocks) + off);
}

static void mcheck_alloc_begin(struct ilka_mcheck *mcheck)
{
    (void) mcheck;
    mcheck_in_alloc = true;
}

static void mcheck_alloc_end(
        struct ilka_mcheck *mcheck, ilka_off_t off, size_t len)
{
    mcheck_in_alloc = false;

    if (ILKA_MCHECK_ACCESS) {
        bool ok = true;
        for (size_t i = off; i < off + len; ++i) ok = ok && (!mcheck->flags[i]);
        ilka_assert(ok, "memory re-allocation detected: %p + %p", (void *) off, (void *) len);

        memset(mcheck->flags + off, mcheck_allocated, len);
        mcheck->flags[off] |= mcheck_header;
    }
    else mcheck->flags[off] = mcheck_header | mcheck_allocated;

    *mcheck_block(mcheck, off) = len;
}

static void mcheck_free_begin(
        struct ilka_mcheck *mcheck, ilka_off_t off, size_t len)
{
    ilka_assert(mcheck->flags[off] == (mcheck_allocated | mcheck_header),
            "mid-object free detected: %p + %p", (void *) off, (void *) len);

    if (ILKA_MCHECK_ACCESS) {
        bool ok = true;
        for (size_t i = off + 1; i < off + len; ++i)
            ok = ok && (mcheck->flags[i] == mcheck_allocated);
        ilka_assert(ok, "invalid free detected: %p + %p", (void *) off, (void *) len);
    }

    size_t exp_len = *mcheck_block(mcheck, off);
    ilka_assert(len == exp_len, "unexpected free len: %p + %p != %p",
            (void *) off, (void *) len, (void *) exp_len);

    if (ILKA_MCHECK_ACCESS) memset(mcheck->flags + off, 0, len);
    else mcheck->flags[off] = 0;

    mcheck_in_alloc = true;
}

static void mcheck_free_end(struct ilka_mcheck *mcheck)
{
    (void) mcheck;
    mcheck_in_alloc = false;
}

static void mcheck_access(struct ilka_mcheck *mcheck, ilka_off_t off, size_t len)
{
    if (!ILKA_MCHECK_ACCESS || mcheck_in_alloc || mcheck->disabled) return;

    size_t i = off;
    if (mcheck->flags[i] == (mcheck_header | mcheck_allocated)) i++;

    bool ok = true;
    for (; i < off + len; ++i) ok = ok && (mcheck->flags[i] == mcheck_allocated);
    if (!ok) {
        for (size_t i = off; i < off + len; ++i) {
            ilka_log("mcheck.access.err", "%p:%p = %c%c",
                    (void *) (i - off), (void *) i,
                    mcheck->flags[i] & mcheck_header ? 'h' : '.',
                    mcheck->flags[i] & mcheck_allocated ? 'a' : '.');
        }
        ilka_assert(ok, "invalid access detected: %p + %p", (void *) off, (void *) len);
    }

}
