/* lock.h
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

// -----------------------------------------------------------------------------
// spin-lock
// -----------------------------------------------------------------------------

typedef uint64_t ilka_slock;

inline void slock_init(ilka_slock *l) { *l = 0; }

inline void slock_lock(ilka_slock *l)
{
    uint64_t old;
    do {
        old = ilka_atomic_load(l, morder_relaxed);
    } while (old || !ilka_atomic_cmp_xchg(l, &old, 1, morder_acquire));
}

inline void slock_unlock(ilka_slock *l)
{
    ilka_atomic_store(l, 0, morder_release);
}
