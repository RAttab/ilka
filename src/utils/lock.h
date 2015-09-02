/* lock.h
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

#include "atomic.h"

// -----------------------------------------------------------------------------
// spin-lock
// -----------------------------------------------------------------------------

typedef uint64_t ilka_slock;

void slock_init(ilka_slock *l) { *l = 0; }

void slock_lock(ilka_slock *l)
{
    uint64_t old = ilka_atomic_load(l, memory_order_relaxed);
    while (old || ilka_atomic_cmp_xchg(l, &old, 1, memory_order_acquire) == 0);
}

void slock_unlock(ilka_slock *l)
{
    ilka_atomic_store(l, 0, memory_order_release);
}
