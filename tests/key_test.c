/* key_test.c
   Rémi Attab (remi.attab@gmail.com), 14 Jun 2014
   FreeBSD-style copyright and disclaimer apply

   key test
*/

#include "key.h"
#include "check.h"


// -----------------------------------------------------------------------------
// empty
// -----------------------------------------------------------------------------

START_TEST(empty_test)
{
    struct ilka_key k;
    ilka_key_init(&k);

    struct ilka_key_it it = ilka_key_begin(&k);
    ck_assert(ilka_key_end(it));
    ck_assert_int_eq(ilka_key_leftover(it), 0);

    ck_assert(!ilka_key_cmp(&k, &k));

    {
        struct ilka_key other;
        ilka_key_init(&other);
        ilka_key_copy(&k, &other);

        ck_assert(!ilka_key_cmp(&k, &other));

        struct ilka_key_it it = ilka_key_begin(&other);
        ck_assert(ilka_key_end(it));
        ck_assert_int_eq(ilka_key_leftover(it), 0);

        ilka_key_free(&other);
    }

    ilka_key_free(&k);
}
END_TEST

// -----------------------------------------------------------------------------
// read_write_test
// -----------------------------------------------------------------------------

START_TEST(read_write_test)
{

}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, empty_test);
    ilka_tc(s, read_write_test);
}

int main(void)
{
    return ilka_tests("key_test", &make_suite);
}
