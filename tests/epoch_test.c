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
    const char *title;
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

void run_basics_test(size_t id, void *data)
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
            if (!ilka_enter(t->r)) ilka_abort();

            for (size_t i = 0; i < t->runs; ++i) {
                size_t *value = ilka_atomic_load(&t->blocks[i % t->n], morder_relaxed);
                if (!value) continue;
                done = false;

                ilka_assert(*value, "invalid zero value: %lu", *value);
            }

            ilka_exit(t->r);
        } while (!done);
    }
}


START_TEST(basics_test_mt)
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
    ilka_run_threads(run_basics_test, &data);

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
            if (!ilka_enter(t->r)) ilka_abort();

            size_t i = ilka_rand_range(0, t->n);
            size_t *new = malloc(sizeof(size_t));
            *new = id;


            size_t *old = ilka_atomic_xchg(&t->blocks[i], new, morder_release);
            if (old) {
                ilka_assert(*old, "unexpected zero value");
                ilka_defer(t->r, defer_fn, old);
            }

            ilka_exit(t->r);
        }

        for (size_t i = 0; i < t->n;  ++i) {
            if (!ilka_enter(t->r)) ilka_abort();

            size_t *old = ilka_atomic_xchg(&t->blocks[i], NULL, morder_release);

            if (!old) {
                ilka_assert(*old, "unexpected zero value");
                ilka_defer(t->r, defer_fn, old);
            }

            ilka_exit(t->r);
        }
    }
}


START_TEST(world_test_mt)
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
// benches
// -----------------------------------------------------------------------------

void run_enter_exit_bench(size_t id, void *data)
{
    struct epoch_test *t = data;

    struct timespec t0 = ilka_now();
    {
        for (size_t i = 0; i < t->n; ++i) {
            ilka_enter(t->r);
            ilka_exit(t->r);
        }
    }
    double elapsed = ilka_elapsed(&t0);

    if (!id) ilka_print_bench(t->title, t->n, elapsed);
}

START_TEST(enter_exit_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct epoch_test data = {
        .title = "enter_exit_bench_st",
        .r = r,
        .n = 10000000
    };
    run_enter_exit_bench(0, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(enter_exit_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct epoch_test data = {
        .title = "enter_exit_bench_mt",
        .r = r,
        .n = 1000000
    };
    ilka_run_threads(run_enter_exit_bench, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, basics_test_mt, true);
    ilka_tc(s, world_test_mt, true);
    ilka_tc(s, enter_exit_bench_st, true);
    ilka_tc(s, enter_exit_bench_mt, true);
}

int main(void)
{
    return ilka_tests("epoch_test", &make_suite);
}
