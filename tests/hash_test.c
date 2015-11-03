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
        const char *cmd, struct ilka_hash_ret ret, int code, ilka_off_t off)
{
    ilka_assert(ret.code >= 0, "%s -> hash op raised an error", cmd);

    if (!ret.code)
        ilka_assert(code, "%s -> code mismatch: %d != %d", cmd, ret.code, code);
    else ilka_assert(code > 0, "%s -> code mismatch: %d != %d", cmd, ret.code, code);

    ilka_assert(ret.off == off, "%s -> off mismatch: %lu != %lu", cmd, ret.off, off);
}

#define check_ret(cmd, code, off) _check_ret(#cmd, cmd, code, off)


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

    uint64_t keys[4] = { 0, 1, 2, 3 };
    enum { n = sizeof(uint64_t) };

    check_ret(ilka_hash_get(h0, &keys[0], n), 1, 0);
    check_ret(ilka_hash_del(h0, &keys[0], n), 1, 0);
    check_ret(ilka_hash_xchg(h0, &keys[0], n, 10), 1, 0);

    ilka_hash_close(h1);
    ilka_hash_free(h0);

    ilka_exit(r, epoch);
    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, basic_test_st, true);
}

int main(void)
{
    return ilka_tests("hash_test", &make_suite);
}
