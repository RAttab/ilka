/* epoch_shared.c
   Rémi Attab (remi.attab@gmail.com), 28 Feb 2016
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// epoch
// -----------------------------------------------------------------------------

struct ilka_packed epoch_shrd_slot
{
    uint64_t epochs[2];

    uint64_t packed[6];
};

struct ilka_packed epoch_shrd_meta
{
    size_t slots_len;

    size_t epoch;
    size_t last_epoch;
    size_t world_lock;

    uint64_t packed[5];

    struct epoch_shrd_slot slots[];
};

struct epoch_shrd
{
    struct ilka_region *region;
    struct epoch_shrd_meta *meta;

    ilka_slock lock;
    struct epoch_gc gc;
    struct epoch_thread_list threads;
};


// -----------------------------------------------------------------------------
// defer
// -----------------------------------------------------------------------------

static size_t epoch_shrd_advance(void *ctx)
{
    struct epoch_shrd *ep = ctx;
    ilka_assert(slock_is_locked(&ep->lock), "lock is required for defer run");

    // morder_relaxed: doesn't synchronize with anyone since any increment is
    // done while holding the lock that we're currently holding.
    size_t epoch = ilka_atomic_load(&ep->meta->epoch, morder_relaxed);

    // The epoch might have been moved forward by another process in which case
    // we should not increment the epoch forward.
    size_t last_epoch = ep->meta->last_epoch;
    ep->meta->last_epoch = epoch;
    if (epoch != last_epoch) return epoch;

    size_t epoch_i = (epoch - 1) % 2;
    for (size_t i = 0; i < ep->meta->slots_len; ++i) {
        struct epoch_shrd_slot *slot = &ep->meta->slots[i];

        //  morder_relaxed: only written when entering the region and there's
        //  therefore no prior operations that we need to synchronize.
        if (ilka_atomic_load(&slot->epochs[epoch_i], morder_relaxed))
            return epoch;
    }

    // morder_release: synchronizes epoch_world_stop and ensures that all the
    // defer lists have been fully cleared before allowing them to be filled up
    // again.
    ilka_atomic_cmp_xchg(&ep->meta->epoch, &epoch, epoch + 1, morder_release);
    return epoch;
}

static bool epoch_shrd_defer(struct epoch_shrd *ep, void (*fn) (void *), void *data)
{
    // morder_relaxed: pushing to a stale epoch is fine because it just means
    // that our node is already obsolete and can therefore be executed
    // right-away.
    size_t epoch = ilka_atomic_load(&ep->meta->epoch, morder_relaxed);

    return epoch_gc_defer(&ep->gc, epoch, fn, data);
}

static bool epoch_shrd_defer_free(
        struct epoch_shrd *ep, ilka_off_t off, size_t len, size_t area)
{
    // morder_relaxed: pushing to a stale epoch is fine because it just means
    // that our node is already obsolete and can therefore be executed
    // right-away.
    size_t epoch = ilka_atomic_load(&ep->meta->epoch, morder_relaxed);

    return epoch_gc_defer_free(&ep->gc, epoch, off, len, area);
}


// -----------------------------------------------------------------------------
// enter/exit
// -----------------------------------------------------------------------------

static struct epoch_shrd_slot *epoch_shrd_slot_get(struct epoch_shrd *ep)
{
    return &ep->meta->slots[ilka_tid() % ep->meta->slots_len];
}


static bool epoch_shrd_enter(struct epoch_shrd *ep)
{
    struct epoch_thread *thread = epoch_thread_get(&ep->threads);
    if (!thread) return false;

    struct epoch_shrd_slot *slot = epoch_shrd_slot_get(ep);

  restart: (void) 0;

    size_t epoch = ilka_atomic_load(&ep->meta->epoch, morder_relaxed);
    ilka_atomic_fetch_add(&slot->epochs[epoch % 2], 1, morder_relaxed);

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
    if (ilka_unlikely(epoch != ilka_atomic_load(&ep->meta->epoch, morder_relaxed))) {
        ilka_atomic_store(&thread->epoch, 0, morder_relaxed);
        goto restart;
    }

    // the world lock is on so spin until we resume.
    if (ilka_unlikely(ilka_atomic_load(&ep->meta->world_lock, morder_acquire))) {
        ilka_atomic_store(&thread->epoch, 0, morder_relaxed);
        while (ilka_atomic_load(&ep->meta->world_lock, morder_acquire));
        goto restart;
    }

    thread->epoch = epoch;
    return true;
}

static void epoch_shrd_exit(struct epoch_shrd *ep)
{
    struct epoch_thread *thread = epoch_thread_get(&ep->threads);
    ilka_assert(!!thread, "unexpected nil epoch thread");
    ilka_assert(thread->epoch, "exiting while not in epoch");

    struct epoch_shrd_slot *slot = epoch_shrd_slot_get(ep);

    // morder_release: synchronizes with epoch_world_stop to ensure that all ops
    // in the region are properly commited before indicating that the world has
    // stopped.
    //
    // We also require that epoch_exit is an overall release op so that no reads
    // from within the region are sunk below the region.
    ilka_atomic_fetch_add(&slot->epochs[thread->epoch % 2], -1, morder_release);
    thread->epoch = 0;
}


// -----------------------------------------------------------------------------
// world
// -----------------------------------------------------------------------------

static void epoch_shrd_world_stop(struct epoch_shrd *ep)
{
    // morder_acquire: syncrhonizes with epoch_world_resume to ensure that all
    // ops within the region stays within the region.
    ilka_atomic_fetch_add(&ep->meta->world_lock, 1, morder_acquire);
    slock_lock(&ep->lock);

    for (size_t i = 0; i < ep->meta->slots_len; ++i) {
        struct epoch_shrd_slot *slot = &ep->meta->slots[i];

        for (size_t epoch = 0; epoch < 2; ++epoch) {
            // morder_acquire: synchronizes with epoch_exit to ensure that all ops
            // within the enter/exit region are completed before we can continue.
            while (ilka_atomic_load(&slot->epochs[epoch], morder_acquire));
        }
    }

    // run it twice to make sure all defered work has been executed.
    epoch_gc_advance(&ep->gc);
    epoch_gc_advance(&ep->gc);

    slock_unlock(&ep->lock);
}

static void epoch_shrd_world_resume(struct epoch_shrd *ep)
{
    // morder_release: synchronizes with epoch_world_stop to ensure that all ops
    // within the region stay within the region.
    ilka_atomic_fetch_add(&ep->meta->world_lock, -1, morder_release);
}


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

static size_t meta_len(size_t slots)
{
    return sizeof(struct epoch_shrd_meta) + slots * sizeof(struct epoch_shrd_slot);
}

static bool epoch_shrd_open(
        struct epoch_shrd *ep,
        struct ilka_region *region,
        struct ilka_options *options,
        ilka_off_t off)
{
    ep->region = region;

    if (!epoch_threads_init(&ep->threads, &ep->lock)) goto fail_threads;

    ep->gc = (struct epoch_gc) {
        .ep = ep,
        .region = region,
        .lock = &ep->lock,
        .threads = &ep->threads,
        .advance_fn = epoch_shrd_advance,
    };
    if (!epoch_gc_init(&ep->gc, options)) goto fail_gc;

    const struct epoch_shrd_meta *meta = ilka_read(region, off, sizeof(*meta));
    ep->meta = ilka_write(region, off, meta_len(meta->slots_len));

    return true;

    epoch_gc_close(&ep->gc);
  fail_gc:

    epoch_threads_close(&ep->threads);
  fail_threads:

    return false;
}

static bool epoch_shrd_init(
        struct epoch_shrd *ep,
        struct ilka_region *region,
        struct ilka_options *options,
        ilka_off_t *off)

{
    size_t slots = options->epoch_slots ? options->epoch_slots : ilka_cpus();
    size_t len = meta_len(slots);

    *off = ilka_alloc(region, len);
    if (!*off) return false;

    struct epoch_shrd_meta *meta = ilka_write(region, *off, len);
    memset(meta, 0, len);
    meta->slots_len = slots;

    return epoch_shrd_open(ep, region, options, *off);
}

static void epoch_shrd_close(struct epoch_shrd *ep)
{
    epoch_gc_close(&ep->gc);
    epoch_threads_close(&ep->threads);
}
