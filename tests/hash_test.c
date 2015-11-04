/* hash_test.c
   Rémi Attab (remi.attab@gmail.com), 03 Nov 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "struct/hash.h"

// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static void _check_ret(
        const char *cmd, struct ilka_hash_ret ret, bool code, ilka_off_t off)
{
    ilka_assert(ret.code >= 0, "%s -> hash op raised an error", cmd);

    if (ret.code)
        ilka_assert(!code, "%s -> code mismatch: %d != %d", cmd, ret.code, code);
    else ilka_assert(code, "%s -> code mismatch: %d != %d", cmd, ret.code, code);

    ilka_assert(ret.off == off, "%s -> off mismatch: %lu != %lu", cmd, ret.off, off);
}

#define check_ret(cmd, code, off) \
    _check_ret(ilka_stringify(__LINE__) ":" #cmd, cmd, code, off)


// -----------------------------------------------------------------------------
// basics
// -----------------------------------------------------------------------------

START_TEST(basic_test_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);
    size_t epoch = ilka_enter(r);

    struct ilka_hash *h0 = ilka_hash_alloc(r);
    struct ilka_hash *h1 = ilka_hash_open(r, ilka_hash_off(h0));

    ck_assert_int_eq(ilka_hash_len(h0), 0);
    ck_assert_int_eq(ilka_hash_len(h1), 0);

    ck_assert_int_eq(ilka_hash_cap(h0), 0);
    ck_assert_int_eq(ilka_hash_cap(h1), 0);

    enum {
        key_count = 128,
        klen = sizeof(uint64_t)
    };

    uint64_t keys[key_count];
    for (size_t i = 0; i < key_count; ++i) keys[i] = i;

    for (size_t round = 0; round < 8; ++round) {
        for (size_t i = 0; i < key_count; ++i) {
            check_ret(ilka_hash_get(h0, &keys[i], klen), false, 0);
            check_ret(ilka_hash_del(h0, &keys[i], klen), false, 0);
            check_ret(ilka_hash_xchg(h0, &keys[i], klen, 10), false, 0);

            check_ret(ilka_hash_put(h0, &keys[i], klen, 10), true, 0);
            check_ret(ilka_hash_put(h1, &keys[i], klen, 100), false, 10);

            check_ret(ilka_hash_get(h0, &keys[i], klen), true, 10);
            check_ret(ilka_hash_get(h1, &keys[i], klen), true, 10);

            check_ret(ilka_hash_xchg(h0, &keys[i], klen, 20), true, 10);
            check_ret(ilka_hash_cmp_xchg(h0, &keys[i], klen, 10, 30), false, 20);
            check_ret(ilka_hash_cmp_xchg(h0, &keys[i], klen, 20, 40), true, 20);
        }

        ck_assert_int_eq(ilka_hash_len(h0), key_count);
        for (size_t i = 0; i < key_count; ++i)
            check_ret(ilka_hash_get(h1, &keys[i], klen), true, 40);

        for (size_t i = 0; i < key_count; ++i) {
            check_ret(ilka_hash_cmp_del(h0, &keys[i], klen, 10), false, 40);
            check_ret(ilka_hash_cmp_del(h0, &keys[i], klen, 40), true, 40);

            check_ret(ilka_hash_get(h0, &keys[i], klen), false, 0);
            check_ret(ilka_hash_del(h0, &keys[i], klen), false, 0);
            check_ret(ilka_hash_xchg(h0, &keys[i], klen, 10), false, 0);
        }

    }
    ilka_log("hash.basic.test", "final cap: %lu", ilka_hash_cap(h1));

    ilka_hash_close(h1);
    ilka_hash_free(h0);

    ilka_exit(r, epoch);
    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// split test
// -----------------------------------------------------------------------------

struct hash_test
{
    struct ilka_region *r;
    struct ilka_hash *h;

    size_t runs;
};

void run_split_test(size_t id, void *data)
{
    struct hash_test *t = data;

    enum { nkey = 16, klen = sizeof(uint64_t) };
    uint64_t keys[nkey];

    for (size_t i = 0; i < nkey; ++i)
        keys[i] = id << 32 | i;

    for (size_t run = 0; run < t->runs; ++run) {
        ilka_epoch_t epoch = ilka_enter(t->r);

        for (size_t i = 0; i < nkey; ++i)
            check_ret(ilka_hash_get(t->h, &keys[i], klen), false, 0);

        for (size_t i = 0; i < nkey; ++i)
            check_ret(ilka_hash_put(t->h, &keys[i], klen, 10), true, 0);

        for (size_t i = 0; i < nkey; ++i)
            check_ret(ilka_hash_get(t->h, &keys[i], klen), true, 10);

        for (size_t i = 0; i < nkey; ++i)
            check_ret(ilka_hash_xchg(t->h, &keys[i], klen, 20), true, 10);

        for (size_t i = 0; i < nkey; ++i)
            check_ret(ilka_hash_del(t->h, &keys[i], klen), true, 20);

        ilka_exit(t->r, epoch);
    }
}

START_TEST(split_test_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);
    struct ilka_hash *h = ilka_hash_alloc(r);

    struct hash_test data = { .r = r, .h = h, .runs = 1000 };
    ilka_run_threads(run_split_test, &data);

    ilka_hash_free(h);
    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, basic_test_st, true);
    ilka_tc(s, split_test_mt, false);
}

int main(void)
{
    return ilka_tests("hash_test", &make_suite);
}
