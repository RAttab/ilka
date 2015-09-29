/* epoch_test.c
   Rémi Attab (remi.attab@gmail.com), 24 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include <stdlib.h>


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

struct epoch_test
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


// -----------------------------------------------------------------------------
// defer tests
// -----------------------------------------------------------------------------

void run_defer_test(size_t id, void *data)
{
    struct epoch_test *t = data;

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

    struct epoch_test data = {
        .r = r,
        .n = n,
        .blocks = blocks,
        .runs = 1000000,
    };
    ilka_run_threads(run_defer_test, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// world test
// -----------------------------------------------------------------------------

void run_world_test(size_t id, void *data)
{
    struct epoch_test *t = data;
    if (!ilka_srand(id + 1)) ilka_abort();

    if ((id % 2) == 0) {
        bool done;
        size_t *exp = alloca(t->n * sizeof(size_t));

        do {
            if (!ilka_nsleep(ilka_rand_range(10, 1000))) ilka_abort();

            ilka_world_stop(t->r);

            done = true;
            for (size_t i = 0; i < t->n; ++i) {
                size_t *val = ilka_atomic_load(&t->blocks[i], morder_relaxed);
                exp[i] = val ? *val : 0;
                done &= !val;
            }

            for (size_t i = 0; i < t->n; ++i) {
                size_t *val = ilka_atomic_load(&t->blocks[i], morder_relaxed);
                if (!exp[i]) ilka_assert(!val, "expected nil value");
                else ilka_assert(*val == exp[i], "unexpected value: %lu != %lu", *val, exp[i]);
            }

            ilka_world_resume(t->r);
        } while (!done);
    }

    else {
        for (size_t i = 0; i < t->runs; ++i) {
            ilka_epoch_t epoch = ilka_enter(t->r);

            size_t i = ilka_rand_range(0, t->n);
            size_t *new = malloc(sizeof(size_t));
            *new = id;


            size_t *old = ilka_atomic_xchg(&t->blocks[i], new, morder_release);
            if (old) {
                ilka_assert(*old, "unexpected zero value");
                ilka_defer(t->r, defer_fn, old);
            }

            ilka_exit(t->r, epoch);
        }

        for (size_t i = 0; i < t->n;  ++i) {
            ilka_epoch_t epoch = ilka_enter(t->r);

            size_t *old = ilka_atomic_xchg(&t->blocks[i], NULL, morder_release);

            if (!old) {
                ilka_assert(*old, "unexpected zero value");
                ilka_defer(t->r, defer_fn, old);
            }

            ilka_exit(t->r, epoch);
        }
    }
}


START_TEST(epoch_world_mt)
{
    enum { n = 10 };

    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    size_t *blocks[n] = { 0 };
    blocks[0] = malloc(sizeof(size_t));
    *blocks[0] = -1UL;

    struct epoch_test data = {
        .r = r,
        .n = n,
        .blocks = blocks,
        .runs = 1000000,
    };
    ilka_run_threads(run_world_test, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, epoch_defer_mt, true);
    ilka_tc(s, epoch_world_mt, false);
}

int main(void)
{
    return ilka_tests("epoch_test", &make_suite);
}
