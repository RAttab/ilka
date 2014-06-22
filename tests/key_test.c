/* key_test.c
   Rémi Attab (remi.attab@gmail.com), 14 Jun 2014
   FreeBSD-style copyright and disclaimer apply

   key test
*/

#include "key.h"
#include "check.h"


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

#define check_read(bits, it, exp)                               \
    do {                                                        \
        uint ## bits ## _t r = ilka_key_read_ ## bits (it);     \
        ck_assert_int_eq(r, exp);                               \
    } while(0)

#define check_leftover(it, exp)                 \
    do {                                        \
        size_t r = ilka_key_leftover(it);       \
        size_t e = (exp);                       \
        ck_assert_int_eq(r, e);                 \
    } while(0)


// -----------------------------------------------------------------------------
// empty
// -----------------------------------------------------------------------------

START_TEST(empty_test)
{
    struct ilka_key k;
    ilka_key_init(&k);

    struct ilka_key_it it = ilka_key_begin(&k);
    ck_assert(ilka_key_end(it));
    check_leftover(it, 0);

    ck_assert(!ilka_key_cmp(&k, &k));

    {
        struct ilka_key other;
        ilka_key_init(&other);
        ilka_key_copy(&k, &other);

        ck_assert(!ilka_key_cmp(&k, &other));

        struct ilka_key_it it = ilka_key_begin(&other);
        ck_assert(ilka_key_end(it));
        check_leftover(it, 0);

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
        struct ilka_key_it it = ilka_key_begin(&k);
        ilka_key_write_8(&it, v_8);
        ilka_key_write_32(&it, v_32);
        ilka_key_write_str(&it, v_str, sizeof(v_str));
        ilka_key_write_64(&it, v_64);
        ilka_key_write_16(&it, v_16);

        ck_assert(ilka_key_end(it));
        ck_assert(!ilka_key_leftover(it));
    }

    ck_assert(!ilka_key_cmp(&k, &k));

    {
        struct ilka_key_it it = ilka_key_begin(&k);

        size_t leftover = sizeof(v_str)
            + sizeof(v_64) + sizeof(v_32) + sizeof(v_16) + sizeof(v_8);
        check_leftover(it, leftover *= 8);

        check_read(8, &it, v_8);
        check_leftover(it, leftover -= sizeof(v_8) * 8);

        check_read(32, &it, v_32);
        check_leftover(it, leftover -= sizeof(v_32) * 8);

        char buf[256] = { 0 };
        ilka_key_read_str(&it, buf, sizeof(v_str));
        ck_assert_str_eq(buf, v_str);
        check_leftover(it, leftover -= sizeof(v_str) * 8);

        check_read(64, &it, v_64);
        check_leftover(it, leftover -= sizeof(v_64) * 8);

        check_read(16, &it, v_16);
        check_leftover(it, leftover -= sizeof(v_16) * 8);

        ck_assert(ilka_key_end(it));
    }

    ilka_key_free(&k);
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
