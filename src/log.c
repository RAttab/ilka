/* log.c
   Rémi Attab (remi.attab@gmail.com), 20 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include <pthread.h>

// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static size_t ring_size = 1 << 12;

static size_t tick_inc()
{
    static size_t ticks = 0;
    return ilka_atomic_add_fetch(&ticks, 1, morder_seq_cst);
}


// -----------------------------------------------------------------------------
// ring log
// -----------------------------------------------------------------------------

struct log_msg
{
    size_t tid;
    size_t tick;
    const char *title;
    char msg[128];
};

struct log_ring
{
    struct log_ring *next;

    size_t pos;
    struct log_msg *data;
};

static pthread_once_t ring_once = PTHREAD_ONCE_INIT;
static pthread_key_t ring_key;

static ilka_slock ring_lock;
static struct log_ring *ring_head = NULL;

static void ring_init()
{
    slock_init(&ring_lock);

    if (pthread_key_create(&ring_key, NULL)) {
        ilka_fail_errno("unable to create pthread key");
        ilka_abort();
    }
}

static struct log_ring *ring_get()
{
    pthread_once(&ring_once, &ring_init);

    struct log_ring *ring = pthread_getspecific(ring_key);
    if (ring) return ring;

    ring = calloc(1, sizeof(struct log_ring));
    ring->data = calloc(ring_size, sizeof(struct log_msg));

    if (pthread_setspecific(ring_key, ring)) {
        ilka_fail_errno("unable to set log thread specific data");
        ilka_abort();
    }

    {
        slock_lock(&ring_lock);

        ring->next = ring_head;
        ring_head = ring;

        slock_unlock(&ring_lock);
    }

    return ring;
}

static void ring_log(const char *title, const char *fmt, va_list args)
{
    struct log_ring *ring = ring_get();

    struct log_msg *data = ilka_atomic_load(&ring->data, morder_relaxed);
    size_t i = ring->pos++ % ring_size;

    data[i].tick = tick_inc();
    data[i].tid = ilka_tid();
    data[i].title = title;
    (void) vsnprintf(data[i].msg, sizeof(data[i].msg), fmt, args);
}

static size_t ring_count()
{
    size_t n = 0;
    for (struct log_ring *ring = ring_head; ring; ring = ring->next) n++;
    return n;
}

static int ring_cmp(const void *lhs_, const void *rhs_)
{
    struct log_msg *const *lhs = lhs_;
    struct log_msg *const *rhs = rhs_;

    return ((ssize_t) (*rhs)->tick) - ((ssize_t) (*lhs)->tick);
}

static void ring_dump()
{
    slock_lock(&ring_lock);

    size_t rings_len = ring_count();
    struct log_msg **rings = alloca(rings_len * sizeof(struct log_msg *));

    size_t i = 0;
    for (struct log_ring *ring = ring_head; ring; ring = ring->next) {
        struct log_msg *new = calloc(ring_size, sizeof(struct log_msg));
        rings[i++] = ilka_atomic_xchg(&ring->data, new, morder_relaxed);
    }

    struct log_msg **msgs = alloca(rings_len * ring_size * sizeof(struct log_msg *));

    size_t k = 0;
    for (size_t i = 0; i < rings_len; ++i) {
        for (size_t j = 0; j < ring_size; ++j)
            msgs[k++] = &rings[i][j];
    }

    qsort(msgs, k, sizeof(struct log_msg *), &ring_cmp);

    for (size_t i = 0; i < k; ++i) {
        if (!msgs[i]->tick) continue;
        fprintf(stderr, "[%8lu] <%lu> %s: %s\n",
                msgs[i]->tick, msgs[i]->tid, msgs[i]->title, msgs[i]->msg);
    }

    for (size_t i = 0; i < rings_len; ++i) free(rings[i]);

    slock_unlock(&ring_lock);
}


// -----------------------------------------------------------------------------
// log
// -----------------------------------------------------------------------------

void ilka_log_impl(const char *title, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    if (ILKA_LOG_RING) ring_log(title, fmt, args);
    else {
        char *buf = alloca(1024);
        (void) vsnprintf(buf, 1024, fmt, args);

        fprintf(stderr, "[%8lu] <%lu> %s: %s\n", tick_inc(), ilka_tid(), title, buf);
    }
}

void ilka_log_dump()
{
    if (ILKA_LOG_RING) ring_dump();
}
