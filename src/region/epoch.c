/* epoch.c
   Rémi Attab (remi.attab@gmail.com), 26 Nov 2015
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// epoch
// -----------------------------------------------------------------------------

struct epoch_defer
{
    void *data;
    void (*fn) (void *);

    size_t len;
    ilka_off_t off;

    struct epoch_defer *next;
};

struct epoch_thread
{
    struct ilka_epoch *ep;

    size_t epoch;
    struct epoch_defer *defers[2];

    struct epoch_thread *next;
    struct epoch_thread *prev;
};

struct ilka_epoch
{
    struct ilka_region *region;
    pthread_key_t key;

    size_t epoch;
    size_t world_lock;

    ilka_slock lock;
    struct epoch_thread *threads;
    struct epoch_thread *sentinel;
};


// -----------------------------------------------------------------------------
// thread
// -----------------------------------------------------------------------------

struct epoch_thread * epoch_thread_get(struct ilka_epoch *ep)
{
    struct epoch_thread *thread = pthread_getspecific(ep->key);
    if (thread) return thread;

    thread = calloc(1, sizeof(struct epoch_thread));
    if (!thread) {
        ilka_fail("out-of-memory for epoch thread: %lu", sizeof(struct epoch_thread));
        return NULL;
    }

    thread->ep = ep;
    pthread_setspecific(ep->key, thread);

    {
        slock_lock(&ep->lock);

        thread->next = ep->threads;
        ep->threads->prev = thread;
        ep->threads = thread;

        slock_unlock(&ep->lock);
    }

    return thread;
}

void epoch_thread_remove(void *data)
{
    struct epoch_thread *thread = data;
    struct ilka_epoch *ep = thread->ep;

    ilka_assert(!thread->epoch, "thread exiting while in epoch");

    slock_lock(&ep->lock);

    // transfer defer nodes to sentinel node.
    for (size_t i = 0; i < 2; ++i) {
        struct epoch_defer *last = thread->defers[i];
        if (!last) continue;
        while (last->next) last = last->next;

        last->next = ep->sentinel->defers[i];
        ep->sentinel->defers[i] = last;
    }

    if (thread->next) thread->next->prev = thread->prev;
    if (thread->prev) thread->prev->next = thread->next;
    else ep->threads = thread->next;
    free(thread);

    slock_unlock(&ep->lock);
}


// -----------------------------------------------------------------------------
// basics
// -----------------------------------------------------------------------------

bool epoch_init(struct ilka_epoch *ep, struct ilka_region *region)
{
    memset(ep, 0, sizeof(struct ilka_epoch));

    ep->region = region;
    ep->epoch = 2;

    if (pthread_key_create(&ep->key, epoch_thread_remove)) {
        ilka_fail_errno("unable to create pthread key");
        goto fail_key;
    }

    ep->sentinel = calloc(1, sizeof(struct epoch_thread));
    if (!ep->sentinel) {
        ilka_fail("out-of-memory for epoch sentinel");
        goto fail_sentinel;
    }
    ep->sentinel->ep = ep;
    ep->threads = ep->sentinel;

    return true;

    free(ep->sentinel);
  fail_sentinel:

    pthread_key_delete(ep->key);
  fail_key:

    return false;
}

void epoch_close(struct ilka_epoch *ep)
{
    ilka_assert(!ep->world_lock, "closing with world stopped");
    ilka_assert(slock_try_lock(&ep->lock), "closing with lock held");

    while (ep->threads) {
        struct epoch_thread *thread = ep->threads;
        ep->threads = thread->next;

        ilka_assert(!thread->epoch,
                "closing with thread in region: thread=%p, epoch=%lu",
                (void *) thread, thread->epoch);

        for (size_t i = 0; i < 2; ++i) {
            ilka_assert(!thread->defers[i],
                    "closing with pending defer work: thread=%p", (void *) thread);
        }

        free(thread);
    }

    pthread_key_delete(ep->key);
}


// -----------------------------------------------------------------------------
// defer
// -----------------------------------------------------------------------------

static void epoch_defer_run(struct ilka_epoch *ep)
{
    ilka_assert(!slock_try_lock(&ep->lock), "lock is required for defer run");
    if (!ep->threads) return;

    // morder_relaxed: doesn't synchronize with anyone since any increment is
    // done while holding the lock that we're currently holding.
    size_t current_epoch = ilka_atomic_load(&ep->epoch, morder_relaxed);

    struct epoch_thread *thread = ep->threads;
    while (thread) {

        //  morder_relaxed: only written when entering the region and there's
        //  therefore no prior operations that we need to synchronize.
        size_t epoch = ilka_atomic_load(&thread->epoch, morder_relaxed);
        if (epoch && epoch < current_epoch) return;
        thread = thread->next;
    }

    thread = ep->threads;
    size_t i = (current_epoch - 1) % 2;
    while (thread) {

        // morder_consume: synchronizes epoch_defer_impl to ensure that all
        // defer nodes have been fully written before we read it.
        struct epoch_defer *defers = ilka_atomic_xchg(&thread->defers[i], 0, morder_consume);

        while (defers) {
            if (defers->fn) defers->fn(defers->data);
            else ilka_free(ep->region, defers->off, defers->len);

            struct epoch_defer *next = defers->next;
            free(defers);
            defers = next;
        }

        // morder_relaxed: only epoch_world_stop requires that all defers be
        // completed before proceeding but we do that syncrhonization through
        // the lock.
        ilka_atomic_store(&thread->defers[i], 0, morder_relaxed);

        thread = thread->next;
    }

    // morder_release: synchronizes epoch_world_stop and ensures that all the
    // defer lists have been fully cleared before allowing them to be filled up
    // again.
    ilka_atomic_fetch_add(&ep->epoch, 1, morder_release);
}

static bool epoch_defer_impl(struct ilka_epoch *ep, struct epoch_defer *node)
{
    struct epoch_thread *thread = epoch_thread_get(ep);
    if (!thread) return false;

    // morder_relaxed: pushing to a stale epoch is fine because it just means
    // that our node is already obsolete and can therefore be executed
    // right-away.
    size_t epoch = ilka_atomic_load(&ep->epoch, morder_relaxed);
    struct epoch_defer **head = &thread->defers[epoch % 2];

    struct epoch_defer *old_head = ilka_atomic_load(head, morder_acquire);
    do {
        node->next = old_head;

        // morder_release: synchronizes with epoch_defer_run to ensure that our
        // node has been fully written before it's read.
    } while (!ilka_atomic_cmp_xchg(head, &old_head, node, morder_release));

    return true;
}

static bool epoch_defer(struct ilka_epoch *ep, void (*fn) (void *), void *data)
{
    if (!fn) {
        ilka_fail("invalid nil function pointer");
        return false;
    }

    if (!data) {
        ilka_fail("invalid nil data");
        return false;
    }

    struct epoch_defer *node = malloc(sizeof(struct epoch_defer));
    if (!node) {
        ilka_fail("out-of-memory for defer node: %lu", sizeof(struct epoch_defer));
        return false;
    }

    *node = (struct epoch_defer) { .fn = fn, .data = data };
    if (!epoch_defer_impl(ep, node)) goto fail;

    return true;

  fail:
    free(node);
    return false;
}

static bool epoch_defer_free(struct ilka_epoch *ep, ilka_off_t off, size_t len)
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

    *node = (struct epoch_defer) { .off = off, .len = len };
    if (!epoch_defer_impl(ep, node)) goto fail;

    return true;

  fail:
    free(node);
    return false;
}


// -----------------------------------------------------------------------------
// enter/exit
// -----------------------------------------------------------------------------

static bool epoch_enter(struct ilka_epoch *ep)
{
    struct epoch_thread *thread = epoch_thread_get(ep);
    if (!thread) return false;

    while (true) {

        size_t epoch = ilka_atomic_load(&ep->epoch, morder_relaxed);
        ilka_atomic_store(&thread->epoch, epoch, morder_relaxed);

        // morder_acq_rel: ensures that our thread is stamped with an epoch
        // before we read world_lock. Otherwise, if the we check world_lock
        // prior to stamping the epoch then it would be possible for world_lock
        // to be set after we read it but before we stamp the epoch which means
        // that we could enter the epoch after world_stop has returned to the
        // user.
        //
        // We also require epoch_enter to be an acquire op so that no reads from
        // within the region are hoisted out of the region.
        ilka_atomic_fence(morder_acq_rel);

        // it's possible for the global epoch to have switched between our load
        // and our store so make sure we have the latest version.
        if (ilka_unlikely(epoch != ilka_atomic_load(&ep->epoch, morder_relaxed)))
            ilka_atomic_store(&thread->epoch, 0, morder_relaxed);

        // the world lock is on so spin until we resume.
        else if (ilka_unlikely(ilka_atomic_load(&ep->world_lock, morder_acquire))) {
            ilka_atomic_store(&thread->epoch, 0, morder_relaxed);
            while (ilka_atomic_load(&ep->world_lock, morder_acquire));
        }

        else return true;
    }

    ilka_unreachable();
}

static void epoch_exit(struct ilka_epoch *ep)
{
    struct epoch_thread *thread = epoch_thread_get(ep);
    ilka_assert(!!thread, "unexpected nil epoch thread");
    ilka_assert(thread->epoch, "exiting while not in epoch");

    // morder_release: synchronizes with epoch_world_stop to ensure that all ops
    // in the region are properly commited before indicating that the world has
    // stopped.
    //
    // We also require that epoch_exit is an overall release op so that no reads
    // from within the region are sunk below the region.
    ilka_atomic_store(&thread->epoch, 0, morder_release);

    // \todo: this is too aggressive. Should find a looser policy.
    if (slock_try_lock(&ep->lock)) {
        epoch_defer_run(ep);
        slock_unlock(&ep->lock);
    }
}


// -----------------------------------------------------------------------------
// world
// -----------------------------------------------------------------------------

static void epoch_world_stop(struct ilka_epoch *ep)
{
    // morder_acquire: syncrhonizes with epoch_world_resume to ensure that all
    // ops within the region stays within the region.
    ilka_atomic_fetch_add(&ep->world_lock, 1, morder_acquire);
    slock_lock(&ep->lock);

    struct epoch_thread *thread = ep->threads;
    while (thread) {
        // morder_acquire: synchronizes with epoch_exit to ensure that all ops
        // within the enter/exit region are completed before we can continue.
        while (ilka_atomic_load(&thread->epoch, morder_acquire));
        thread = thread->next;
    }

    // run it twice to make sure all defered work has been executed.
    epoch_defer_run(ep);
    epoch_defer_run(ep);

    slock_unlock(&ep->lock);
}

static void epoch_world_resume(struct ilka_epoch *ep)
{
    // morder_release: synchronizes with epoch_world_stop to ensure that all ops
    // within the region stay within the region.
    ilka_atomic_fetch_add(&ep->world_lock, -1, morder_release);
}
