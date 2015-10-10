/* mmap_test.c
   Rémi Attab (remi.attab@gmail.com), 07 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"


// -----------------------------------------------------------------------------
// coalesce test
// -----------------------------------------------------------------------------


START_TEST(coalesce_test_st)
{
    const size_t n = ILKA_PAGE_SIZE;

    struct ilka_options options = {
        .open = true,
        .create = true,
        .vma_reserved = n
    };
    struct ilka_region *r = ilka_open("blah", &options);

    ilka_srand(1);

    size_t pages = 0;

    ilka_off_t start = ilka_grow(r, n);
    memset(ilka_write(r, start, n), pages++, n);

    for (size_t i = 0; i < 8; ++i) {

        size_t k = ilka_rand_range(1, 8);
        for (size_t j = 0; j < k; ++j) {
            ilka_off_t off = ilka_grow(r, n);
            memset(ilka_write(r, off, n), pages++, n);
        }

        // coalesce happens on world stop.
        ilka_world_stop(r);
        ilka_world_resume(r);
    }

    for (size_t i = 0; i < pages; ++i) {
        const uint8_t *p = ilka_read(r, start + (i * n), n);
        for (size_t j = 0; j < n; ++j) {
            ilka_assert(p[j] == i,
                    "invalid value: %x != %x", (unsigned) p[i], (unsigned) i);
        }
    };

    if (!ilka_close(r)) ilka_abort();
}
END_TEST



// -----------------------------------------------------------------------------
// access bench
// -----------------------------------------------------------------------------

START_TEST(access_bench_st)
{

}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, coalesce_test_st, true);
    ilka_tc(s, access_bench_st, false);
}

int main(void)
{
    return ilka_tests("test_name", &make_suite);
}
