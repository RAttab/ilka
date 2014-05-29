/* bits_test.c
   Rémi Attab (remi.attab@gmail.com), 28 May 2014
   FreeBSD-style copyright and disclaimer apply

   bit ops test.
*/

#include "utils/bits.h"
#include "check.h"

START_TEST(next_bitfield)
{
}
END_TEST

void make_suite(Suite *suite)
{
    TCase *tcase = tcase_create("next_bitfield");
    tcase_add_test(tcase, next_bitfield);
    suite_add_tcase(suite, tcase);
}

int main(void)
{
    return ilka_tests("bits", &make_suite);
}


