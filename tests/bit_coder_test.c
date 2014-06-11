/* bit_coder_test.c
   Rémi Attab (remi.attab@gmail.com), 10 Jun 2014
   FreeBSD-style copyright and disclaimer apply

   Bit encoder/decoder test
*/

#include "utils/bit_coder.h"
#include "check.h"

#include <signal.h>


// -----------------------------------------------------------------------------
// basics test
// -----------------------------------------------------------------------------

START_TEST(basics_test)
{
    uint8_t a = 0;

    {
        struct bit_encoder coder;
        bit_encoder_init(&coder, &a, sizeof(a));
        ck_assert_int_eq(bit_encoder_offset(&coder), 0);

        bit_encode(&coder, 0xFF, 8);
        ck_assert_int_eq(bit_encoder_offset(&coder), 8);
        ck_assert_int_eq(a, 0xFF);
    }

    {
        struct bit_decoder coder;
        bit_decoder_init(&coder, &a, sizeof(a));
        ck_assert_int_eq(bit_decoder_offset(&coder), 0);

        uint8_t v = bit_decode(&coder, 8);
        ck_assert_int_eq(bit_decoder_offset(&coder), 8);
        ck_assert_int_eq(a, v);
    }

}
END_TEST


// -----------------------------------------------------------------------------
// bound tests
// -----------------------------------------------------------------------------

START_TEST(bound_test_1)
{
    uint64_t v = 0;
    struct bit_encoder coder;
    bit_encoder_init(&coder, &v, sizeof(v));
    bit_encoder_check(&coder, 65);
}
END_TEST

START_TEST(bound_test_2)
{
    uint64_t v = 0;
    struct bit_encoder coder;
    bit_encoder_init(&coder, &v, sizeof(v));
    bit_encode(&coder, 0, 1);
    bit_encoder_check(&coder, 64);
}
END_TEST

START_TEST(bound_test_3)
{
    uint64_t v = 0;
    struct bit_encoder coder;
    bit_encoder_init(&coder, &v, sizeof(v));
    bit_encode(&coder, 0, 64);
    bit_encoder_check(&coder, 1);
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, basics_test);
    ilka_tc_signal(s, bound_test_1, SIGABRT);
    ilka_tc_signal(s, bound_test_2, SIGABRT);
    ilka_tc_signal(s, bound_test_3, SIGABRT);
}

int main(void)
{
    return ilka_tests("bit_coder_test", &make_suite);
}
