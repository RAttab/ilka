/* persist.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// persist
// -----------------------------------------------------------------------------

#define marks_min_len   ILKA_CACHE_LINE
#define marks_block_len ILKA_CACHE_LINE
#define marks_block_bits (marks_block_len * 8)

#define marks_trunc_bits ctz(marks_min_len)
#define marks_low_bits   ctz(marks_block_bits)
#define marks_high_bits  (64 - marks_low_bits - marks_trunc_bits)

#define marks_bits  (marks_high_bits * marks_block_bits)
#define marks_words (marks_bits / 64)

struct ilka_persist
{
    struct ilka_region *region;
    const char *file;

    uint64_t *marks;
    ilka_slock lock;
};

static bool persist_init(
        struct ilka_persist *p, struct ilka_region *r, const char *file)
{
    memset(p, 0, sizeof(struct ilka_persist));

    p->region = r;
    p->file = file;
    slock_init(&p->lock);
    p->marks = calloc(marks_words, sizeof(uint64_t));

    return true;
}

static void persist_close(struct ilka_persist *p)
{
    if (p->marks) free(p->marks);
}

static void persist_mark(struct ilka_persist *p, ilka_off_t off, size_t len)
{
    ilka_off_t end = off + len;
    end >>= marks_trunc_bits;
    off >>= marks_trunc_bits;

    do {
        size_t msb = clz(off);
        msb = msb < 64 ? 63 - msb : 0;

        size_t low, high, len;

        if (msb < marks_low_bits) {
            high = 0;
            low = off;
            len = marks_min_len;
        }
        else {
            high = (msb - marks_low_bits) + 1;
            low = (off >> (high - 1)) & (marks_low_bits - 1);
            len = marks_min_len << high;
        }

        size_t i = high * marks_block_bits + low;
        ilka_atomic_fetch_or(&p->marks[i / 64], 1UL << (i % 64), morder_relaxed);

        off += (len >> marks_trunc_bits);
    } while (off < end);
}

static void _persist_save_journal(struct ilka_persist *p, uint64_t *marks)
{
    struct ilka_journal j;
    if (!journal_init(&j, p->region, p->file)) ilka_abort();

    for (size_t i = bitfields_next(marks, 0, marks_bits);
         i < marks_bits;
         i = bitfields_next(marks, i + 1, marks_bits))
    {
        size_t len = marks_min_len;
        size_t off = i % marks_block_bits;
        size_t high = i / marks_block_bits;

        if (high) {
            len <<= high - 1;
            off = (off | marks_low_bits) << high;
        }

        off <<= marks_trunc_bits;

        if (!journal_add(&j, off, len)) ilka_abort();
    }

    if (!journal_finish(&j)) ilka_abort();
}

static bool _persist_wait(pid_t pid)
{
    int status;
    do {
        if (waitpid(pid, &status, WUNTRACED) == -1) {
            ilka_fail_errno("unable to wait on persist process: %d", pid);
            return false;
        }
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    if (WIFEXITED(status)) {
        if (!WEXITSTATUS(status)) return true;

        ilka_fail("persist process returned error: %d", WEXITSTATUS(status));
        return false;
    }
    else if (WIFSIGNALED(status)) {
        ilka_fail("persist process signaled: %d", WTERMSIG(status));
        return false;
    }

    return true;
}

static bool persist_save(struct ilka_persist *p)
{
    uint64_t *old_marks;
    uint64_t *new_marks = calloc(marks_words, sizeof(uint64_t));

    slock_lock(&p->lock);

    pid_t pid;
    {
        ilka_world_stop(p->region);

        pid = fork();

        old_marks = p->marks;
        p->marks = new_marks;

        ilka_world_resume(p->region);
    }

    if (pid == -1) {
        ilka_fail_errno("unable to fork for persist");
        slock_unlock(&p->lock);
        return false;
    }


    if (!pid) {
        _persist_save_journal(p, old_marks);
        _exit(0);
    }
    else {
        free(old_marks);
        bool ret = _persist_wait(pid);

        slock_unlock(&p->lock);
        return ret;
    }
}
