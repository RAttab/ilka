/* trie_kv_test.c
   Rémi Attab (remi.attab@gmail.com), 24 Jun 2014
   FreeBSD-style copyright and disclaimer apply

   Trie kv tests
*/

#include "check.h"
#include "trie/kv.h"
#include "utils/arch.h"


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
// encode / decode
// -----------------------------------------------------------------------------

#define check_kv(kv, exp)                       \
    do {                                        \
        ck_assert_int_eq(kv.key, exp.key);      \
        ck_assert_int_eq(kv.val, exp.val);      \
        ck_assert_int_eq(kv.state, exp.state);  \
    } while (0)

void
check_encode_info(
        const struct trie_kvs_encode_info *val,
        const struct trie_kvs_encode_info *exp)
{
    ck_assert_int_eq(val->bits, exp->bits);
    ck_assert_int_eq(val->shift, exp->shift);

    ck_assert_int_eq(val->prefix, exp->prefix);
    ck_assert_int_eq(val->prefix_bits, exp->prefix_bits);
    ck_assert_int_eq(val->prefix_shift, exp->prefix_shift);

    ck_assert_int_eq(val->padding, exp->padding);
}

void
check_info(
        const struct trie_kvs_info *val,
        const struct trie_kvs_info *exp)
{
    ck_assert_int_eq(val->size, exp->size);
    ck_assert_int_eq(val->key_len, exp->key_len);

    ck_assert_int_eq(val->buckets, exp->buckets);
    ck_assert_int_eq(val->is_abs_buckets, exp->is_abs_buckets);

    ck_assert_int_eq(val->value, exp->value);
    ck_assert_int_eq(val->value_bits, exp->value_bits);
    ck_assert_int_eq(val->value_shift, exp->value_shift);

    ck_assert_int_eq(val->value_offset, exp->value_offset);
    ck_assert_int_eq(val->state_offset, exp->state_offset);
    ck_assert_int_eq(val->bucket_offset, exp->bucket_offset);

    ck_assert_int_eq(val->state[0], exp->state[0]);
    ck_assert_int_eq(val->state[1], exp->state[1]);

    check_encode_info(&val->key, &exp->key);
    check_encode_info(&val->val, &exp->val);
}

void
check_encode_decode(
        size_t key_len,
        int has_value, uint64_t value,
        struct trie_kv *kvs, size_t kvs_n)
{
    struct trie_kvs_info info;
    uint8_t data[ILKA_CACHE_LINE] = { 0 };

    {
        int r = trie_kvs_info(&info, key_len, has_value, value, kvs, kvs_n);
        ck_assert(r);

        trie_kvs_encode(&info, kvs, kvs_n, data);
        printf("encoded="); trie_kvs_print_info(&info); printf("\n");

        ck_assert(info.size <= ILKA_CACHE_LINE);
        ck_assert_int_eq(info.key_len, key_len);
    }

    struct trie_kvs_info decoded_info;
    trie_kvs_decode(&decoded_info, data);
    printf("decoded="); trie_kvs_print_info(&decoded_info); printf("\n");

    check_info(&info, &decoded_info);

    ck_assert_int_eq(trie_kvs_count(&decoded_info), has_value + kvs_n);

    if (!has_value) ck_assert(!decoded_info.value_bits);
    else {
        ck_assert(decoded_info.value_bits);
        ck_assert_int_eq(decoded_info.value, value);
    }

    struct trie_kv decoded_kvs[decoded_info.buckets];
    size_t n = trie_kvs_extract(&info, decoded_kvs, decoded_info.buckets, data);
    ck_assert_int_eq(n, kvs_n);

    for (size_t i = 0; i < kvs_n; ++i) {
        struct trie_kv kv = trie_kvs_get(&decoded_info, kvs[i].key, data);

        check_kv(kv, kvs[i]);
        check_kv(decoded_kvs[i], kvs[i]);
    }
}

START_TEST(encode_decode_test)
{
    {
        struct trie_kv kvs[] = {
            { 0xF10, 0xA10, trie_kvs_state_terminal },
            { 0xF20, 0xA20, trie_kvs_state_branch },
            { 0xF30, 0xA30, trie_kvs_state_terminal },
        };

        printf("\n\n[ val ]==============================================\n\n");
        check_encode_decode(64, 1, 0x40, kvs, 3);

        printf("\n\n[ no_val ]===========================================\n\n");
        check_encode_decode(64, 0, 0, kvs, 3);
    }
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, kvs_test);
    ilka_tc(s, basics_test);
    ilka_tc(s, encode_decode_test);
}

int main(void)
{
    return ilka_tests("trie_kv_test", &make_suite);
}
