/* vec_test.c
   Rémi Attab (remi.attab@gmail.com), 11 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "vec.h"


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

#define check_vec(vec, start, end, value)               \
    for (size_t i = start; i < end; ++i) {              \
        const uint64_t *p = ilka_vec_read(vec, i, 1);   \
        ck_assert_int_eq(*p, value);                    \
    }

#define check_vec_arr(vec, start, end, values)                  \
    for (size_t i = 0; i < (end - start); ++i) {                \
        const uint64_t *p = ilka_vec_read(vec, start + i, 1);   \
        ck_assert_int_eq(*p, (values)[i]);                      \
    }

void write_vec(struct ilka_vec *v, size_t start, size_t end, uint64_t value)
{
    for (size_t i = start; i < end; ++i) {
        uint64_t *p = ilka_vec_write(v, i, 1);
        *p = value;
    }
}

void print_vec(struct ilka_vec *v, const char *title)
{
    enum { n = 1024 };
    char buf[n];

    size_t i = snprintf(buf, n, "[ ");
    for (size_t j = 0; j < ilka_vec_len(v); ++j) {
        const uint64_t *p = ilka_vec_read(v, j, 1);
        i += snprintf(buf + i, n - i, "%lu:%p ", j, (void *) *p);
    }
    snprintf(buf + i, n - i, "]");

    ilka_log(title, "vec=%s", buf);
}

// -----------------------------------------------------------------------------
// basics_test
// -----------------------------------------------------------------------------

START_TEST(basics_test)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct ilka_vec *v0 = ilka_vec_alloc(r, sizeof(uint64_t), 0);
    struct ilka_vec *v1 = ilka_vec_open(r, ilka_vec_off(v0));

    ck_assert(!ilka_vec_len(v0));
    ck_assert_int_eq(ilka_vec_len(v0), ilka_vec_len(v1));

    uint64_t value = 10;
    if (!ilka_vec_append(v0, &value, 1)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v0), 1);
    ck_assert_int_eq(ilka_vec_len(v1), 1);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v0, 0, 1)), value);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v1, 0, 1)), value);


    *((uint64_t *) ilka_vec_write(v1, 0, 1)) = value = 20;
    ck_assert_int_eq(ilka_vec_len(v0), 1);
    ck_assert_int_eq(ilka_vec_len(v1), 1);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v0, 0, 1)), value);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v1, 0, 1)), value);

    uint64_t values[4] = { 11, 22, 33, 44 };
    if (!ilka_vec_insert(v0, values, 0, 4)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v1), 5);
    check_vec_arr(v1, 0, 4, values);
    check_vec(v1, 4, 1, value);

    if (!ilka_vec_remove(v1, 2, 2)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v0), 3);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v0, 0, 1)), values[0]);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v0, 1, 1)), values[1]);
    ck_assert_int_eq(*((uint64_t *)ilka_vec_read(v0, 2, 1)), value);

    if (!ilka_vec_close(v1)) ilka_abort();
    if (!ilka_vec_free(v0)) ilka_abort();
    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// resize
// -----------------------------------------------------------------------------


START_TEST(resize_test)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct ilka_vec *v = ilka_vec_alloc(r, sizeof(uint64_t), 9);
    ck_assert_int_eq(ilka_vec_len(v), 0);
    ck_assert_int_eq(ilka_vec_cap(v), 16);

    if (!ilka_vec_reserve(v, 32)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v), 0);
    ck_assert_int_eq(ilka_vec_cap(v), 32);

    if (!ilka_vec_reserve(v, 4)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v), 0);
    ck_assert_int_eq(ilka_vec_cap(v), 32);

    if (!ilka_vec_resize(v, 15)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v), 15);
    ck_assert_int_eq(ilka_vec_cap(v), 32);
    check_vec(v, 0, ilka_vec_len(v), 0);

    write_vec(v, 0, ilka_vec_len(v), -1UL);

    if (!ilka_vec_resize(v, 8)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v), 8);
    ck_assert_int_eq(ilka_vec_cap(v), 16);

    if (!ilka_vec_resize(v, 33)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v), 33);
    ck_assert_int_eq(ilka_vec_cap(v), 64);

    check_vec(v, 0, 8, -1UL);
    check_vec(v, 8, ilka_vec_len(v), 0);

    if (!ilka_vec_resize(v, 0)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v), 0);
    ck_assert_int_eq(ilka_vec_cap(v), 0);

    if (!ilka_vec_resize(v, 1)) ilka_abort();
    ck_assert_int_eq(ilka_vec_len(v), 1);
    ck_assert_int_eq(ilka_vec_cap(v), 1);
    check_vec(v, 0, ilka_vec_len(v), 0);
}
END_TEST


// -----------------------------------------------------------------------------
// append
// -----------------------------------------------------------------------------

START_TEST(append_test)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);
    struct ilka_vec *v = ilka_vec_alloc(r, sizeof(uint64_t), 0);

    uint64_t values[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    ilka_vec_append(v, values, 1);
    ck_assert_int_eq(ilka_vec_len(v), 1);
    ck_assert_int_eq(ilka_vec_cap(v), 1);
    check_vec_arr(v, 0, ilka_vec_len(v), values);

    ilka_vec_append(v, values, 8);
    ck_assert_int_eq(ilka_vec_len(v), 9);
    ck_assert_int_eq(ilka_vec_cap(v), 16);
    check_vec_arr(v, 0, 1, values);
    check_vec_arr(v, 1, ilka_vec_len(v), values);

    ilka_vec_append(v, values, 4);
    ck_assert_int_eq(ilka_vec_len(v), 13);
    ck_assert_int_eq(ilka_vec_cap(v), 16);
    check_vec_arr(v, 0, 1, values);
    check_vec_arr(v, 1, 9, values);
    check_vec_arr(v, 9, ilka_vec_len(v), values);
}
END_TEST


// -----------------------------------------------------------------------------
// insert
// -----------------------------------------------------------------------------

START_TEST(insert_test)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);
    struct ilka_vec *v = ilka_vec_alloc(r, sizeof(uint64_t), 0);

    uint64_t values[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    ilka_vec_insert(v, values, 0, 8);
    ck_assert_int_eq(ilka_vec_len(v), 8);
    check_vec_arr(v, 0, ilka_vec_len(v), values);

    ilka_vec_insert(v, values, 4, 4);
    ck_assert_int_eq(ilka_vec_len(v), 12);
    check_vec_arr(v, 0, 4, values);
    check_vec_arr(v, 4, 8, values);
    check_vec_arr(v, 8, ilka_vec_len(v), &values[4]);

    ilka_vec_insert(v, values, ilka_vec_len(v), 4);
    ck_assert_int_eq(ilka_vec_len(v), 16);
    check_vec_arr(v, 0, 4, values);
    check_vec_arr(v, 4, 8, values);
    check_vec_arr(v, 8, 12, &values[4]);
    check_vec_arr(v, 12, ilka_vec_len(v), values);

    ilka_vec_insert(v, values, 0, 4);
    ck_assert_int_eq(ilka_vec_len(v), 20);
    check_vec_arr(v, 0, 4, values);
    check_vec_arr(v, 4, 8, values);
    check_vec_arr(v, 8, 12, values);
    check_vec_arr(v, 12, 16, &values[4]);
    check_vec_arr(v, 16, ilka_vec_len(v), values);
}
END_TEST


// -----------------------------------------------------------------------------
// remove
// -----------------------------------------------------------------------------

START_TEST(remove_test)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);
    struct ilka_vec *v = ilka_vec_alloc(r, sizeof(uint64_t), 0);

    uint64_t values[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    ilka_vec_append(v, values, 8);
    ilka_vec_append(v, values, 8);
    ck_assert_int_eq(ilka_vec_len(v), 16);
    ck_assert_int_eq(ilka_vec_cap(v), 16);

    ilka_vec_remove(v, 6, 4);
    ck_assert_int_eq(ilka_vec_len(v), 12);
    ck_assert_int_eq(ilka_vec_cap(v), 16);
    check_vec_arr(v, 0, 6, values);
    check_vec_arr(v, 6, ilka_vec_len(v), &values[2]);

    ilka_vec_remove(v, 8, 4);
    ck_assert_int_eq(ilka_vec_len(v), 8);
    ck_assert_int_eq(ilka_vec_cap(v), 16);
    check_vec_arr(v, 0, 6, values);
    check_vec_arr(v, 6, ilka_vec_len(v), &values[2]);

    ilka_vec_remove(v, 0, 4);
    ck_assert_int_eq(ilka_vec_len(v), 4);
    ck_assert_int_eq(ilka_vec_cap(v), 8);
    check_vec_arr(v, 0, 2, &values[4]);
    check_vec_arr(v, 2, ilka_vec_len(v), &values[2]);

    ilka_vec_remove(v, 0, 4);
    ck_assert_int_eq(ilka_vec_len(v), 0);
    ck_assert_int_eq(ilka_vec_cap(v), 0);
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, basics_test, true);
    ilka_tc(s, resize_test, true);
    ilka_tc(s, append_test, true);
    ilka_tc(s, insert_test, true);
    ilka_tc(s, remove_test, true);
}

int main(void)
{
    return ilka_tests("vec_test", &make_suite);
}
