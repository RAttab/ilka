/* key_test.c
   Rémi Attab (remi.attab@gmail.com), 14 Jun 2014
   FreeBSD-style copyright and disclaimer apply

   key test
*/

#include "check.h"
#include "struct/key.h"


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

#define check_read(bits, it, exp)                               \
    do {                                                        \
        uint ## bits ## _t r = -1;                              \
        *(it) = ilka_key_read_ ## bits (*(it), &r);             \
        ck_assert(!ilka_key_err(*(it)));                        \
        ck_assert_int_eq(r, exp);                               \
    } while(0)

#define check_remaining(it, exp)                \
    do {                                        \
        size_t r = ilka_key_remaining_bits(it); \
        size_t e = (exp);                       \
        ck_assert_int_eq(r, e);                 \
        ck_assert(!ilka_key_err(it));           \
    } while(0)


// -----------------------------------------------------------------------------
// empty
// -----------------------------------------------------------------------------

START_TEST(empty_test)
{
    struct ilka_key k;
    ilka_key_init(&k);

    struct ilka_key_it it = ilka_key_at(&k, 0);
    ck_assert(ilka_key_end(it));
    check_remaining(it, 0);

    ck_assert(!ilka_key_cmp(&k, &k));

    {
        struct ilka_key other;
        ilka_key_init(&other);
        ilka_key_copy(&k, &other);

        ck_assert(!ilka_key_cmp(&k, &other));

        struct ilka_key_it it = ilka_key_at(&other, 0);
        ck_assert(!ilka_key_err(it));
        ck_assert(ilka_key_end(it));
        check_remaining(it, 0);

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
    struct ilka_key k;
    ilka_key_init(&k);

    const char v_str[] =
        "this is a string with letters in it "
        "but it's aparently not long enough "
        "so I'm making it longer by adding more letters";
    const uint64_t v_64 = 0xA55A7887A55A7887UL;
    const uint32_t v_32 = 0x01234567;
    const uint16_t v_16 = 0x89AB;
    const uint8_t  v_8  = 0xCD;

    {
        struct ilka_key_it it = ilka_key_at(&k, 0);
        it = ilka_key_write_8(it, v_8);
        it = ilka_key_write_32(it, v_32);
        it = ilka_key_write_str(it, v_str, strlen(v_str));
        it = ilka_key_write_64(it, v_64);
        it = ilka_key_write_16(it, v_16);

        ck_assert(ilka_key_end(it));
        ck_assert(!ilka_key_err(it));
        ck_assert(!ilka_key_remaining_bits(it));
    }

    ck_assert(!ilka_key_cmp(&k, &k));

    {
        struct ilka_key_it it = ilka_key_at(&k, 0);
        ck_assert(!ilka_key_end(it));

        size_t remaining_bits = sizeof(v_str)
            + sizeof(v_64) + sizeof(v_32) + sizeof(v_16) + sizeof(v_8);
        check_remaining(it, remaining_bits *= 8);

        check_read(8, &it, v_8);
        check_remaining(it, remaining_bits -= sizeof(v_8) * 8);

        check_read(32, &it, v_32);
        check_remaining(it, remaining_bits -= sizeof(v_32) * 8);

        char buf[256] = { 0 };
        it = ilka_key_read_str(it, buf, sizeof(v_str));
        ck_assert_str_eq(buf, v_str);
        check_remaining(it, remaining_bits -= sizeof(v_str) * 8);

        check_read(64, &it, v_64);
        check_remaining(it, remaining_bits -= sizeof(v_64) * 8);

        check_read(16, &it, v_16);
        check_remaining(it, remaining_bits -= sizeof(v_16) * 8);

        ck_assert(ilka_key_end(it));
    }

    ilka_key_free(&k);
}
END_TEST



// -----------------------------------------------------------------------------
// cmp_test
// -----------------------------------------------------------------------------

struct ilka_key
make_key_impl(const char *data, size_t data_n)
{
    struct ilka_key k;
    ilka_key_init(&k);
    ilka_key_write_bytes(ilka_key_at(&k, 0), (const uint8_t *) data, data_n);
    return k;
}

#define small_prefix "ab"
#define long_prefix                                                     \
    "this is a long prefix and it's long because I want it to be long."
#define make_key(str) make_key_impl(str, sizeof(str))

START_TEST(cmp_test)
{
    struct ilka_key ks   = make_key(small_prefix);
    struct ilka_key ks_a = make_key(small_prefix "a");
    struct ilka_key ks_b = make_key(small_prefix "b");
    struct ilka_key ks_c = make_key(small_prefix "c");
    struct ilka_key ks_l = make_key(small_prefix long_prefix);

    struct ilka_key kl   = make_key(long_prefix);
    struct ilka_key kl_a = make_key(long_prefix "a");
    struct ilka_key kl_b = make_key(long_prefix "b");
    struct ilka_key kl_c = make_key(long_prefix "c");

    ck_assert(!ilka_key_cmp(&ks, &ks));
    ck_assert(!ilka_key_cmp(&kl, &kl));
    ck_assert(!ilka_key_cmp(&ks_a, &ks_a));
    ck_assert(!ilka_key_cmp(&ks_l, &ks_l));
    ck_assert(!ilka_key_cmp(&kl_a, &kl_a));

    ck_assert(ilka_key_cmp(&ks, &ks_a) < 0);
    ck_assert(ilka_key_cmp(&ks_a, &ks) > 0);

    ck_assert(ilka_key_cmp(&ks_a, &ks_b) < 0);
    ck_assert(ilka_key_cmp(&ks_b, &ks_a) > 0);

    ck_assert(ilka_key_cmp(&ks_c, &ks_b) > 0);
    ck_assert(ilka_key_cmp(&ks_b, &ks_c) < 0);

    ck_assert(ilka_key_cmp(&ks, &ks_l) < 0);
    ck_assert(ilka_key_cmp(&ks_l, &ks) > 0);

    ck_assert(ilka_key_cmp(&kl, &kl_a) < 0);
    ck_assert(ilka_key_cmp(&kl_a, &kl) > 0);

    ck_assert(ilka_key_cmp(&kl_a, &kl_b) < 0);
    ck_assert(ilka_key_cmp(&kl_b, &kl_a) > 0);

    ck_assert(ilka_key_cmp(&kl_c, &kl_b) > 0);
    ck_assert(ilka_key_cmp(&kl_b, &kl_c) < 0);

    ilka_key_free(&ks);
    ilka_key_free(&ks_a);
    ilka_key_free(&ks_b);
    ilka_key_free(&ks_c);
    ilka_key_free(&ks_l);

    ilka_key_free(&kl);
    ilka_key_free(&kl_a);
    ilka_key_free(&kl_b);
    ilka_key_free(&kl_c);
}
END_TEST


// -----------------------------------------------------------------------------
// endian_test
// -----------------------------------------------------------------------------

START_TEST(endian_test)
{
    struct ilka_key k;
    ilka_key_init(&k);

    const uint64_t c = 0x0123456789ABCDEFUL;

    {
        struct ilka_key_it it = ilka_key_at(&k, 0);
        it = ilka_key_write_64(it, c);
    }

    {
        struct ilka_key_it it = ilka_key_at(&k, 0);
        for (size_t i = 0; i < 8; ++i) {
            size_t j = 64 - ((i + 1) * 8);

            uint8_t data;
            it = ilka_key_read_8(it, &data);
            ck_assert_int_eq(data, (c >> j) & 0xFF);
        }
    }

    ilka_key_free(&k);
}
END_TEST

START_TEST(region_test)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    const uint8_t *data = (const uint8_t *)
        "what's with all these long strings that never end."
        "It's like there's this conspiracy or something."
        "I don't like it one bit.";

    struct ilka_key a, b;

    for (size_t i = 0; i < 64; ++i) {
        ilka_key_write_bytes(ilka_key_at(&a, 0), data, i);

        ilka_off_t off = ilka_key_region_save(r, &a);
        ilka_assert(off, "unable to save key");

        struct ilka_key_it it = ilka_key_region_load(r, off, ilka_key_at(&b, 0));
        ilka_assert(!ilka_key_err(it), "unable to load region");
        ilka_assert(!ilka_key_cmp(&a, &b), "loaded wrong key data");

        ilka_key_region_free(r, off);
    }

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, empty_test, false);
    ilka_tc(s, read_write_test, false);
    ilka_tc(s, cmp_test, false);
    ilka_tc(s, endian_test, false);
    ilka_tc(s, region_test, true);
}

