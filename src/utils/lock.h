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

inline bool slock_try_lock(ilka_slock *l)
{
    uint64_t old = 0;
    return ilka_atomic_cmp_xchg(l, &old, 1, morder_acquire);
}

inline void slock_unlock(ilka_slock *l)
{
    ilka_atomic_store(l, 0, morder_release);
}

inline bool slock_is_locked(ilka_slock *l)
{
    return ilka_atomic_load(l, morder_relaxed);
}


// -----------------------------------------------------------------------------
// spin-barrier
// -----------------------------------------------------------------------------

struct ilka_sbar
{
    uint64_t value;
    size_t target;
};

inline void sbar_init(struct ilka_sbar *b, size_t target)
{
    b->value = 0;
    b->target = target;
}

inline void sbar_wait(struct ilka_sbar *b)
{
    if (ilka_atomic_add_fetch(&b->value, 1, morder_acq_rel) == b->target)
        return;

    while (ilka_atomic_load(&b->value, morder_acquire) != b->target);
}
