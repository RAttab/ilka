/* mcheck.c
   Rémi Attab (remi.attab@gmail.com), 22 Nov 2015
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// config
// -----------------------------------------------------------------------------

#if ILKA_MCHECK_TAG_BITS > 7
# error ILKA_MCHECK_TAG_BITS > 7
#endif

static const size_t mcheck_max_len = 1UL << 32;
static const size_t mcheck_tag_bits = ILKA_MCHECK_TAG_BITS;


// -----------------------------------------------------------------------------
// tags
// -----------------------------------------------------------------------------

typedef uint8_t mcheck_tag_t;

static inline mcheck_tag_t mcheck_tag_next()
{
    static size_t tags = 0;

    mcheck_tag_t tag = ilka_atomic_fetch_add(&tags, 1, morder_relaxed);
    return tag  & ((1UL << mcheck_tag_bits) - 1);
}

static inline ilka_off_t mcheck_tag(ilka_off_t off, mcheck_tag_t tag)
{
    return off | ((ilka_off_t) tag) << (64 - mcheck_tag_bits);
}

static inline mcheck_tag_t mcheck_untag(ilka_off_t *off)
{
    mcheck_tag_t tag = *off >> (64 - mcheck_tag_bits);
    *off &= (1UL << (64 - mcheck_tag_bits)) - 1;
    return tag;
}


// -----------------------------------------------------------------------------
// mcheck
// -----------------------------------------------------------------------------

struct ilka_mcheck {
    uint8_t *region;
};

static void mcheck_init(struct ilka_mcheck *mcheck)
{
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    mcheck->region = mmap(0, mcheck_max_len, prot, flags, -1, 0);
    if (mcheck->region == MAP_FAILED) {
        ilka_fail_errno("unable mmap mcheck");
        ilka_abort();
    }
}

static inline void mcheck_check(
        struct ilka_mcheck *mcheck,
        ilka_off_t off,
        size_t len,
        mcheck_tag_t tag,
        const char *msg)
{
    ilka_assert(off + len < mcheck_max_len,
            "mcheck access outside of max len: %p + %p >= %p",
            (void *) off, (void *) len, (void *) mcheck_max_len);

    bool ok = true;
    for (size_t i = off; i < off + len; ++i)
        ok = ok && mcheck->region[i] == tag;
    if (ilka_likely(ok)) return;

    size_t blen = len * 16 + 128;
    char *buf = alloca(blen);

    size_t bpos = snprintf(buf, blen, "mcheck error (%p, %p, %d): %s\n",
            (void *) off, (void *) len, (int) (tag - 1), msg);

    for (size_t i = off; i < off + len; ++i) {
        bpos += snprintf(buf + bpos, blen - bpos, "  %p:%d\n",
                (void *) i, (int) mcheck->region[i]);
    }

    ilka_fail("%s", buf);
    ilka_abort();
}

static inline void mcheck_set(
        struct ilka_mcheck *mcheck, ilka_off_t off, size_t len, mcheck_tag_t tag)
{
    memset(mcheck->region + off, tag, len);
    ilka_atomic_fence(morder_release);
}

static void mcheck_alloc(
        struct ilka_mcheck *mcheck, ilka_off_t off, size_t len, mcheck_tag_t tag)
{
    mcheck_check(mcheck, off, len, 0, "double-allocation");
    mcheck_set(mcheck, off, len, tag + 1);
}

static void mcheck_free(
        struct ilka_mcheck *mcheck, ilka_off_t off, size_t len, mcheck_tag_t tag)
{
    mcheck_check(mcheck, off, len, tag + 1, "double-free");
    mcheck_set(mcheck, off, len, 0);
}

static void mcheck_access(
        struct ilka_mcheck *mcheck, ilka_off_t off, size_t len, mcheck_tag_t tag)
{
    mcheck_check(mcheck, off, len, tag + 1, "access-after-free");
}
