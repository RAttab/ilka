/* epoch.c
   Rémi Attab (remi.attab@gmail.com), 26 Nov 2015
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// epoch
// -----------------------------------------------------------------------------

struct epoch_priv_thread
{
    size_t epoch;
    struct epoch_defer *defers[2];

    struct epoch_thread *next;
    struct epoch_thread *prev;
};

struct epoch_priv
{
    struct ilka_region *region;

    size_t epoch;
    size_t world_lock;

    ilka_slock lock;
    struct epoch_gc gc;
    struct epoch_thread_list threads;
};


// -----------------------------------------------------------------------------
// defer
// -----------------------------------------------------------------------------

static size_t epoch_priv_advance(void *ctx)
{
    struct epoch_priv *ep = ctx;
    ilka_assert(slock_is_locked(&ep->lock), "lock is required for defer run");

    // morder_relaxed: doesn't synchronize with anyone since any increment is
    // done while holding the lock that we're currently holding.
    size_t current_epoch = ilka_atomic_load(&ep->epoch, morder_relaxed);

    struct epoch_thread *thread = epoch_threads_head(&ep->threads);
    while (thread) {

        //  morder_relaxed: only written when entering the region and there's
        //  therefore no prior operations that we need to synchronize.
        size_t epoch = ilka_atomic_load(&thread->epoch, morder_relaxed);
        if (epoch && epoch < current_epoch) return current_epoch;
        thread = thread->next;
    }

    // morder_release: synchronizes epoch_world_stop and ensures that all the
    // defer lists have been fully cleared before allowing them to be filled up
    // again.
    return ilka_atomic_add_fetch(&ep->epoch, 1, morder_release);
}

static bool epoch_priv_defer(struct epoch_priv *ep, void (*fn) (void *), void *data)
{
    // morder_relaxed: pushing to a stale epoch is fine because it just means
    // that our node is already obsolete and can therefore be executed
    // right-away.
    size_t epoch = ilka_atomic_load(&ep->epoch, morder_relaxed);

    return epoch_gc_defer(&ep->gc, epoch, fn, data);
}

static bool epoch_priv_defer_free(
        struct epoch_priv *ep, ilka_off_t off, size_t len, size_t area)
{
    // morder_relaxed: pushing to a stale epoch is fine because it just means
    // that our node is already obsolete and can therefore be executed
    // right-away.
    size_t epoch = ilka_atomic_load(&ep->epoch, morder_relaxed);

    return epoch_gc_defer_free(&ep->gc, epoch, off, len, area);
}


// -----------------------------------------------------------------------------
// enter/exit
// -----------------------------------------------------------------------------

static bool epoch_priv_enter(struct epoch_priv *ep)
{
    struct epoch_thread *thread = epoch_thread_get(&ep->threads);
    if (!thread) return false;

  restart: (void) 0;

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
    if (ilka_unlikely(epoch != ilka_atomic_load(&ep->epoch, morder_relaxed))) {
        ilka_atomic_store(&thread->epoch, 0, morder_relaxed);
        goto restart;
    }

    // the world lock is on so spin until we resume.
    if (ilka_unlikely(ilka_atomic_load(&ep->world_lock, morder_acquire))) {
        ilka_atomic_store(&thread->epoch, 0, morder_relaxed);
        while (ilka_atomic_load(&ep->world_lock, morder_acquire));
        goto restart;
    }

    return true;
}

static void epoch_priv_exit(struct epoch_priv *ep)
{
    struct epoch_thread *thread = epoch_thread_get(&ep->threads);
    ilka_assert(!!thread, "unexpected nil epoch thread");
    ilka_assert(thread->epoch, "exiting while not in epoch");

    // morder_release: synchronizes with epoch_world_stop to ensure that all ops
    // in the region are properly commited before indicating that the world has
    // stopped.
    //
    // We also require that epoch_exit is an overall release op so that no reads
    // from within the region are sunk below the region.
    ilka_atomic_store(&thread->epoch, 0, morder_release);
}


// -----------------------------------------------------------------------------
// world
// -----------------------------------------------------------------------------

static void epoch_priv_world_stop(struct epoch_priv *ep)
{
    // morder_acquire: syncrhonizes with epoch_world_resume to ensure that all
    // ops within the region stays within the region.
    ilka_atomic_fetch_add(&ep->world_lock, 1, morder_acquire);
    slock_lock(&ep->lock);

    struct epoch_thread *thread = epoch_threads_head(&ep->threads);
    while (thread) {
        // morder_acquire: synchronizes with epoch_exit to ensure that all ops
        // within the enter/exit region are completed before we can continue.
        while (ilka_atomic_load(&thread->epoch, morder_acquire));
        thread = thread->next;
    }

    // run it twice to make sure all defered work has been executed.
    epoch_gc_advance(&ep->gc);
    epoch_gc_advance(&ep->gc);

    slock_unlock(&ep->lock);
}

static void epoch_priv_world_resume(struct epoch_priv *ep)
{
    // morder_release: synchronizes with epoch_world_stop to ensure that all ops
    // within the region stay within the region.
    ilka_atomic_fetch_add(&ep->world_lock, -1, morder_release);
}


// -----------------------------------------------------------------------------
// basics
// -----------------------------------------------------------------------------

bool epoch_priv_init(
        struct epoch_priv *ep,
        struct ilka_region *region,
        struct ilka_options *options)
{
    memset(ep, 0, sizeof(struct epoch_priv));

    ep->region = region;
    ep->epoch = 2;

    if (!epoch_threads_init(&ep->threads, &ep->lock)) goto fail_threads;

    ep->gc = (struct epoch_gc) {
        .ep = ep,
        .region = region,
        .lock = &ep->lock,
        .threads = &ep->threads,
        .advance_fn = epoch_priv_advance,
    };
    if (!epoch_gc_init(&ep->gc, options)) goto fail_gc;

    return true;

    epoch_gc_close(&ep->gc);
  fail_gc:

    epoch_threads_close(&ep->threads);
  fail_threads:

    return false;
}

void epoch_priv_close(struct epoch_priv *ep)
{
    epoch_gc_close(&ep->gc);

    ilka_assert(!ep->world_lock, "closing with world stopped");
    ilka_assert(slock_try_lock(&ep->lock), "closing with lock held");

    epoch_threads_close(&ep->threads);
}
