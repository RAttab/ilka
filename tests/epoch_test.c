/* epoch_test.c
   Rémi Attab (remi.attab@gmail.com), 24 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include <stdlib.h>

// -----------------------------------------------------------------------------
// epoch tests
// -----------------------------------------------------------------------------

struct defer_test
{
    struct ilka_region *r;

    size_t n;
    size_t **blocks;

    size_t runs;
};

void defer_fn(void *d)
{
    size_t *data = d;
    ilka_assert(*data, "invalid zero data");

    *data = 0;
    free(data);
}

void run_defer_test(size_t id, void *data)
{
    enum { n = 10, it = 1000 };

    struct defer_test *t = data;

    if ((id % 2) == 0) {
        for (size_t i = 0; i < t->runs; ++i) {
            size_t *new = malloc(sizeof(size_t));
            *new = -1UL;

            // morder_release: commit writes to new before publishing it.
            size_t *old = ilka_atomic_xchg(&t->blocks[i % t->n], new, morder_release);
            if (old) {
                ilka_assert(*old, "invalid zero value");
                ilka_defer(t->r, defer_fn, old);
            }
        }

        for (size_t i = 0; i < t->n; ++i) {
            size_t *old = ilka_atomic_xchg(&t->blocks[i], NULL, morder_relaxed);
            if (old) ilka_defer(t->r, defer_fn, old);
        }
    }
    else {
        bool done;
        do {
            done = true;
            ilka_epoch_t epoch = ilka_enter(t->r);

            for (size_t i = 0; i < t->runs; ++i) {
                size_t *value = ilka_atomic_load(&t->blocks[i % t->n], morder_relaxed);
                if (!value) continue;
                done = false;

                ilka_assert(*value, "invalid zero value: %lu", *value);
            }

            ilka_exit(t->r, epoch);
        } while (!done);
    }
}


START_TEST(epoch_defer_mt)
{
    enum { n = 10 };

    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    size_t *blocks[n] = { 0 };
    blocks[0] = malloc(sizeof(size_t));
    *blocks[0] = -1UL;

    struct defer_test data = {
        .r = r,
        .n = n,
        .blocks = blocks,
        .runs = 100,
    };
    ilka_run_threads(run_defer_test, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, epoch_defer_mt, true);
}

int main(void)
{
    return ilka_tests("epoch_test", &make_suite);
}
