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


// -----------------------------------------------------------------------------
// prof
// -----------------------------------------------------------------------------

static struct ilka_prof *prof_roots = NULL;
static __thread struct ilka_prof *prof_current = NULL;

struct ilka_prof_data ilka_prof_enter(struct ilka_prof *p, const char *title)
{
    if (!p->title && !ilka_atomic_xchg(&p->title, title, morder_relaxed)) {
        struct ilka_prof *old = ilka_atomic_load(&prof_roots, morder_relaxed);
        do {
            p->next = old;
        } while (!ilka_atomic_cmp_xchg(&prof_roots, &old, p, morder_release));
    }

    struct ilka_prof_data data = { .parent = prof_current };
    ilka_atomic_fetch_add(&p->hits, 1, morder_relaxed);

    if (prof_current) {
        for (size_t i = 0; i < ilka_prof_max_children; ++i) {

            struct ilka_prof *node =
                ilka_atomic_load(&prof_current->children[i].p, morder_relaxed);

          restart:

            if (!node) {
                if (!ilka_atomic_cmp_xchg(&prof_current->children[i].p, &node, p, morder_relaxed))
                    goto restart;
            }
            else if (node != p) continue;

            data.index = i;
            ilka_atomic_fetch_add(&prof_current->children[i].hits, 1, morder_relaxed);
            break;
        }
    }

    prof_current = p;

    data.start = ilka_now();
    return data;
}

void ilka_prof_exit(struct ilka_prof *p, struct ilka_prof_data *data)
{
    uint64_t elapsed = ilka_elapsed(&data->start) * 1000000000;

    ilka_atomic_fetch_add(&p->elapsed, elapsed, morder_relaxed);

    if (data->parent) {
        ilka_atomic_fetch_add(
                &data->parent->children[data->index].elapsed,
                elapsed, morder_relaxed);
    }

    prof_current = data->parent;
}

static void prof_print(
        const char *title,
        size_t hits, double hit_ratio,
        uint64_t elapsed, double elapsed_pct,
        const char *prefix)
{
    double latency = elapsed / hits;
    latency /= 1000000000;

    char buf[1024];
    size_t i = snprintf(buf, sizeof(buf),
            "%s%-40s %8lu (%10.2f) ", prefix, title, hits, hit_ratio);
    i += ilka_print_elapsed(buf + i, sizeof(buf) - i, latency);
    printf("%s (%6.2f%%)", buf, elapsed_pct * 100);
}

void ilka_prof_dump()
{
    struct ilka_prof *p = prof_roots;

    while (p) {
        prof_print(p->title, p->hits, 1, p->elapsed, 1.0, "");

        for (size_t i = 0; i < ilka_prof_max_children; ++i) {
            if (!p->children[i].p) continue;

            size_t hits = p->children[i].hits;
            uint64_t elapsed = p->children[i].elapsed;

            prof_print(p->children[i].p->title,
                    hits, hits / p->hits, elapsed,
                    (double) elapsed / p->elapsed, "    ");
        }

        p = p->next;
    }
}
