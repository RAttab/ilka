/* hash_bucket.c
   Rémi Attab (remi.attab@gmail.com), 28 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// bucket
// -----------------------------------------------------------------------------
//
// \todo: This code needs a review of the morder.
//
// \todo: we should return ret_stop if we observe BOTH key and val to nil.
//        Otherwise, bucket_put returns ret_skip if it detects a half-entered
//        key which means that we a bucket_put could terminate and not be
//        immediately available to get subsequent hash_get or hash_del op due to
//        a prior bucket_put operation having not completed.

struct ilka_packed hash_bucket
{
    ilka_off_t key;
    ilka_off_t val;
};

static struct ilka_hash_ret bucket_get(
        struct ilka_hash *ht,
        const struct hash_bucket *bucket,
        struct hash_key *key)
{
    ilka_off_t old_key = ilka_atomic_load(&bucket->key, morder_relaxed);
    switch (state_get(old_key)) {
    case state_nil: return make_ret(ret_skip, 0);
    case state_tomb: return make_ret(ret_skip, 0);
    case state_move: return make_ret(ret_resize, 0);
    case state_set:
        if (!key_check(ht, state_clear(old_key), key))
            return make_ret(ret_skip, 0);
        break;
    }

    ilka_off_t old_val = ilka_atomic_load(&bucket->val, morder_relaxed);
    switch (state_get(old_val)) {
    case state_nil: return make_ret(ret_skip, 0);
    case state_tomb: return make_ret(ret_skip, 0);
    case state_move: return make_ret(ret_resize, 0);
    case state_set: return make_ret(ret_ok, state_clear(old_val));
    }

    ilka_unreachable();
}

static void bucket_tomb_key(struct ilka_hash *ht, ilka_off_t *val, enum morder mo)
{
    ilka_off_t new;
    ilka_off_t old = ilka_atomic_load(val, morder_relaxed);
    do {
        if (state_get(old) == state_tomb) {
            ilka_atomic_fence(mo);
            return;
        }
        new = state_trans(old, state_tomb);
    } while (!ilka_atomic_cmp_xchg(val, &old, new, mo));

    if (state_get(old) != state_move)
        key_free(ht, state_clear(old));
}

static void bucket_tomb_val(ilka_off_t *val, enum morder mo)
{
    ilka_off_t new;
    ilka_off_t old = ilka_atomic_load(val, morder_relaxed);
    do {
        if (state_get(old) == state_tomb) {
            ilka_atomic_fence(mo);
            return;
        }
        new = state_trans(old, state_tomb);
    } while (!ilka_atomic_cmp_xchg(val, &old, new, mo));
}

static struct ilka_hash_ret bucket_put(
        struct ilka_hash *ht,
        struct hash_bucket *bucket,
        struct hash_key *key,
        ilka_off_t value)
{
    ilka_off_t new_key;
    ilka_off_t old_key = ilka_atomic_load(&bucket->key, morder_relaxed);
    do {
        switch (state_get(old_key)) {
        case state_tomb: return make_ret(ret_skip, 0);
        case state_move: return make_ret(ret_resize, 0);

        case state_set:
            if (!key_check(ht, state_clear(old_key), key))
                return make_ret(ret_skip, 0);
            goto break_key;

        case state_nil:
            if (!key->off) {
                key_alloc(ht, key);

                // morder_release: make sure the key is committed before it's
                // published.
                ilka_atomic_fence(morder_release);
            }
            if (!key->off) return make_ret(ret_err, 0);
            new_key = state_trans(key->off, state_set);
            break;
        }

        // morder_relaxed: we can commit the key with the value set.
    } while (!ilka_atomic_cmp_xchg(&bucket->key, &old_key, new_key, morder_relaxed));
  break_key: (void) 0;

    ilka_off_t new_val;
    ilka_off_t old_val = ilka_atomic_load(&bucket->val, morder_relaxed);
    do {
        switch (state_get(old_val)) {
        case state_tomb: return make_ret(ret_skip, 0);
        case state_move: return make_ret(ret_resize, 0);
        case state_set: return make_ret(ret_stop, state_clear(old_val));
        case state_nil: new_val = state_trans(value, state_set); break;
        }

        // morder_release: make sure both writes are commited before moving on.
    } while (!ilka_atomic_cmp_xchg(&bucket->val, &old_val, new_val, morder_release));

    return make_ret(ret_ok, 0);
}

static struct ilka_hash_ret bucket_xchg(
        struct ilka_hash *ht,
        struct hash_bucket *bucket,
        struct hash_key *key,
        ilka_off_t expected,
        ilka_off_t value)
{
    ilka_off_t old_key = ilka_atomic_load(&bucket->key, morder_relaxed);
    switch (state_get(old_key)) {
    case state_nil: return make_ret(ret_skip, 0);
    case state_tomb: return make_ret(ret_skip, 0);
    case state_move: return make_ret(ret_resize, 0);

    case state_set:
        if (!key_check(ht, state_clear(old_key), key))
            return make_ret(ret_skip, 0);
        break;
    }

    ilka_off_t new_val;
    ilka_off_t old_val = ilka_atomic_load(&bucket->val, morder_relaxed);
    ilka_off_t clean_val = state_clear(old_val);
    do {
        switch (state_get(old_val)) {
        case state_nil: return make_ret(ret_skip, 0);
        case state_tomb: return make_ret(ret_skip, 0);
        case state_move: return make_ret(ret_resize, 0);

        case state_set:
            if (expected && clean_val != expected)
                return make_ret(ret_stop, clean_val);
            new_val = state_trans(value, state_set);
            break;
        }

        // morder_release: make sure all value related writes are committed
        // before publishing it.
    } while (!ilka_atomic_cmp_xchg(&bucket->val, &old_val, new_val, morder_release));

    return make_ret(ret_ok, clean_val);
}

static struct ilka_hash_ret bucket_del(
        struct ilka_hash *ht,
        struct hash_bucket *bucket,
        struct hash_key *key,
        ilka_off_t expected)
{
    ilka_off_t old_key = ilka_atomic_load(&bucket->key, morder_relaxed);
    switch (state_get(old_key)) {
    case state_nil: return make_ret(ret_skip, 0);
    case state_tomb: return make_ret(ret_skip, 0);
    case state_move: return make_ret(ret_resize, 0);
    case state_set:
        if (!key_check(ht, state_clear(old_key), key))
            return make_ret(ret_skip, 0);
        break;
    }

    ilka_off_t new_val;
    ilka_off_t old_val = ilka_atomic_load(&bucket->val, morder_relaxed);
    ilka_off_t clean_val = state_clear(old_val);
    do {
        switch(state_get(old_val)) {
        case state_nil: return make_ret(ret_skip, 0);
        case state_tomb: return make_ret(ret_skip, 0);
        case state_move: return make_ret(ret_resize, 0);
        case state_set:
            if (expected && clean_val != expected)
                return make_ret(ret_stop, clean_val);
            new_val = state_trans(old_val, state_tomb);
            break;
        }

        // morder_relaxed: It should not be possible for the tomb of the key to
        // be reordered before the tomb of the val because that would change the
        // semantic of the sequential program (there are return statements in
        // the for-loop. As a result we can just commit this write along with
        // the tomb of the key.
    } while (!ilka_atomic_cmp_xchg(&bucket->val, &old_val, new_val, morder_relaxed));

    // morder_release: commit both the key and val writes.
    bucket_tomb_key(ht, &bucket->key, morder_release);

    return make_ret(ret_ok, state_clear(old_val));
}

static bool bucket_lock(struct ilka_hash *ht, struct hash_bucket *bucket)
{
    ilka_off_t new_key;
    ilka_off_t old_key = ilka_atomic_load(&bucket->key, morder_relaxed);
    do {
        switch (state_get(old_key)) {
        case state_tomb: return false;
        case state_move: new_key = old_key; goto break_key_lock;
        case state_nil: new_key = state_trans(old_key, state_tomb); break;
        case state_set: new_key = state_trans(old_key, state_move); break;
        }

        // morder_relaxed: we can commit the key with the value lock.
    } while (!ilka_atomic_cmp_xchg(&bucket->key, &old_key, new_key, morder_relaxed));
  break_key_lock: (void) 0;

    enum state key_state = state_get(new_key);

    ilka_off_t new_val;
    ilka_off_t old_val = ilka_atomic_load(&bucket->val, morder_relaxed);
    do {
        switch (state_get(old_val)) {
        case state_tomb: return false;
        case state_move: new_val = old_val; goto break_val_lock;
        case state_nil: new_val = state_trans(old_val, state_tomb); break;
        case state_set: new_val = state_trans(old_val, key_state); break;
        }

        // morder_release: make sure both locks are committed before moving on.
    } while (!ilka_atomic_cmp_xchg(&bucket->val, &old_val, new_val, morder_release));
  break_val_lock: (void) 0;

    enum state val_state = state_get(new_key);
    if (key_state == state_move && val_state == state_tomb) {
        // morder_relaxed: this is not a linearlization point and mostly just
        // bookeeping so no ordering guarantees are required.
        bucket_tomb_key(ht, &bucket->key, morder_relaxed);
        return false;
    }

    ilka_assert(key_state == val_state,
            "unmatched state for key and val: %d != %d", key_state, val_state);
    return val_state == state_move;
}
