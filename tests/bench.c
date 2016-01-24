/* bench.c
   Rémi Attab (remi.attab@gmail.com), 23 Jan 2016
   FreeBSD-style copyright and disclaimer apply
*/

#include "bench.h"
#include "check.h"

#include <pthread.h>

// -----------------------------------------------------------------------------
// ilka_bench
// -----------------------------------------------------------------------------

struct ilka_bench
{
    struct ilka_sbar *barrier;

    bool started;
    struct timespec start;

    bool stopped;
    struct timespec stop;
};


void ilka_bench_start(struct ilka_bench *bench)
{
    ilka_assert(!bench->started, "bench started twice");
    bench->started = true;

    if (bench->barrier) sbar_wait(bench->barrier);

    if (ilka_unlikely(clock_gettime(CLOCK_REALTIME, &bench->start) < 0)) {
        ilka_fail_errno("unable to read realtime clock");
        ilka_abort();
    }
}

void ilka_bench_stop(struct ilka_bench *bench)
{
    ilka_no_opt_clobber(); // make sure everything is done before we stop.

    if (clock_gettime(CLOCK_REALTIME, &bench->stop) < 0) {
        ilka_fail_errno("unable to read realtime clock");
        ilka_abort();
    }

    bench->stopped = true;
}


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------


static double  sec() { return 1; }
static double msec() { return  sec() / 1000; }
static double usec() { return msec() / 1000; }
static double nsec() { return usec() / 1000; }

static double bench_elapsed(struct ilka_bench *bench)
{
    return
        (bench->stop.tv_sec - bench->start.tv_sec) +
        ((bench->stop.tv_nsec - bench->start.tv_nsec) * nsec());
}

static double run_bench(
        struct ilka_bench *bench,
        ilka_bench_fn_t fn,
        void *ctx,
        size_t id,
        size_t n)
{
    fn(bench, ctx, id, n);

    if (!bench->stopped) ilka_bench_stop(bench);
    ilka_assert(bench->started, "bench_start was not called");

    return bench_elapsed(bench);
}

typedef double (* bench_policy) (ilka_bench_fn_t, void *, size_t);

static void bench_runner(
        bench_policy pol, const char *title, ilka_bench_fn_t fn, void *ctx)
{
    const double duration = 1 * msec();
    const size_t iterations = 1000;

    // Also acts as the warmup for the bench.
    size_t n = 1;
    double elapsed = 0;
    for (; elapsed < duration; n *= 2) {
        elapsed = pol(fn, ctx, n);
        ilka_assert(n < 100UL * 1000 * 1000 * 1000, "bench is a noop");
    }

    double min = -1;
    for (size_t i = 0; i < iterations; ++i) {
        elapsed = pol(fn, ctx, n);
        if (min < 0.0 || elapsed < min) min = elapsed;
    }

    char scale = ' ';
    double scaled = ilka_scale_elapsed(elapsed / n, &scale);
    printf("bench: %-30s  %8lu  %6.2f%c\n", title, n, scaled, scale);
}


// -----------------------------------------------------------------------------
// bench st
// -----------------------------------------------------------------------------

static double bench_st_policy(ilka_bench_fn_t fn, void *ctx, size_t n)
{
    struct ilka_bench bench = { 0 };
    return run_bench(&bench, fn, ctx, 0, n);
}

void ilka_bench_st(const char *title, ilka_bench_fn_t fn, void *ctx)
{
    bench_runner(bench_st_policy, title, fn, ctx);
}


// -----------------------------------------------------------------------------
// bench mt
// -----------------------------------------------------------------------------

struct bench_mt
{
    size_t n;
    void *ctx;
    ilka_bench_fn_t fn;

    double *elapsed;
    struct ilka_sbar barrier;
};

static void bench_thread(size_t id, void *ctx)
{
    struct bench_mt *data = ctx;

    struct ilka_bench bench = { .barrier = &data->barrier };
    data->elapsed[id] = run_bench(&bench, data->fn, data->ctx, id, data->n);
}

static double bench_mt_policy(ilka_bench_fn_t fn, void *ctx, size_t n)
{
    size_t threads = ilka_cpus();
    double elapsed[threads];
    memset(elapsed, 0, sizeof(elapsed));

    struct bench_mt data = { .fn = fn, .ctx = ctx, .n = n, .elapsed = elapsed };
    sbar_init(&data.barrier, threads);

    ilka_run_threads(bench_thread, &data, threads);

    double min = -1.0;
    for (size_t i = 0; i < threads; ++i) {
        if (min < 0 || elapsed[i] < min)
            min = elapsed[i];
    }

    return min;
}

void ilka_bench_mt(const char *title, ilka_bench_fn_t fn, void *ctx)
{
    bench_runner(bench_mt_policy, title, fn, ctx);
}
