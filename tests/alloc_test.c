/* alloc_test.c
   Rémi Attab (remi.attab@gmail.com), 12 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include <stdlib.h>

// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

struct alloc_node
{
    ilka_off_t off;
    size_t len;
};

inline size_t cap_len(size_t len)
{
    if (len >= ILKA_PAGE_SIZE) return len;
    if (len < sizeof(uint64_t)) return sizeof(uint64_t);
    return ceil_pow2(len);
}

inline void fill_block(
        struct ilka_region *r, ilka_off_t off, size_t len, size_t value)
{
    len = cap_len(len);
    size_t *data = ilka_write(r, off, len);
    for (size_t i = 0; i < len / sizeof(size_t); ++i) data[i] = value;
}

#define check_block(r, off, len)                                        \
    do {                                                                \
        size_t len_ = cap_len(len);                                     \
        const size_t *data = ilka_read(r, off, len_);                   \
        size_t value = data[0];                                         \
        for (size_t i = 1; i < len_ / sizeof(size_t); ++i) {            \
            ilka_assert(data[i] == value,                               \
                    "wrong value in page: %lu != %lu", value, data[i]); \
        }                                                               \
    } while(false)

struct alloc_test
{
    struct ilka_region *r;
    size_t blocks;
    size_t allocs;
    size_t mul;
};

void run_alloc_test(size_t id, void *data)
{
    struct alloc_test *t = (struct alloc_test *) data;

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
    const char *title;
    size_t n;
    size_t len;
};


void run_cold_alloc_bench(size_t id, void *data)
{
    struct alloc_bench *t = (struct alloc_bench *) data;

    struct timespec t0 = ilka_now();
    {
        for (size_t i = 0; i < t->n; ++i)
            ilka_alloc(t->r, t->len);
    }
    double elapsed = ilka_elapsed(&t0);

    if (!id) ilka_print_bench(t->title, t->n, elapsed);
}

void run_warm_alloc_bench(size_t id, void *data)
{
    struct alloc_bench *t = (struct alloc_bench *) data;

    // warm-up
    {
        ilka_off_t *pages = alloca(t->n * sizeof(ilka_off_t));

        for (size_t i = 0; i < t->n; ++i)
            pages[i] = ilka_alloc(t->r, t->len);

        for (size_t i = 0; i < t->n; ++i)
            ilka_free(t->r, pages[i], t->len);
    }

    struct timespec t0 = ilka_now();
    {
        for (size_t i = 0; i < t->n; ++i)
            ilka_alloc(t->r, t->len);
    }
    double elapsed = ilka_elapsed(&t0);

    if (!id) ilka_print_bench(t->title, t->n, elapsed);
}

void run_linear_free_bench(size_t id, void *data)
{
    struct alloc_bench *t = (struct alloc_bench *) data;

    ilka_off_t *pages = alloca(t->n * sizeof(ilka_off_t));

    for (size_t i = 0; i < t->n; ++i)
        pages[i] = ilka_alloc(t->r, t->len);

    struct timespec t0 = ilka_now();
    {
        for (size_t i = 0; i < t->n; ++i)
            ilka_free(t->r, pages[i], t->len);
    }
    double elapsed = ilka_elapsed(&t0);

    if (!id) ilka_print_bench(t->title, t->n, elapsed);
}


void run_mixed_free_bench(size_t id, void *data)
{
    struct alloc_bench *t = (struct alloc_bench *) data;

    ilka_off_t *pages = alloca(t->n * sizeof(ilka_off_t));

    for (size_t i = 0; i < t->n; ++i)
        pages[i] = ilka_alloc(t->r, t->len);

    struct timespec t0 = ilka_now();
    {
        for (size_t i = 0; i < t->n; i += 2)
            ilka_free(t->r, pages[i], t->len);

        for (size_t i = 1; i < t->n; i += 2)
            ilka_free(t->r, pages[i], t->len);
    }
    double elapsed = ilka_elapsed(&t0);

    if (!id) ilka_print_bench(t->title, t->n, elapsed);
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

    ilka_close(r);
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
    ilka_run_threads(run_alloc_test, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(page_cold_alloc_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "page_cold_alloc_bench_st",
        .r = r,
        .n = 1000,
        .len = ILKA_PAGE_SIZE,
    };
    run_cold_alloc_bench(0, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(page_cold_alloc_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "page_cold_alloc_bench_mt",
        .r = r,
        .n = 1000,
        .len = ILKA_PAGE_SIZE,
    };
    ilka_run_threads(run_cold_alloc_bench, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(page_warm_alloc_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "page_warm_alloc_bench_st",
        .r = r,
        .n = 1000,
        .len = ILKA_PAGE_SIZE,
    };
    run_warm_alloc_bench(0, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(page_warm_alloc_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "page_warm_alloc_bench_mt",
        .r = r,
        .n = 1000,
        .len = ILKA_PAGE_SIZE,
    };
    ilka_run_threads(run_warm_alloc_bench, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(page_linear_free_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "page_linear_free_bench_st",
        .r = r,
        .n = 1000,
        .len = ILKA_PAGE_SIZE,
    };
    run_linear_free_bench(0, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(page_linear_free_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "page_linear_free_bench_mt",
        .r = r,
        .n = 1000,
        .len = ILKA_PAGE_SIZE,
    };
    ilka_run_threads(run_linear_free_bench, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(page_mixed_free_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "page_mixed_free_bench_st",
        .r = r,
        .n = 1000,
        .len = ILKA_PAGE_SIZE,
    };
    run_mixed_free_bench(0, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(page_mixed_free_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "page_mixed_free_bench_mt",
        .r = r,
        .n = 1000,
        .len = ILKA_PAGE_SIZE,
    };
    ilka_run_threads(run_mixed_free_bench, &tdata);

    ilka_close(r);
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

    ilka_close(r);
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
    ilka_run_threads(run_alloc_test, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(block_cold_alloc_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "block_cold_alloc_bench_st",
        .r = r,
        .n = 1000,
        .len = sizeof(uint64_t),
    };
    run_cold_alloc_bench(0, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(block_cold_alloc_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "block_cold_alloc_bench_mt",
        .r = r,
        .n = 1000,
        .len = sizeof(uint64_t),
    };
    ilka_run_threads(run_cold_alloc_bench, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(block_warm_alloc_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "block_warm_alloc_bench_st",
        .r = r,
        .n = 1000,
        .len = sizeof(uint64_t),
    };
    run_warm_alloc_bench(0, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(block_warm_alloc_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "block_warm_alloc_bench_mt",
        .r = r,
        .n = 1000,
        .len = sizeof(uint64_t),
    };
    ilka_run_threads(run_warm_alloc_bench, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(block_linear_free_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "block_linear_free_bench_st",
        .r = r,
        .n = 1000,
        .len = sizeof(uint64_t),
    };
    run_linear_free_bench(0, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(block_linear_free_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "block_linear_free_bench_mt",
        .r = r,
        .n = 1000,
        .len = sizeof(uint64_t),
    };
    ilka_run_threads(run_linear_free_bench, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(block_mixed_free_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "block_mixed_free_bench_st",
        .r = r,
        .n = 1000,
        .len = sizeof(uint64_t),
    };
    run_mixed_free_bench(0, &tdata);

    ilka_close(r);
}
END_TEST

START_TEST(block_mixed_free_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct alloc_bench tdata = {
        .title = "block_mixed_free_bench_mt",
        .r = r,
        .n = 1000,
        .len = sizeof(uint64_t),
    };
    ilka_run_threads(run_mixed_free_bench, &tdata);

    ilka_close(r);
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_dbg_abort_on_fail();

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
}

int main(void)
{
    return ilka_tests("alloc_test", &make_suite);
}
