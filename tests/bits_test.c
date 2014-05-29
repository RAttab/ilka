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

void make_suite(Suite *s)
{
    ilka_tc(s, next_bitfield);
}

int main(void)
{
    return ilka_tests("bits", &make_suite);
}


