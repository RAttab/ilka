/* vec_test.c
   Rémi Attab (remi.attab@gmail.com), 11 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "vec.h"

// -----------------------------------------------------------------------------
// basics_test
// -----------------------------------------------------------------------------

START_TEST(basics_test)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct ilka_vec *v0 = ilka_vec_alloc(r, sizeof(uint64_t), 0);
    if (!v0) ilka_abort();

    struct ilka_vec *v1 = ilka_vec_open(r, ilka_vec_off(v0));
    if (!v1) ilka_abort();

    ck_assert(!ilka_vec_len(v0));
    ck_assert_int_eq(ilka_vec_len(v0), ilka_vec_len(v1));

    uint64_t value = 10;
    if (!ilka_vec_append(v0, &value, 1)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v0), 1);
    ck_assert_int_eq(ilka_vec_len(v1), 1);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v0, 0, 1)), value);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v1, 0, 1)), value);


    *((uint64_t *) ilka_vec_write(v1, 0, 1)) = value = 20;
    ck_assert_int_eq(ilka_vec_len(v0), 1);
    ck_assert_int_eq(ilka_vec_len(v1), 1);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v0, 0, 1)), value);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v1, 0, 1)), value);

    uint64_t values[4] = { 11, 22, 33, 44 };
    if (!ilka_vec_insert(v0, values, 0, 4)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v1), 5);
    for (size_t i = 0; i < 4; ++i)
        ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v1, i, 1)), values[i]);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v1, 4, 1)), value);

    if (!ilka_vec_remove(v1, 2, 2)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v0), 3);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v0, 0, 1)), values[0]);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v0, 1, 1)), values[1]);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v0, 2, 1)), value);

    if (!ilka_vec_close(v1)) ilka_abort();
    if (!ilka_vec_free(v0)) ilka_abort();
    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, basics_test, true);
}

int main(void)
{
    return ilka_tests("vec_test", &make_suite);
}
