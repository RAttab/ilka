/* atomic.h
   Rémi Attab (remi.attab@gmail.com), 10 May 2014
   FreeBSD-style copyright and disclaimer apply

   Atomic utilities

   \todo replace by the standard library when available.

*/

#pragma once

// -----------------------------------------------------------------------------
// memory model
// -----------------------------------------------------------------------------

enum morder
{
    morder_relaxed = __ATOMIC_RELAXED,
    morder_consume = __ATOMIC_CONSUME,
    morder_acquire = __ATOMIC_ACQUIRE,
    morder_release = __ATOMIC_RELEASE,
    morder_acq_rel = __ATOMIC_ACQ_REL,
    morder_seq_cst = __ATOMIC_SEQ_CST,
};

// -----------------------------------------------------------------------------
// atomic
// -----------------------------------------------------------------------------

#define ilka_atomic_fence(m) __atomic_thread_fence(m)

#define ilka_atomic_load(p, m)     __atomic_load_n(p, m)
#define ilka_atomic_store(p, v, m) __atomic_store_n(p, v, m)
#define ilka_atomic_xchg(p, v, m)  __atomic_exchange_n(p, v, m)
#define ilka_atomic_cmp_xchg(p, exp, val, m)                            \
    __atomic_compare_exchange_n(p, exp, val, false, m, morder_relaxed)

#define ilka_atomic_fetch_add(p, v, m)  __atomic_fetch_add(p, v, m)
#define ilka_atomic_fetch_sub(p, v, m)  __atomic_fetch_sub(p, v, m)
#define ilka_atomic_fetch_or(p, v, m)   __atomic_fetch_or(p, v, m)
#define ilka_atomic_fetch_and(p, v, m)  __atomic_fetch_and(p, v, m)
#define ilka_atomic_fetch_xor(p, v, m)  __atomic_fetch_xor(p, v, m)
#define ilka_atomic_fetch_nand(p, v, m) __atomic_fetch_nand(p, v, m)

#define ilka_atomic_add_fetch(p, v, m)  __atomic_add_fetch(p, v, m)
#define ilka_atomic_sub_fetch(p, v, m)  __atomic_sub_fetch(p, v, m)
#define ilka_atomic_or_fetch(p, v, m)   __atomic_or_fetch(p, v, m)
#define ilka_atomic_and_fetch(p, v, m)  __atomic_and_fetch(p, v, m)
#define ilka_atomic_xor_fetch(p, v, m)  __atomic_xor_fetch(p, v, m)
#define ilka_atomic_nand_fetch(p, v, m) __atomic_nand_fetch(p, v, m)

#define ilka_atomic_is_lockfree(t) __atomic_is_always_lock_free(sizeof(t), 0)