int main(void)
{
    return ilka_tests("key_test", &make_suite);
}



#if 0 // Not supported at the moment

// -----------------------------------------------------------------------------
// bits_test
// -----------------------------------------------------------------------------

/* printf("check_pop: value=%p, exp=%p, eq=%d\n", (void *) peek, (void *) (exp), peek == (exp)); \ */

#define check_pop(it, bits, exp)                        \
    do {                                                \
        uint64_t peek = ilka_key_peek(it, bits);        \
        ck_assert_int_eq(peek, exp);                    \
                                                        \
        uint64_t pop = ilka_key_pop(&it, bits);         \
        ck_assert_int_eq(pop, exp);                     \
    } while(0)


START_TEST(bits_test)
{
    struct ilka_key k;
    ilka_key_init(&k);


    const uint64_t c = 0xFEDCBA0987654321UL;
    const size_t chunks = (ILKA_KEY_CHUNK_SIZE + 1) * 2;

    size_t bits = 0;

    {
        struct ilka_key_it it = ilka_key_at(&k, 0);

        ilka_key_push(&it, 0x5, 3);
        bits += 3;

        for (size_t i = 0; i < 64; ++i) {
            ilka_key_push(&it, c, i);
            bits += i;
        }

        ilka_key_push(&it, 0x15, 5);
        bits += 5;

        for (size_t i = 0; i < chunks; ++i) {
            ilka_key_push(&it, i % 2 ? c : 0UL, 64);
            bits += 64;
        }

        ck_assert(ilka_key_end(it));
        check_remaining(it, 0);
    }

    {
        struct ilka_key_it it = ilka_key_at(&k, 0);

        check_pop(it, 3, 0x5);
        check_remaining(it, bits -= 3);

        for (size_t i = 0; i < 64; ++i) {
            check_pop(it, i, c & ((1UL << i) - 1));
            check_remaining(it, bits -= i);
        }

        check_pop(it, 5, 0x15);
        check_remaining(it, bits -= 5);

        for (size_t i = 0; i < chunks; ++i) {
            check_pop(it, 64, i % 2 ? c : 0UL);
            check_remaining(it, bits -= 64);
        }

        ck_assert(ilka_key_end(it));
    }

    ilka_key_free(&k);
}
END_TEST

#endif
