/* epoch_gc.c
   Rémi Attab (remi.attab@gmail.com), 06 Mar 2016
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// struct
// -----------------------------------------------------------------------------

typedef size_t (* epoch_gc_advance_t) (void *ep);

struct epoch_gc
{
    struct ilka_region *region;

    void *ep;
    ilka_slock *lock;
    epoch_gc_advance_t advance_fn;
    struct epoch_thread_list *threads;

    size_t freq_usec;
    pthread_t thread;

    size_t last_epoch;
};


// -----------------------------------------------------------------------------
// advance
// -----------------------------------------------------------------------------

static void epoch_gc_gather(struct epoch_gc *gc)
{
    ilka_assert(slock_is_locked(gc->lock), "epoch gc gather requires lock to be held");

    struct epoch_defer *defers = NULL;
    struct epoch_defer *defers_last = NULL;

    struct epoch_thread *thread = epoch_threads_head(gc->threads);
    while (thread) {

        struct epoch_defer *head = epoch_thread_defers(thread);
        if (!head) continue;

        if (!defers) defers = head;
        else defers_last->next = head;

        defers_last = head;
        while (defers_last->next) defers_last = defers_last->next;

        thread = thread->next;
    }

    epoch_thread_push_defers(epoch_thread_sentinel(gc->threads), defers, defers_last);
}

static void epoch_gc_reap(struct epoch_gc *gc, size_t epoch)
{
    ilka_assert(!slock_is_locked(gc->lock),
            "epoch gc reap should not be called with the lock held.");

    struct epoch_thread *sentinel = epoch_thread_sentinel(gc->threads);
    struct epoch_defer *head = epoch_thread_defers(sentinel);

    struct epoch_defer *node = head;
    struct epoch_defer *prev = NULL;

    while (node) {
        if (node->epoch >= epoch) {
            prev = node;
            node = node->next;
            continue;
        }

        if (node->fn) node->fn(node->data);
        else ilka_free_in(gc->region, node->off, node->len, node->area);

        if (prev) prev->next = node->next;
        else head = node->next;

        struct epoch_defer *next = node->next;
        free(node);
        node = next;
    }

    epoch_thread_push_defers(sentinel, head, prev);
}

static void epoch_gc_advance(struct epoch_gc *gc)
{
    size_t epoch;
    {
        slock_lock(gc->lock);

        epoch_gc_gather(gc);
        epoch = gc->advance_fn(gc->ep);

        slock_unlock(gc->lock);
    }

    if (epoch > gc->last_epoch)
        epoch_gc_reap(gc, epoch);

    gc->last_epoch = epoch;
}


// -----------------------------------------------------------------------------
// defer
// -----------------------------------------------------------------------------

static bool epoch_gc_defer_impl(struct epoch_gc *gc, struct epoch_defer *node)
{
    struct epoch_thread *thread = epoch_thread_get(gc->threads);
    if (!thread) return false;

    epoch_thread_push_defers(thread, node, NULL);
    return true;
}

static bool epoch_gc_defer(struct epoch_gc *gc, size_t epoch, void (*fn) (void *), void *data)
{
    if (!fn) {
        ilka_fail("invalid nil function pointer");
        return false;
    }

    struct epoch_defer *node = malloc(sizeof(struct epoch_defer));
    if (!node) {
        ilka_fail("out-of-memory for defer node: %lu", sizeof(struct epoch_defer));
        return false;
    }

    *node = (struct epoch_defer) { .epoch = epoch, .fn = fn, .data = data };
    if (!epoch_gc_defer_impl(gc, node)) goto fail;

    return true;

  fail:
    free(node);
    return false;
}

static bool epoch_gc_defer_free(
        struct epoch_gc *gc, size_t epoch, ilka_off_t off, size_t len, size_t area)
{
    if (!off) {
        ilka_fail("invalid nil offset");
        return false;
    }

    if (!len) {
        ilka_fail("invalid nil length");
        return false;
    }

    struct epoch_defer *node = malloc(sizeof(struct epoch_defer));
    if (!node) {
        ilka_fail("out-of-memory for defer node: %lu", sizeof(struct epoch_defer));
        return false;
    }

    *node = (struct epoch_defer) {.epoch = epoch, .off = off, .len = len, .area = area };
    if (!epoch_gc_defer_impl(gc, node)) goto fail;

    return true;

  fail:
    free(node);
    return false;
}


// -----------------------------------------------------------------------------
// thread
// -----------------------------------------------------------------------------

static void * epoch_gc_thread(void *data)
{
    struct epoch_gc *gc = data;

    while (true) {
        ilka_nsleep(gc->freq_usec * 1000);
        epoch_gc_advance(gc);
    }

    return NULL;
}


// -----------------------------------------------------------------------------
// init/close
// -----------------------------------------------------------------------------

static bool epoch_gc_init(struct epoch_gc *gc, struct ilka_options *options)
{
    gc->freq_usec = options->epoch_gc_freq_usec;
    if (!gc->freq_usec) gc->freq_usec = 1UL * 1000;

    int err = pthread_create(&gc->thread, NULL, epoch_gc_thread, gc);
    if (err) {
        ilka_fail_ierrno(err, "unable to pthread_create the epoch gc thread");
        return false;
    }

    return true;
}

static void epoch_gc_close(struct epoch_gc *gc)
{
    int err = pthread_cancel(gc->thread);
    if (err) {
        ilka_fail_ierrno(err, "unable to pthread_cancel the epoch gc_thread");
        ilka_abort();
    }

    err = pthread_join(gc->thread, NULL);
    if (err) {
        ilka_fail_ierrno(err, "unable to pthread_join the epoch gc_thread");
        ilka_abort();
    }
}
