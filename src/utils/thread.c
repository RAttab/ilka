/* thread.c
   Rémi Attab (remi.attab@gmail.com), 20 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "thread.h"
#include "atomic.h"


// -----------------------------------------------------------------------------
// tid
// -----------------------------------------------------------------------------

static size_t tid_counter = 0;
static __thread size_t tid = 0;

size_t ilka_tid()
{
    if (!tid) tid = ilka_atomic_add_fetch(&tid_counter, 1, morder_relaxed);
    return tid;
}
