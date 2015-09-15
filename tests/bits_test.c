/* bits_test.c
   Rémi Attab (remi.attab@gmail.com), 28 May 2014
   FreeBSD-style copyright and disclaimer apply

   bit ops test.
*/

#include "utils/bits.h"
#include "check.h"

inline size_t floor_div(size_t n, size_t d) { return n / d; }


// -----------------------------------------------------------------------------
// checck bitfield next
// -----------------------------------------------------------------------------

#define check_bf_next(v, i, exp)                \
    ck_assert_int_eq(bitfield_next(v, i), exp)

START_TEST(check_bitfield_next)
{

    for (size_t i = 0; i < 64; ++i)
        check_bf_next(0x0000000000000000UL, i, 64);

    for (size_t i = 0; i < 64; ++i)
        check_bf_next(0xFFFFFFFFFFFFFFFFUL, i, i);

    for (size_t i = 0; i < 64; ++i)
        check_bf_next(0x5555555555555555UL, i, ceil_div(i, 2) * 2);

    for (size_t i = 0; i < 64; ++i)
        check_bf_next(0xAAAAAAAAAAAAAAAAUL, i, floor_div(i, 2) * 2 + 1);
}
END_TEST


// -----------------------------------------------------------------------------
// check leading bit
// -----------------------------------------------------------------------------

#define check_lbit(v, exp)                      \
    ck_assert_int_eq(leading_bit(v), exp);

START_TEST(check_leading_bit)
{
    check_lbit(0, 0);

    for (size_t i = 0; i < 64; ++i)
        check_lbit(1UL << i, 1UL << i);

    for (size_t i = 1; i < 64; ++i)
        check_lbit((1UL << i) - 1, 1UL << (i - 1));
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, check_bitfield_next, true);
    ilka_tc(s, check_leading_bit, true);
}

int main(void)
{
    return ilka_tests("bits", &make_suite);
}


