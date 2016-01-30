/* epoch_bench.c
   Rémi Attab (remi.attab@gmail.com), 30 Jan 2016
   FreeBSD-style copyright and disclaimer apply
*/


#include "check.h"
#include "bench.h"
#include <stdlib.h>


// -----------------------------------------------------------------------------
// bench
// -----------------------------------------------------------------------------

struct epoch_bench
{
    struct ilka_region *r;
};

void run_enter_exit_bench(struct ilka_bench *b, void *data, size_t id, size_t n)
{
    (void) id;
    struct epoch_bench *t = data;

    ilka_bench_start(b);

    for (size_t i = 0; i < n; ++i) {
        ilka_enter(t->r);
        ilka_exit(t->r);
    }
}

START_TEST(enter_exit_bench_st)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    struct epoch_bench data = { .r = r };
    ilka_bench_st("enter_exit_bench_st", run_enter_exit_bench, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST

START_TEST(enter_exit_bench_mt)
{
    struct ilka_options options = {
        .open = true,
        .create = true,
        .epoch_gc_freq_usec = 1,
    };
    struct ilka_region *r = ilka_open("blah", &options);

    struct epoch_bench data = { .r = r };
    ilka_bench_mt("enter_exit_bench_mt", run_enter_exit_bench, &data);

    if (!ilka_close(r)) ilka_abort();
}
END_TEST



// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, enter_exit_bench_st, true);
    ilka_tc(s, enter_exit_bench_mt, true);
}

int main(void)
{
    return ilka_tests("epoch_bench", &make_suite);
}
