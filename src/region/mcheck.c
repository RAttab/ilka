/* mcheck.c
   Rémi Attab (remi.attab@gmail.com), 22 Nov 2015
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// config
// -----------------------------------------------------------------------------

static const size_t mcheck_max_len = 1UL << 32;


// -----------------------------------------------------------------------------
// mcheck
// -----------------------------------------------------------------------------

struct ilka_mcheck
{
    uint8_t *region;
    void *blocks;
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

    mcheck->backtrace = mmap(0, mcheck_max_len, prot, flags, -1, 0);
}

static inline void mcheck_check(
        struct ilka_mcheck *mcheck,
        ilka_off_t off,
        size_t len,
        uint8_t value,
        const char *msg)
{
    bool ok = true;
    for (size_t i = off; i < off + len; ++i)
        ok = ok && mcheck->region[i] == value;
    if (ilka_likely(ok)) return;

    size_t blen = len * 16 + 128;
    char *buf = alloca(blen);

    size_t bpos = snprintf(buf, blen, "mcheck error (%p, %p): %s\n",
            (void *) off, (void *) len, msg);

    for (size_t i = 0; i < len; ++i) {
        bpos += snprintf(buf + bpos, blen - bpos, "  %p:%d\n",
                (void *) (off + i), (int) mcheck->region[i]);
    }

    ilka_fail("%s", buf);
    ilka_abort();
}

static inline void mcheck_set(
        struct ilka_mcheck *mcheck, ilka_off_t off, size_t len, uint8_t value)
{
    memset(mcheck->region + off, value, len);
    ilka_atomic_fence(morder_release);
}

static void mcheck_alloc(struct ilka_mcheck *mcheck, ilka_off_t off, size_t len)
{
    mcheck_check(mcheck, off, len, 0, "double-allocation");
    mcheck_set(mcheck, off, len, 1);
}

static void mcheck_free(struct ilka_mcheck *mcheck, ilka_off_t off, size_t len)
{
    mcheck_check(mcheck, off, len, 1, "double-free");
    mcheck_set(mcheck, off, len, 0);
}

static void mcheck_access(struct ilka_mcheck *mcheck, ilka_off_t off, size_t len)
{
    mcheck_check(mcheck, off, len, 1, "access-after-free");
}
