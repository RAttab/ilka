/* alloc_test.c
   Rémi Attab (remi.attab@gmail.com), 12 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "utils/arch.h"
#include "utils/bits.h"
#include "utils/rand.h"
#include "utils/error.h"
#include "region/region.h"

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

void run_alloc_test(
        struct ilka_region *r, int id, size_t blocks, size_t allocs, size_t mul)
{
    ilka_srand(id + 1);
    struct alloc_node *nodes = calloc(blocks, sizeof(struct alloc_node));

    for (size_t alloc = 0; alloc < allocs; ++alloc) {
        size_t i = ilka_rand_range(0, blocks);

        if (nodes[i].off) {
            check_block(r, nodes[i].off, nodes[i].len);
            ilka_free(r, nodes[i].off, nodes[i].len);
            nodes[i] = (struct alloc_node) {0};
        }
        else {
            size_t max = ilka_rand_range(2, 128);
            nodes[i].len = ilka_rand_range(1, max) * mul;
            nodes[i].off = ilka_alloc(r, nodes[i].len);
            fill_block(r, nodes[i].off, nodes[i].len, alloc);
        }
    }

    for (size_t i = 0; i < blocks; ++i) {
        if (!nodes[i].off) continue;
        check_block(r, nodes[i].off, nodes[i].len);
        ilka_free(r, nodes[i].off, nodes[i].len);
    }

    free(nodes);
}


// -----------------------------------------------------------------------------
// tests
// -----------------------------------------------------------------------------

START_TEST(page_test_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    run_alloc_test(r, 0, 20, 20 * 100, ILKA_PAGE_SIZE);

    ilka_close(r);
}
END_TEST


START_TEST(block_test_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    run_alloc_test(r, 0, 100, 100 * 100, 1);

    ilka_close(r);
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, page_test_st, true);
    ilka_tc(s, block_test_st, true);
}

int main(void)
{
    return ilka_tests("alloc_test", &make_suite);
}
