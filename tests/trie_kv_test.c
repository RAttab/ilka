/* trie_kv_test.c
   Rémi Attab (remi.attab@gmail.com), 24 Jun 2014
   FreeBSD-style copyright and disclaimer apply

   Trie kv tests
*/

#include "check.h"
#include "trie/kv.h"


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

struct trie_kv
make_kv(uint64_t k, uint64_t v)
{
    return (struct trie_kv) { k, v, trie_kvs_state_terminal };
}


#define check_count(info, exp)                  \
    do {                                        \
        size_t r = trie_kvs_count(info);        \
        ck_assert_int_eq(r, exp);               \
    } while(0)


// -----------------------------------------------------------------------------
// kvs_test
// -----------------------------------------------------------------------------

START_TEST(kvs_test)
{
    struct trie_kv kvs[128];
    memset(kvs, 0, sizeof(kvs));

    {
        for (size_t i = 32; i < 64; ++i)
            trie_kvs_add(kvs, 128, make_kv(i, i));

        for (size_t i = 0; i < 32; ++i)
            ck_assert_int_eq(kvs[i].key, i + 32);
    }

    {
        for (size_t i = 0; i < 32; ++i)
            trie_kvs_add(kvs, 128, make_kv(i, i));

        for (size_t i = 0; i < 64; ++i)
            ck_assert_int_eq(kvs[i].key, i);
    }

    {
        for (size_t i = 64; i < 128; i += 2)
            trie_kvs_add(kvs, 128, make_kv(i, i));
        for (size_t i = 64 + 1; i < 128; i += 2)
            trie_kvs_add(kvs, 128, make_kv(i, i));

        for (size_t i = 0; i < 128; ++i) {
            ck_assert_int_eq(kvs[i].key, i);
            ck_assert_int_eq(kvs[i].val, i);
        }
    }

    {
        for (size_t i = 0; i < 128; ++i)
            trie_kvs_set(kvs, 128, make_kv(i, i * 2));

        for (size_t i = 0; i < 128; ++i) {
            ck_assert_int_eq(kvs[i].key, i);
            ck_assert_int_eq(kvs[i].val, i * 2);
        }
    }
}
END_TEST


// -----------------------------------------------------------------------------
// basics_test
// -----------------------------------------------------------------------------

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
    ilka_tc(s, kvs_test);
    ilka_tc(s, basics_test);
}

int main(void)
{
    return ilka_tests("trie_kv_test", &make_suite);
}
