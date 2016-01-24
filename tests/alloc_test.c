/* alloc_test.c
   Rémi Attab (remi.attab@gmail.com), 12 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "bench.h"
#include <stdlib.h>

// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

struct alloc_node
{
    ilka_off_t off;
    size_t len;
};

inline void fill_block(
        struct ilka_region *r, ilka_off_t off, size_t len, size_t value)
{
    size_t *data = ilka_write(r, off, len);
    for (size_t i = 0; i * sizeof(size_t) + sizeof(size_t) <= len; ++i)
        data[i] = value;
}

inline void check_block(struct ilka_region *r, ilka_off_t off, size_t len)
{
    const size_t *data = ilka_read(r, off, len);
    size_t value = data[0];
    for (size_t i = 1; i * sizeof(size_t) + sizeof(size_t) < len; ++i) {
        ilka_assert(data[i] == (value), "wrong value at %p: %p != %p",
                (void *) off, (void *) (value), (void *) data[i]);
    }
}

struct alloc_test
{
    struct ilka_region *r;
    size_t blocks;
    size_t allocs;
    size_t mul;
};

void run_alloc_test(size_t id, void *data)
{
    struct alloc_test *t = data;

    ilka_srand(id + 1);
    struct alloc_node *nodes = calloc(t->blocks, sizeof(struct alloc_node));

    for (size_t alloc = 0; alloc < t->allocs; ++alloc) {
        size_t i = ilka_rand_range(0, t->blocks);

        if (nodes[i].off) {
            check_block(t->r, nodes[i].off, nodes[i].len);
            ilka_free(t->r, nodes[i].off, nodes[i].len);
            nodes[i] = (struct alloc_node) {0};
        }
        else {
            size_t max = ilka_rand_range(2, 128);
            nodes[i].len = ilka_rand_range(1, max) * t->mul;
            nodes[i].off = ilka_alloc(t->r, nodes[i].len);
            fill_block(t->r, nodes[i].off, nodes[i].len, alloc);
        }
    }

    for (size_t i = 0; i < t->blocks; ++i) {
        if (!nodes[i].off) continue;
        check_block(t->r, nodes[i].off, nodes[i].len);
        ilka_free(t->r, nodes[i].off, nodes[i].len);
    }

    free(nodes);
}

struct alloc_bench
{
    struct ilka_region *r;
    size_t len;
};


void run_cold_alloc_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    (void) id;
    struct alloc_bench *t = data;

    ilka_bench_start(b);

    for (size_t i = 0; i < n; ++i)
        ilka_alloc(t->r, t->len);
}

void run_warm_alloc_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    (void) id;
    struct alloc_bench *t = data;

    // warm-up
    {
        ilka_off_t *pages = alloca(n * sizeof(ilka_off_t));

        for (size_t i = 0; i < n; ++i)
            pages[i] = ilka_alloc(t->r, t->len);

        for (size_t i = 0; i < n; ++i)
            ilka_free(t->r, pages[i], t->len);
    }

    ilka_bench_start(b);

    for (size_t i = 0; i < n; ++i)
        ilka_alloc(t->r, t->len);
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
// page tests
// -----------------------------------------------------------------------------

START_TEST(page_test_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_test tdata = {
        .r = r,
        .blocks = 20,
        .allocs = 20 * 100,
        .mul = ILKA_PAGE_SIZE,
    };
    run_alloc_test(0, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(page_test_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_test tdata = {
        .r = r,
        .blocks = 5,
        .allocs = 5 * 100,
        .mul = ILKA_PAGE_SIZE,
    };
    ilka_run_threads(run_alloc_test, &tdata, 0);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(page_cold_alloc_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r,  .len = ILKA_PAGE_SIZE};
    ilka_bench_st("page_cold_alloc_bench_st", run_cold_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(page_cold_alloc_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = ILKA_PAGE_SIZE };
    ilka_bench_mt("page_cold_alloc_bench_mt", run_cold_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(page_warm_alloc_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = ILKA_PAGE_SIZE };
    ilka_bench_st("page_warm_alloc_bench_st", run_warm_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(page_warm_alloc_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = ILKA_PAGE_SIZE };
    ilka_bench_mt("page_warm_alloc_bench_mt", run_warm_alloc_bench, &tdata);

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
// block tests
// -----------------------------------------------------------------------------

START_TEST(block_test_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_test tdata = {
        .r = r,
        .blocks = 100,
        .allocs = 100 * 100,
        .mul = 1,
    };
    run_alloc_test(0, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_test_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_test tdata = {
        .r = r,
        .blocks = 100,
        .allocs = 100 * 100,
        .mul = 1,
    };
    ilka_run_threads(run_alloc_test, &tdata, 0);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_cold_alloc_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_st("block_cold_alloc_bench_st", run_cold_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_cold_alloc_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_mt("block_cold_alloc_bench_mt", run_cold_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_warm_alloc_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_st("block_warm_alloc_bench_st", run_warm_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_warm_alloc_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_mt("block_warm_alloc_bench_mt", run_warm_alloc_bench, &tdata);

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
    ilka_bench_mt("block_areas_1_alloc_bench_mt", run_warm_alloc_bench, &tdata);

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
    ilka_bench_mt("block_areas_2_alloc_bench_mt", run_warm_alloc_bench, &tdata);

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
    ilka_bench_mt("block_areas_4_alloc_bench_mt", run_warm_alloc_bench, &tdata);

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
    ilka_bench_mt("block_areas_8_alloc_bench_mt", run_warm_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(block_areas_16_alloc_bench_mt)
{
    struct ilka_options options = { .create = true, .alloc_areas = 16 };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = { .r = r, .len = sizeof(uint64_t) };
    ilka_bench_mt("block_areas_16_alloc_bench_mt", run_warm_alloc_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST



// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, page_test_st, true);
    ilka_tc(s, page_test_mt, true);
    ilka_tc(s, page_cold_alloc_bench_st, true);
    ilka_tc(s, page_cold_alloc_bench_mt, true);
    ilka_tc(s, page_warm_alloc_bench_st, true);
    ilka_tc(s, page_warm_alloc_bench_mt, true);
    ilka_tc(s, page_linear_free_bench_st, true);
    ilka_tc(s, page_linear_free_bench_mt, true);
    ilka_tc(s, page_mixed_free_bench_st, true);
    ilka_tc(s, page_mixed_free_bench_mt, true);

    ilka_tc(s, block_test_st, true);
    ilka_tc(s, block_test_mt, true);
    ilka_tc(s, block_cold_alloc_bench_st, true);
    ilka_tc(s, block_cold_alloc_bench_mt, true);
    ilka_tc(s, block_warm_alloc_bench_st, true);
    ilka_tc(s, block_warm_alloc_bench_mt, true);
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
    return ilka_tests("alloc_test", &make_suite);
}
