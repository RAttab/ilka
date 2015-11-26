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

    thread->prev->next = thread->next;
    thread->next->prev = thread->prev;
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
    ilka_assert(!ep->world_lock, "closing with active threads");
    ilka_assert(slock_try_lock(&ep->lock), "closing with active threads");

    while (ep->threads) {
        struct epoch_thread *thread = ep->threads;
        ep->threads = thread->next;

        ilka_assert(thread->epoch, "closing with active thread");
        for (size_t i = 0; i < 2; ++i)
            ilka_assert(!thread->defers[i], "closing with pending defer work");

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

    struct epoch_thread *thread = ep->threads;
    while (thread) {
        size_t epoch = ilka_atomic_load(&thread->epoch, morder_acquire);
        if (epoch && epoch < ep->epoch) return;
    }

    thread = ep->threads;
    size_t i = (ep->epoch - 1) % 2;
    while (thread) {
        struct epoch_defer *defers = ilka_atomic_load(&thread->defers[i], morder_acquire);

        while (defers) {
            if (defers->fn) defers->fn(defers->data);
            else ilka_free(ep->region, defers->off, defers->len);

            struct epoch_defer *next = defers->next;
            free(defers);
            defers = next;
        }

        ilka_atomic_store(&thread->defers[i], 0, morder_release);
    }

    ilka_atomic_fetch_add(&ep->epoch, 1, morder_release);
}

static bool epoch_defer_impl(struct ilka_epoch *ep, struct epoch_defer *node)
{
    struct epoch_thread *thread = epoch_thread_get(ep);
    if (!thread) return false;

    size_t epoch = ilka_atomic_load(&ep->epoch, morder_acquire);
    struct epoch_defer **head = &thread->defers[epoch % 2];

    struct epoch_defer *old_head = ilka_atomic_load(head, morder_relaxed);
    do {
        node->next = old_head;
    } while (!ilka_atomic_cmp_xchg(head, &old_head, node, morder_release));

    return true;
}

static bool epoch_defer(struct ilka_epoch *ep, void (*fn) (void *), void *data)
{
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

        ilka_atomic_fence(morder_acq_rel);

        // it's possible for the global epoch to have switched between our load
        // and our store so make sure we have the latest version.
        if (epoch != ilka_atomic_load(&ep->epoch, morder_relaxed))
            ilka_atomic_store(&thread->epoch, 0, morder_relaxed);

        // the world lock is on so spin until we resume.
        else if (ilka_atomic_load(&ep->world_lock, morder_relaxed)) {
            ilka_atomic_store(&thread->epoch, 0, morder_relaxed);
            while (ilka_atomic_load(&ep->world_lock, morder_relaxed));
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
    ilka_atomic_fetch_add(&ep->world_lock, 1, morder_acquire);
    slock_lock(&ep->lock);

    struct epoch_thread *thread = ep->threads;
    while (thread) {
        while (ilka_atomic_load(&thread->epoch, morder_relaxed));
        thread = thread->next;
    }

    // run it twice to make sure all defered work has been executed.
    epoch_defer_run(ep);
    epoch_defer_run(ep);

    slock_unlock(&ep->lock);
}

static void epoch_world_resume(struct ilka_epoch *ep)
{
    ilka_atomic_fetch_add(&ep->world_lock, -1, morder_release);
}
