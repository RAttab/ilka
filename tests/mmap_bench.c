/* mmap_bench.c
   Rémi Attab (remi.attab@gmail.com), 30 Jan 2016
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "bench.h"


// -----------------------------------------------------------------------------
// access bench
// -----------------------------------------------------------------------------

struct access_bench
{
    struct ilka_region *r;
    ilka_off_t off;
};

void run_access_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    (void) id;
    struct access_bench *t = data;

    ilka_bench_start(b);

    for (size_t i = 0; i < n; ++i)
        ilka_read(t->r, t->off, sizeof(uint64_t));
}

enum type { st, mt };

void access_bench_runner(size_t i, enum type type)
{
    struct ilka_options options = {
        .open = true,
        .create = true,
        .vma_reserved = ILKA_PAGE_SIZE
    };
    struct ilka_region *r = ilka_open("blah", &options);

    ilka_off_t pages[32];

    for (size_t i = 0; i < 32; ++i)
        pages[i] = ilka_grow(r, 2 * ILKA_PAGE_SIZE);

    char title[256];
    struct access_bench data = { .r = r };

    data.off = pages[i - 1];

    switch (type) {
    case st:
        snprintf(title, sizeof(title), "access_%lu_bench_st", i);
        ilka_bench_st(title, run_access_bench, &data);
        break;
    case mt:
        snprintf(title, sizeof(title), "access_%lu_bench_mt", i);
        ilka_bench_mt(title, run_access_bench, &data);
        break;
    }

    if (!ilka_close(r)) ilka_abort();
}

START_TEST(access_bench_1_st) { access_bench_runner(1, st); } END_TEST
START_TEST(access_bench_2_st) { access_bench_runner(2, st); } END_TEST
START_TEST(access_bench_4_st) { access_bench_runner(4, st); } END_TEST
START_TEST(access_bench_8_st) { access_bench_runner(8, st); } END_TEST
START_TEST(access_bench_16_st) { access_bench_runner(16, st); } END_TEST
START_TEST(access_bench_32_st) { access_bench_runner(32, st); } END_TEST

START_TEST(access_bench_1_mt) { access_bench_runner(1, mt); } END_TEST
START_TEST(access_bench_2_mt) { access_bench_runner(2, mt); } END_TEST
START_TEST(access_bench_4_mt) { access_bench_runner(4, mt); } END_TEST
START_TEST(access_bench_8_mt) { access_bench_runner(8, mt); } END_TEST
START_TEST(access_bench_16_mt) { access_bench_runner(16, mt); } END_TEST
START_TEST(access_bench_32_mt) { access_bench_runner(32, mt); } END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, access_bench_1_st, true);
    ilka_tc(s, access_bench_1_mt, true);
    ilka_tc(s, access_bench_2_st, true);
    ilka_tc(s, access_bench_2_mt, true);
    ilka_tc(s, access_bench_4_st, true);
    ilka_tc(s, access_bench_4_mt, true);
    ilka_tc(s, access_bench_8_st, true);
    ilka_tc(s, access_bench_8_mt, true);
    ilka_tc(s, access_bench_16_st, true);
    ilka_tc(s, access_bench_16_mt, true);
    ilka_tc(s, access_bench_32_st, true);
    ilka_tc(s, access_bench_32_mt, true);
}

int main(void)
{
    return ilka_tests("mmap_bench", &make_suite);
}
