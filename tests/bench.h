/* bench.h
   Rémi Attab (remi.attab@gmail.com), 23 Jan 2016
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

#include "ilka.h"


// -----------------------------------------------------------------------------
// bench
// -----------------------------------------------------------------------------

struct ilka_bench;
typedef void (* ilka_bench_fn_t) (
        struct ilka_bench *, void * ctx, size_t id, size_t n);

void* ilka_bench_setup(struct ilka_bench *, void *data);
void ilka_bench_start(struct ilka_bench *);
void ilka_bench_stop(struct ilka_bench *);

void ilka_bench_st(const char *title, ilka_bench_fn_t fn, void *ctx);
void ilka_bench_mt(const char *title, ilka_bench_fn_t fn, void *ctx);
