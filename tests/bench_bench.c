/* bench_bench.c
   Rémi Attab (remi.attab@gmail.com), 24 Jan 2016
   FreeBSD-style copyright and disclaimer apply

   control tests for the bench framework
*/

#include "check.h"
#include "bench.h"


// -----------------------------------------------------------------------------
// harness bench
// -----------------------------------------------------------------------------

void run_harness_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    (void) id, (void) data;
    ilka_bench_start(b);

    for (size_t i = 0; i < n; ++i) ilka_no_opt();
}

START_TEST(harness_bench_st)
{
    ilka_bench_st("harness_bench_st", run_harness_bench, NULL);
}
END_TEST

START_TEST(harness_bench_mt)
{
    ilka_bench_mt("harness_bench_mt", run_harness_bench, NULL);
}
END_TEST


// -----------------------------------------------------------------------------
// sleep bench
// -----------------------------------------------------------------------------


void run_sleep_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    (void) id, (void) data;
    ilka_bench_start(b);

    ilka_nsleep(n);
}

START_TEST(sleep_bench_st)
{
    ilka_bench_st("sleep_bench_st", run_sleep_bench, NULL);
}
END_TEST

START_TEST(sleep_bench_mt)
{
    ilka_bench_mt("sleep_bench_mt", run_sleep_bench, NULL);
}
END_TEST



// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, harness_bench_st, true);
    ilka_tc(s, harness_bench_mt, true);
    ilka_tc(s, sleep_bench_st, true);
    ilka_tc(s, sleep_bench_mt, true);
}

int main(void)
{
    return ilka_tests("bench_test", &make_suite);
}
