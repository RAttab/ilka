/* trie_kv_test.c
   Rémi Attab (remi.attab@gmail.com), 24 Jun 2014
   FreeBSD-style copyright and disclaimer apply

   Trie kv tests
*/

#include "check.h"
#include "trie/kv.h"

// -----------------------------------------------------------------------------
// basics_test
// -----------------------------------------------------------------------------

#define check_count(info, exp)                  \
    do {                                        \
        size_t r = trie_kvs_count(info);        \
        ck_assert_int_eq(r, exp);               \
    } while(0)

START_TEST(basics_test)
{
    const uint64_t c = 0x0123456789ABCDEFUL;

    struct trie_kvs_info info;
    struct trie_kv kvs[] = {
        { c, c, trie_kvs_state_terminal }
    };

    int r = trie_kvs_info(&info, 64, 1, c, kvs, 1);
    ck_assert(r);

    ck_assert(info.is_abs_buckets);
    ck_assert_int_eq(info.buckets, 1);
    ck_assert_int_eq(info.key_len, 64);

    ck_assert(info.value_bits > 0);
    ck_assert_int_eq(info.value, c);

    ck_assert_int_eq(info.key.bits, 0);
    ck_assert_int_eq(info.key.prefix, c);

    ck_assert_int_eq(info.val.bits, 0);
    ck_assert_int_eq(info.val.prefix, c);

    check_count(&info, 1);
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, basics_test);
}

int main(void)
{
    return ilka_tests("trie_kv_test", &make_suite);
}
