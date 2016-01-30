/* hash_bench.c
   Rémi Attab (remi.attab@gmail.com), 30 Jan 2016
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "bench.h"
#include "struct/hash.h"



// -----------------------------------------------------------------------------
// bench
// -----------------------------------------------------------------------------

struct hash_bench
{
    struct ilka_region *r;
    struct ilka_hash *hash;
    size_t keys;
};


void run_get_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    (void) id;
    struct hash_bench *t = data;

    ilka_enter(t->r);
    ilka_bench_start(b);

    for (uint64_t i = 0; i < n; ++i) {
        uint64_t key = i % t->keys;
        ilka_hash_get(t->hash, &key, sizeof(i));
    }

    ilka_bench_stop(b);
    ilka_exit(t->r);
}

START_TEST(get_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    enum { keys = 100000 };
    struct ilka_hash *hash = ilka_hash_alloc(r);
    ilka_hash_reserve(hash, keys);

    for (uint64_t i = 0; i < keys; ++i)
        ilka_hash_put(hash, &i, sizeof(i), 1);

    struct hash_bench tdata = { .hash = hash, .r = r, .keys = keys };
    ilka_bench_st("get_bench_st", run_get_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


START_TEST(get_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    enum { keys = 100000 };
    struct ilka_hash *hash = ilka_hash_alloc(r);
    ilka_hash_reserve(hash, keys);

    for (uint64_t i = 0; i < keys; ++i)
        ilka_hash_put(hash, &i, sizeof(i), 1);

    struct hash_bench tdata = { .hash = hash, .r = r, .keys = keys };
    ilka_bench_mt("get_bench_mt", run_get_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


void run_insert_only_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    struct hash_bench *t = data;
    if (!id) {
        if (t->hash) ilka_hash_free(t->hash);
        t->hash = ilka_hash_alloc(t->r);
    }

    ilka_bench_start(b);

    for (size_t i = 0; i < n; ++i) {
        ilka_enter(t->r);

        uint64_t value = (id << 32) | i;
        ilka_hash_put(t->hash, &value, sizeof(value), 1);

        ilka_exit(t->r);
    }
}

START_TEST(insert_only_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct hash_bench tdata = { .r = r };
    ilka_bench_st("insert_only_bench_st", run_insert_only_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(insert_only_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct hash_bench tdata = { .r = r };
    ilka_bench_mt("insert_only_bench_mt", run_insert_only_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


void run_rolling_insert_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    struct hash_bench *t = data;
    if (!id) {
        if (t->hash) ilka_hash_free(t->hash);
        t->hash = ilka_hash_alloc(t->r);
    }

    ilka_bench_start(b);

    enum { runs = 100 };

    for (size_t run = 0; run < runs; ++run) {
        ilka_enter(t->r);

        for (size_t i = 0; i < n / runs; ++i) {
            uint64_t value = (id << 32) | i;

            if (run % 2 == 0)
                ilka_hash_put(t->hash, &value, sizeof(value), 1);
            else ilka_hash_del(t->hash, &value, sizeof(value));

        }

        ilka_exit(t->r);
    }
}

START_TEST(rolling_insert_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct hash_bench tdata = { .r = r };
    ilka_bench_st("rolling_insert_bench_st", run_rolling_insert_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(rolling_insert_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct hash_bench tdata = { .r = r };
    ilka_bench_mt("rolling_insert_bench_mt", run_rolling_insert_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


void run_cmp_xchg_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    struct hash_bench *t = data;

    if (!id) {
        if (t->hash) ilka_hash_free(t->hash);
        t->hash = ilka_hash_alloc(t->r);
        ilka_hash_reserve(t->hash, 128);
    }

    (void) ilka_bench_setup(b, NULL);

    uint64_t value = id << 32;
    ilka_hash_put(t->hash, &value, sizeof(value), 1);

    ilka_bench_start(b);

    for (size_t i = 0; i < n; ++i) {
        ilka_enter(t->r);

        ilka_hash_cmp_xchg(t->hash, &value, sizeof(value), 1, 1);

        ilka_exit(t->r);
    }
}

START_TEST(cmp_xchg_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct hash_bench tdata = { .r = r };
    ilka_bench_st("cmp_xchg_bench_st", run_cmp_xchg_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(cmp_xchg_bench_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct hash_bench tdata = { .r = r };
    ilka_bench_mt("cmp_xchg_bench_mt", run_cmp_xchg_bench, &tdata);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, get_bench_st, true);
    ilka_tc(s, get_bench_mt, true);
    ilka_tc(s, insert_only_bench_st, true);
    ilka_tc_timeout(s, insert_only_bench_mt, 60, true);
    ilka_tc(s, rolling_insert_bench_st, true);
    ilka_tc_timeout(s, rolling_insert_bench_mt, 60, true);
    ilka_tc(s, cmp_xchg_bench_st, true);
    ilka_tc_timeout(s, cmp_xchg_bench_mt, 60, true);
}

int main(void)
{
    return ilka_tests("hash_bench", &make_suite);
}
