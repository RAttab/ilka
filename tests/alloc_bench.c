/* alloc_bench.c
   Rémi Attab (remi.attab@gmail.com), 12 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "bench.h"
#include <stdlib.h>


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------


struct alloc_bench
{
    struct ilka_region *r;
    size_t len;
};


void run_basic_alloc_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    (void) id;
    struct alloc_bench *t = data;

    ilka_off_t blocks[n];

    ilka_bench_start(b);

    for (size_t i = 0; i < n; ++i)
        blocks[i] = ilka_alloc(t->r, t->len);

    ilka_bench_stop(b);

    for (size_t i = 0; i < n; ++i)
        ilka_free(t->r, blocks[i], t->len);
}

void run_linear_free_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    (void) id;
    struct alloc_bench *t = data;

    ilka_off_t *pages = alloca(n * sizeof(ilka_off_t));

    for (size_t i = 0; i < n; ++i)
        pages[i] = ilka_alloc(t->r, t->len);

    ilka_bench_start(b);

    for (size_t i = 0; i < n; ++i)
        ilka_free(t->r, pages[i], t->len);
}


void run_mixed_free_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    (void) id;
    struct alloc_bench *t = data;

    ilka_off_t *pages = alloca(n * sizeof(ilka_off_t));

    for (size_t i = 0; i < n; ++i)
        pages[i] = ilka_alloc(t->r, t->len);

    ilka_bench_start(b);

    for (size_t i = 0; i < n; i += 2)
        ilka_free(t->r, pages[i], t->len);

    for (size_t i = 1; i < n; i += 2)
        ilka_free(t->r, pages[i], t->len);
}


// -----------------------------------------------------------------------------
// page
// -----------------------------------------------------------------------------

START_TEST(page_basic_alloc_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = ILKA_PAGE_SIZE };
    ilka_bench_st("page_basic_alloc_bench_st", run_basic_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(page_basic_alloc_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = ILKA_PAGE_SIZE };
    ilka_bench_mt("page_basic_alloc_bench_mt", run_basic_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(page_linear_free_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = ILKA_PAGE_SIZE };
    ilka_bench_st("page_linear_free_bench_st", run_linear_free_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(page_linear_free_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = ILKA_PAGE_SIZE };
    ilka_bench_mt("page_linear_free_bench_mt", run_linear_free_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(page_mixed_free_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = ILKA_PAGE_SIZE };
    ilka_bench_st("page_mixed_free_bench_st", run_mixed_free_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(page_mixed_free_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = ILKA_PAGE_SIZE };
    ilka_bench_mt("page_mixed_free_bench_mt", run_mixed_free_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// block
// -----------------------------------------------------------------------------


START_TEST(block_basic_alloc_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_st("block_basic_alloc_bench_st", run_basic_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_basic_alloc_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_mt("block_basic_alloc_bench_mt", run_basic_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_linear_free_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_st("block_linear_free_bench_st", run_linear_free_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_linear_free_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_mt("block_linear_free_bench_mt", run_linear_free_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_mixed_free_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_st("block_mixed_free_bench_st", run_mixed_free_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_mixed_free_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_mt("block_mixed_free_bench_mt", run_mixed_free_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


START_TEST(block_areas_1_alloc_bench_mt)
{
    struct ilka_options options = {
        .open = true,
        .create = true,
        .alloc_areas = 1
    };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_mt("block_areas_1_alloc_bench_mt", run_basic_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_areas_2_alloc_bench_mt)
{
    struct ilka_options options = {
        .open = true,
        .create = true,
        .alloc_areas = 2
    };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_mt("block_areas_2_alloc_bench_mt", run_basic_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_areas_4_alloc_bench_mt)
{
    struct ilka_options options = {
        .open = true,
        .create = true,
        .alloc_areas = 4
    };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_mt("block_areas_4_alloc_bench_mt", run_basic_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_areas_8_alloc_bench_mt)
{
    struct ilka_options options = {
        .open = true,
        .create = true,
        .alloc_areas = 8
    };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_mt("block_areas_8_alloc_bench_mt", run_basic_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_areas_16_alloc_bench_mt)
{
    struct ilka_options options = { .create = true, .alloc_areas = 16 };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_mt("block_areas_16_alloc_bench_mt", run_basic_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, page_basic_alloc_bench_st, true);
    ilka_tc(s, page_basic_alloc_bench_mt, true);
    ilka_tc(s, page_linear_free_bench_st, true);
    ilka_tc(s, page_linear_free_bench_mt, true);
    ilka_tc(s, page_mixed_free_bench_st, true);
    ilka_tc(s, page_mixed_free_bench_mt, true);

    ilka_tc(s, block_basic_alloc_bench_st, true);
    ilka_tc(s, block_basic_alloc_bench_mt, true);
    ilka_tc(s, block_linear_free_bench_st, true);
    ilka_tc(s, block_linear_free_bench_mt, true);
    ilka_tc(s, block_mixed_free_bench_st, true);
    ilka_tc(s, block_mixed_free_bench_mt, true);
    ilka_tc(s, block_areas_1_alloc_bench_mt, true);
    ilka_tc(s, block_areas_2_alloc_bench_mt, true);
    ilka_tc(s, block_areas_4_alloc_bench_mt, true);
    ilka_tc(s, block_areas_8_alloc_bench_mt, true);
    ilka_tc(s, block_areas_16_alloc_bench_mt, true);
}

int main(void)
{
    return ilka_tests("alloc_bench", &make_suite);
}
