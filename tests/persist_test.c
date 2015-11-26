/* persist_test.c
   Rémi Attab (remi.attab@gmail.com), 02 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"


// -----------------------------------------------------------------------------
// marks
// -----------------------------------------------------------------------------

START_TEST(marks_test_st)
{
    const char *file = "blah";
    const size_t max_len = 1UL << 20;

    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open(file, &options);

    ilka_off_t root;
    {
        size_t n = max_len + ILKA_CACHE_LINE;
        root = ilka_alloc(r, n);
        memset(ilka_write(r, root, n), 0, n);
        ilka_save(r);
    }

    uint8_t c = 0;
    for (size_t n = 1; n < max_len; n *= 8) {
        for (size_t j = 0; j < 3; ++j) {
            size_t off = root + j;

            memset(ilka_write(r, off, n), ++c, n);
            if (!ilka_save(r)) ilka_abort();

            struct ilka_options options = { .open = true, .read_only = true };
            struct ilka_region *tr = ilka_open(file, &options);

            const uint8_t *p = ilka_read(tr, off, n);
            for (size_t i = 0; i < n; ++i) {
                ilka_assert(p[i] == c,
                        "unexpected value (%lu != %lu): i=%lu, n=%lu, j=%lu, off=%p",
                        (size_t) p[i], (size_t) c, i, n, j, (void *) off);
            }

            if (!ilka_close(tr)) ilka_abort();
        }
    }

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

struct marks_bench
{
    struct ilka_region *r;
    const char *title;
    size_t runs;

    ilka_off_t off;
    size_t len;
};

void run_marks_bench(size_t id, void *data)
{
    struct marks_bench *t = data;

    struct timespec t0 = ilka_now();
    {
        for (size_t i = 0; i < t->runs; ++i)
            ilka_write(t->r, t->off, t->len);
    }
    double elapsed = ilka_elapsed(&t0);

    if (!id) ilka_print_bench(t->title, t->runs, elapsed);
}

START_TEST(marks_small_bench_st)
{
    enum { len = sizeof(uint64_t) };

    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct marks_bench data = {
        .r = r,
        .title = "marks_small_bench_st",
        .runs = 10000,
        .off = ilka_alloc(r, len),
        .len = len
    };

    run_marks_bench(0, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(marks_small_bench_mt)
{
    enum { len = sizeof(uint64_t) };

    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct marks_bench data = {
        .r = r,
        .title = "marks_small_bench_mt",
        .runs = 10000,
        .off = ilka_alloc(r, len),
        .len = len
    };

    ilka_run_threads(run_marks_bench, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


START_TEST(marks_large_bench_st)
{
    enum { len = ILKA_PAGE_SIZE };

    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct marks_bench data = {
        .r = r,
        .title = "marks_large_bench_st",
        .runs = 10000,
        .off = ilka_alloc(r, len),
        .len = len
    };

    run_marks_bench(0, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(marks_large_bench_mt)
{
    enum { len = ILKA_PAGE_SIZE };

    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct marks_bench data = {
        .r = r,
        .title = "marks_large_bench_mt",
        .runs = 10000,
        .off = ilka_alloc(r, len),
        .len = len
    };

    ilka_run_threads(run_marks_bench, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// save
// -----------------------------------------------------------------------------

struct save_test
{
    const char *file;
    struct ilka_region *r;

    size_t runs;
    size_t threads;

    uint64_t done;
};

void run_save_test(size_t id, void *data)
{
    struct save_test *t = data;
    enum {
        n = ILKA_PAGE_SIZE,
        m = sizeof(uint64_t),
    };

    if (!ilka_srand(id + 1)) ilka_abort();

    if (id) {
        ilka_off_t page;
        {
            if (!ilka_enter(t->r)) ilka_abort();

            page = ilka_alloc(t->r, n);

            size_t off = ilka_get_root(t->r) + id * sizeof(ilka_off_t);
            ilka_off_t *root = ilka_write(t->r, off, sizeof(ilka_off_t));
            ilka_atomic_store(root, page, morder_release);

            memset(ilka_write(t->r, page, n), 0, n);

            ilka_exit(t->r);
        }

        uint64_t count = 0;
        do {
            if (!ilka_enter(t->r)) ilka_abort();

            for (size_t i = 0; i < n / m; ++i) {
                uint64_t *p = ilka_write(t->r, page + (i * m), m);
                *p = count + (id << 32);;
            }

            count++;
            ilka_exit(t->r);
        } while (!ilka_atomic_load(&t->done, morder_relaxed));

    }
    else {
        for (size_t run = 0; run < t->runs; ++run) {
            if (!ilka_save(t->r)) ilka_abort();

            struct ilka_options options = { .open = true, .read_only = true };
            struct ilka_region *r = ilka_open(t->file, &options);

            ilka_off_t root = ilka_get_root(r);

            const ilka_off_t *roots = ilka_read(r, root, t->threads * sizeof(ilka_off_t));

            for (size_t i = 0; i < t->threads; ++i) {
                if (!roots[i]) continue;

                const uint64_t *page = ilka_read(r, roots[i], n);
                for (size_t j = 1; j < n / m; ++j) {
                    ilka_assert(page[0] == page[j],
                            "inconsistent value: [%lu, %d], %p != %p",
                            j, n / m, (void *) page[0], (void *) page[j]);
                }
            }

            if (!ilka_close(r)) ilka_abort();
        }

        ilka_atomic_store(&t->done, 1, morder_release);
    }
}

START_TEST(save_test_mt)
{
    enum { threads = 128 };

    const char *file = "blah";

    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open(file, &options);

    size_t n = threads * sizeof(ilka_off_t);
    ilka_off_t root = ilka_alloc(r, n);
    memset(ilka_write(r, root, n), 0, n);
    ilka_set_root(r, root);

    struct save_test data = {
        .r = r,
        .file = file,
        .runs = 10,
        .threads = threads,
    };
    ilka_run_threads(run_save_test, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, marks_test_st, true);
    ilka_tc(s, marks_small_bench_st, true);
    ilka_tc(s, marks_small_bench_mt, true);
    ilka_tc(s, marks_large_bench_st, true);
    ilka_tc(s, marks_large_bench_mt, true);

    ilka_tc(s, save_test_mt, true);
}

int main(void)
{
    return ilka_tests("persist_test", &make_suite);
}
