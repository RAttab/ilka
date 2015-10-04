/* persist_test.c
   Rémi Attab (remi.attab@gmail.com), 02 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"


// -----------------------------------------------------------------------------
// persist test
// -----------------------------------------------------------------------------

struct basics_test
{
    const char *file;
    struct ilka_region *r;

    size_t runs;
    size_t threads;

    uint64_t done;
};

void run_basics_test(size_t id, void *data)
{
    struct basics_test *t = data;
    enum {
        n = ILKA_PAGE_SIZE,
        m = sizeof(uint64_t),
    };

    if (!ilka_srand(id + 1)) ilka_abort();

    if (id) {
        ilka_off_t page;
        {
            ilka_epoch_t e = ilka_enter(t->r);

            page = ilka_alloc(t->r, n);

            size_t off = ilka_get_root(t->r) + id * sizeof(ilka_off_t);
            ilka_off_t *root = ilka_write(t->r, off, sizeof(ilka_off_t));
            ilka_atomic_store(root, page, morder_relaxed);

            memset(ilka_write(t->r, page, n), 0, n);

            ilka_exit(t->r, e);
        }

        uint64_t count = 0;
        do {
            ilka_epoch_t e = ilka_enter(t->r);

            for (size_t i = 0; i < n / m; ++i) {
                uint64_t *p = ilka_write(t->r, page + (i * m), m);
                *p = count + (id << 32);;
            }

            count++;
            ilka_exit(t->r, e);
        } while (!ilka_atomic_load(&t->done, morder_relaxed));

    }
    else {
        for (size_t run = 0; run < t->runs; ++run) {
            ilka_nsleep(ilka_rand_range(100, 10000));

            ilka_logt("test.save");
            if (!ilka_save(t->r)) ilka_abort();

            ilka_logt("test.open");
            struct ilka_options options = { .open = true, .read_only = true };
            struct ilka_region *r = ilka_open(t->file, &options);

            ilka_off_t root = ilka_get_root(r);

            const ilka_off_t *roots = ilka_read(r, root, t->threads * sizeof(ilka_off_t));

            for (size_t i = 0; i < t->threads; ++i) {
                if (!roots[i]) continue;

                ilka_log("test.read", "i=%lu", i);

                const uint64_t *page = ilka_read(r, roots[i], n);
                for (size_t j = 1; j < n / m; ++j) {
                    ilka_assert(page[0] == page[j],
                            "inconsistent value: [%lu, %d], %p != %p",
                            j, n / m, (void *) page[0], (void *) page[j]);
                }
            }

            ilka_logt("test.close");
            if (!ilka_close(r)) ilka_abort();
        }

        ilka_atomic_store(&t->done, 1, morder_release);
    }
}

START_TEST(basics_test_mt)
{
    enum { threads = 128 };

    const char *file = "blah";

    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open(file, &options);

    size_t n = threads * sizeof(ilka_off_t);
    ilka_off_t root = ilka_alloc(r, n);
    memset(ilka_write(r, root, n), 0, n);
    ilka_set_root(r, root);

    struct basics_test data = {
        .r = r,
        .file = file,
        .runs = 10,
        .threads = threads,
    };
    ilka_run_threads(run_basics_test, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, basics_test_mt, true);
}

int main(void)
{
    return ilka_tests("persist_test", &make_suite);
}
