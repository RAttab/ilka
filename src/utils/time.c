/* time.c
   Rémi Attab (remi.attab@gmail.com), 22 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// timer
// -----------------------------------------------------------------------------

struct timespec ilka_now()
{
    struct timespec ts;
    if (!clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) return ts;

    ilka_fail_errno("unable to read monotonic clock");
    ilka_abort();
}

double ilka_elapsed(struct timespec *start)
{
    struct timespec end = ilka_now();

    double secs = end.tv_sec - start->tv_sec;

    int64_t nsecs = end.tv_nsec - start->tv_nsec;
    if (nsecs < 0) nsecs *= -1;

    return secs + nsecs * 0.000000001;
}

size_t ilka_print_elapsed(char *buf, size_t n, double t)
{
    static const char scale[] = "smun";

    size_t i = 0;
    for (i = 0; i < sizeof(scale); ++i) {
        if (t >= 1.0) break;
        t *= 1000.0;
    }

    return snprintf(buf, n, "%7.3f%c", t, scale[i]);
}


// -----------------------------------------------------------------------------
// sleep
// -----------------------------------------------------------------------------

bool ilka_nsleep(uint64_t nanos)
{
    struct timespec t = ilka_now();

    t.tv_nsec += nanos;
    if (t.tv_nsec >= 1000000000) {
        t.tv_sec += t.tv_nsec / 1000000000;
        t.tv_nsec = t.tv_nsec % 1000000000;
    }

    while (true) {
        int ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
        if (!ret) return true;
        if (errno == EINTR) continue;

        ilka_fail_errno("unable to sleep via clock_nanosleep");
        return false;
    }
}
