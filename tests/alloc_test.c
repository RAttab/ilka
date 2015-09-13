/* alloc_test.c
   Rémi Attab (remi.attab@gmail.com), 12 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "utils/arch.h"
#include "utils/rand.h"
#include "utils/error.h"
#include "region/region.h"

#include <stdlib.h>

enum
{
    blocks = 10,
    allocs = 100 * blocks,
    max_size = 128,
};

struct alloc_node
{
    ilka_off_t off;
    size_t len;
};


// -----------------------------------------------------------------------------
// page test
// -----------------------------------------------------------------------------

inline void fill_page(
        struct ilka_region *r, ilka_off_t off, size_t len, size_t value)
{
    size_t *data = ilka_write(r, off, len);
    for (size_t i = 0; i < len / sizeof(size_t); ++i) data[i] = value;
}

#define check_page(r, off, len)                                         \
    do {                                                                \
        size_t *data = ilka_write(r, off, len);                         \
        size_t value = data[0];                                         \
        for (size_t i = 1; i < len / sizeof(size_t); ++i) {             \
            ilka_assert(data[i] == value,                               \
                    "wrong value in page: %lu != %lu", value, data[i]); \
        }                                                               \
    } while(false)

void run_page_test(struct ilka_region *r, int id)
{
    ilka_srand(id + 1);
    struct alloc_node nodes[blocks] = {{0}};

    for (size_t alloc = 0; alloc < allocs; ++alloc) {
        size_t i = ilka_rand_range(0, blocks);

        if (nodes[i].off) {
            check_page(r, nodes[i].off, nodes[i].len);
            ilka_free(r, nodes[i].off, nodes[i].len);
            nodes[i] = (struct alloc_node) {0};
        }
        else {
            size_t max = ilka_rand_range(2, max_size);
            nodes[i].len = ilka_rand_range(1, max) * ILKA_PAGE_SIZE;
            nodes[i].off = ilka_alloc(r, nodes[i].len);
            fill_page(r, nodes[i].off, nodes[i].len, alloc);
        }
    }

    for (size_t i = 0; i < blocks; ++i) {
        if (!nodes[i].off) continue;
        check_page(r, nodes[i].off, nodes[i].len);
        ilka_free(r, nodes[i].off, nodes[i].len);
    }
}

START_TEST(page_test_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    run_page_test(r, 0);

    ilka_close(r);
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, page_test_st);
}

int main(void)
{
    return ilka_tests("alloc_test", &make_suite);
}
