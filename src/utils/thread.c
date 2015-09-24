/* thread.c
   Rémi Attab (remi.attab@gmail.com), 20 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// cpus
// -----------------------------------------------------------------------------

size_t ilka_cpus()
{
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count != -1) return count;

    ilka_fail_errno("unable to call sysconf to get cpu count");
    ilka_abort();
}


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
