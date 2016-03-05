/* epoch_threads.c
   Rémi Attab (remi.attab@gmail.com), 06 Mar 2016
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// struct
// -----------------------------------------------------------------------------

struct epoch_thread_list;

struct epoch_thread
{
    struct epoch_thread_list *threads;

    size_t epoch;
    struct epoch_defer *defers;

    struct epoch_thread *next;
    struct epoch_thread *prev;
};

struct epoch_thread_list
{
    pthread_key_t key;

    ilka_slock *lock;
    struct epoch_thread *head;
    struct epoch_thread *sentinel;
};


// -----------------------------------------------------------------------------
// defers
// -----------------------------------------------------------------------------

static struct epoch_defer * epoch_thread_defers(struct epoch_thread *thread)
{
    return ilka_atomic_xchg(&thread->defers, 0, morder_acquire);
}

static void epoch_thread_push_defers(
        struct epoch_thread *thread,
        struct epoch_defer *defers,
        struct epoch_defer *defers_last)
{
    if (!defers) return;

    if (!defers_last) {
        defers_last = defers;
        while (defers_last->next) defers_last = defers_last->next;
    }

    struct epoch_defer *old = ilka_atomic_load(&thread->defers, morder_acquire);
    do {
        defers_last->next = old;
    } while (!ilka_atomic_cmp_xchg(&thread->defers, &old, defers, morder_release));
}


// -----------------------------------------------------------------------------
// access
// -----------------------------------------------------------------------------

static struct epoch_thread * epoch_threads_head(struct epoch_thread_list *threads)
{
    ilka_assert(!slock_try_lock(threads->lock), "accessing thread list without lock");
    return threads->head;
}

static struct epoch_thread * epoch_thread_sentinel(struct epoch_thread_list *threads)
{
    return threads->sentinel;
}

static struct epoch_thread * epoch_thread_get(struct epoch_thread_list *threads)
{
    struct epoch_thread *thread = pthread_getspecific(threads->key);
    if (thread) return thread;

    thread = calloc(1, sizeof(struct epoch_thread));
    if (!thread) {
        ilka_fail("out-of-memory for epoch thread: %lu", sizeof(struct epoch_thread));
        return NULL;
    }

    thread->threads = threads;
    pthread_setspecific(threads->key, thread);

    {
        slock_lock(threads->lock);

        thread->next = threads->head;
        threads->head->prev = thread;
        threads->head = thread;

        slock_unlock(threads->lock);
    }

    return thread;
}

static void epoch_thread_remove(void *data)
{
    struct epoch_thread *thread = data;
    struct epoch_thread_list *threads = thread->threads;

    ilka_assert(!thread->epoch, "thread exiting while in epoch");

    struct epoch_defer *defers = epoch_thread_defers(thread);

    {
        slock_lock(thread->threads->lock);

        if (thread->next) thread->next->prev = thread->prev;
        if (thread->prev) thread->prev->next = thread->next;
        else threads->head = thread->next;
        free(thread);

        slock_unlock(threads->lock);
    }

    epoch_thread_push_defers(threads->sentinel, defers, NULL);
}


// -----------------------------------------------------------------------------
// init/close
// -----------------------------------------------------------------------------

static bool epoch_threads_init(struct epoch_thread_list *threads, ilka_slock *lock)
{
    threads->lock = lock;

    if (pthread_key_create(&threads->key, epoch_thread_remove)) {
        ilka_fail_errno("unable to create pthread key");
        goto fail_key;
    }

    threads->sentinel = calloc(1, sizeof(struct epoch_thread));
    if (!threads->sentinel) {
        ilka_fail("out-of-memory for epoch sentinel");
        goto fail_sentinel;
    }
    threads->sentinel->threads = threads;
    threads->head = threads->sentinel;

    return true;

    free(threads->sentinel);
  fail_sentinel:

    pthread_key_delete(threads->key);
  fail_key:

    return false;
}


static void epoch_threads_close(struct epoch_thread_list *threads)
{
    while (threads->head) {
        struct epoch_thread *thread = threads->head;
        threads->head = thread->next;

        ilka_assert(!thread->epoch,
                "closing with thread in region: thread=%p, epoch=%lu",
                (void *) thread, thread->epoch);

        ilka_assert(!thread->defers,
                "closing with pending defer work: thread=%p", (void *) thread);

        free(thread);
    }

    pthread_key_delete(threads->key);
}
