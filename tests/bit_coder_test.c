/* bit_coder_test.c
   Rémi Attab (remi.attab@gmail.com), 10 Jun 2014
   FreeBSD-style copyright and disclaimer apply

   Bit encoder/decoder test
*/

#include "utils/bit_coder.h"
#include "check.h"

#include <signal.h>

#define check_decode(coder, bits, exp)          \
    do {                                        \
        uint64_t r = bit_decode(coder, bits);   \
        ck_assert_int_eq(r, exp);               \
    } while(0);

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
        ck_assert_int_eq(bit_encoder_leftover(&coder), 8);

        bit_encode(&coder, 0xFF, 8);
        ck_assert_int_eq(bit_encoder_offset(&coder), 8);
        ck_assert_int_eq(bit_encoder_leftover(&coder), 0);
        ck_assert_int_eq(a, 0xFF);
    }

    {
        struct bit_decoder coder;
        bit_decoder_init(&coder, &a, sizeof(a));
        ck_assert_int_eq(bit_decoder_offset(&coder), 0);
        ck_assert_int_eq(bit_decoder_leftover(&coder), 8);

        check_decode(&coder, 8, a);
        ck_assert_int_eq(bit_decoder_offset(&coder), 8);
        ck_assert_int_eq(bit_decoder_leftover(&coder), 0);
    }

}
END_TEST


// -----------------------------------------------------------------------------
// complex test
// -----------------------------------------------------------------------------

START_TEST(complex_test)
{
    const uint64_t c5 = 0x5555555555555555UL;
    const uint64_t cf = 0xFFFFFFFFFFFFFFFFUL;

    uint64_t v[4] = { cf, cf, cf, cf };

    {
        size_t off = 0, left = sizeof(v) * 8;
        struct bit_encoder coder;
        bit_encoder_init(&coder, v, sizeof(v));

        off += 1; left -= 1;
        bit_encode(&coder, 0, 1);
        ck_assert_int_eq(bit_encoder_offset(&coder), off);
        ck_assert_int_eq(bit_encoder_leftover(&coder), left);

        off += 64; left -= 64;
        bit_encode(&coder, c5, 64);
        ck_assert_int_eq(bit_encoder_offset(&coder), off);
        ck_assert_int_eq(bit_encoder_leftover(&coder), left);

        off += 64; left -= 64;
        bit_encode_skip(&coder, 64);
        ck_assert_int_eq(bit_encoder_offset(&coder), off);
        ck_assert_int_eq(bit_encoder_leftover(&coder), left);

        off += 63; left -= 63;
        bit_encode(&coder, c5, 63);
        ck_assert_int_eq(bit_encoder_offset(&coder), off);
        ck_assert_int_eq(bit_encoder_leftover(&coder), left);

        for (size_t i = 0; i < 10; ++i) {
            off += i; left -= i;
            bit_encode(&coder, i, i);
            ck_assert_int_eq(bit_encoder_offset(&coder), off);
            ck_assert_int_eq(bit_encoder_leftover(&coder), left);
        }
    }

    {
        size_t off = 0, left = sizeof(v) * 8;
        struct bit_decoder coder;
        bit_decoder_init(&coder, v, sizeof(v));

        off += 1; left -= 1;
        check_decode(&coder, 1, 0UL);
        ck_assert_int_eq(bit_decoder_offset(&coder), off);
        ck_assert_int_eq(bit_decoder_leftover(&coder), left);

        off += 64; left -= 64;
        check_decode(&coder, 64, c5);
        ck_assert_int_eq(bit_decoder_offset(&coder), off);
        ck_assert_int_eq(bit_decoder_leftover(&coder), left);

        off += 64; left -= 64;
        bit_decode_skip(&coder, 64);
        ck_assert_int_eq(bit_decoder_offset(&coder), off);
        ck_assert_int_eq(bit_decoder_leftover(&coder), left);

        off += 63; left -= 63;
        check_decode(&coder, 63, c5);
        ck_assert_int_eq(bit_decoder_offset(&coder), off);
        ck_assert_int_eq(bit_decoder_leftover(&coder), left);

        for (size_t i = 0; i < 10; ++i) {
            off += i; left -= i;
            check_decode(&coder, i, i);
            ck_assert_int_eq(bit_decoder_offset(&coder), off);
            ck_assert_int_eq(bit_decoder_leftover(&coder), left);
        }
    }
}
END_TEST


// -----------------------------------------------------------------------------
// skip test
// -----------------------------------------------------------------------------

START_TEST(skip_test)
{
    uint64_t v = 0xFFFFFFFFFFFFFFFFUL;

    {
        struct bit_encoder coder;
        bit_encoder_init(&coder, &v, sizeof(v));

        bit_encode(&coder, 0, 13);
        bit_encode_skip(&coder, 17);
        bit_encode(&coder, 0, 64 - 13 - 17);
    }

    {
        struct bit_decoder coder;
        bit_decoder_init(&coder, &v, sizeof(v));

        check_decode(&coder, 13, 0);
        check_decode(&coder, 17, (1UL << 17) - 1);
        check_decode(&coder, 64 - 13 - 17, 0);
    }

}
END_TEST


// -----------------------------------------------------------------------------
// edge_test
// -----------------------------------------------------------------------------

START_TEST(edge_test)
{
    uint64_t v[2] = { 0 };
    const uint64_t c = 0xFFFFFFFFFFFFFFFFUL;

    for (size_t i = 0; i < 8; ++i) {
        {
            struct bit_encoder coder;
            bit_encoder_init(&coder, v, sizeof(v));
            bit_encode_skip(&coder, i);
            bit_encode(&coder, c, 64);
        }

        {
            struct bit_decoder coder;
            bit_decoder_init(&coder, v, sizeof(v));
            bit_decode_skip(&coder, i);

            uint64_t r = bit_decode(&coder, 64);
            if (r == c) continue;

            printf("fail: i=%zu, r=%p, coder={ pos=%zu, sz=%zu }\n",
                    i, (void *) r, coder.pos, coder.size);
        }
    }
}
END_TEST


// -----------------------------------------------------------------------------
// endian test
// -----------------------------------------------------------------------------

void endian_test_impl(size_t bits)
{
    uint64_t v = 0;
    const uint64_t c = 0x0123456789ABCDEFUL;

    {
        struct bit_encoder coder;
        bit_encoder_init(&coder, &v, sizeof(v));

        for (size_t i = 0; i < (64 / bits); ++i)
            bit_encode(&coder, c >> (i * bits), bits);
    }

    {
        struct bit_decoder coder;
        bit_decoder_init(&coder, &v, sizeof(v));

        for (size_t i = 0; i < 8; ++i)
            check_decode(&coder, 8, (c >> (i * 8)) & 0xFF);
    }
}

START_TEST(endian_test)
{
    endian_test_impl( 1);
    endian_test_impl( 2);
    endian_test_impl( 4);
    endian_test_impl( 8);
    endian_test_impl(16);
    endian_test_impl(32);
    endian_test_impl(64);
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
    ilka_tc(s, basics_test, true);
    ilka_tc(s, complex_test, true);
    ilka_tc(s, skip_test, true);
    ilka_tc(s, edge_test, true);
    ilka_tc(s, endian_test, true);

    ilka_tc_signal(s, bound_test_1, SIGABRT, true);
    ilka_tc_signal(s, bound_test_2, SIGABRT, true);
    ilka_tc_signal(s, bound_test_3, SIGABRT, true);
}

int main(void)
{
    return ilka_tests("bit_coder_test", &make_suite);
}
