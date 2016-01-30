/* persist_bench.c
   Rémi Attab (remi.attab@gmail.com), 30 Jan 2016
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "bench.h"


// -----------------------------------------------------------------------------
// bench
// -----------------------------------------------------------------------------

struct marks_bench
{
    struct ilka_region *r;
    const char *title;

    ilka_off_t off;
    size_t len;
};

void run_marks_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    (void) id;
    struct marks_bench *t = data;

    ilka_bench_start(b);

    for (size_t i = 0; i < n; ++i)
        ilka_write(t->r, t->off, t->len);
}

START_TEST(marks_small_bench_st)
{
    enum { len = sizeof(uint64_t) };

    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct marks_bench data = {
        .r = r,
        .off = ilka_alloc(r, len),
        .len = len
    };
    ilka_bench_st("marks_small_bench_st", run_marks_bench, &data);

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
        .off = ilka_alloc(r, len),
        .len = len
    };
    ilka_bench_mt("marks_small_bench_mt", run_marks_bench, &data);

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
        .off = ilka_alloc(r, len),
        .len = len
    };
    ilka_bench_st("marks_large_bench_st", run_marks_bench, &data);

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
        .off = ilka_alloc(r, len),
        .len = len
    };
    ilka_bench_mt("marks_large_bench_mt", run_marks_bench, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST




// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, marks_small_bench_st, true);
    ilka_tc(s, marks_small_bench_mt, true);
    ilka_tc(s, marks_large_bench_st, true);
    ilka_tc(s, marks_large_bench_mt, true);
}

int main(void)
{
    return ilka_tests("persist_bench", &make_suite);
}
