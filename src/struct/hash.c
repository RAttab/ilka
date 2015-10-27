/* hash.c
   Rémi Attab (remi.attab@gmail.com), 20 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "hash.h"

#include "utils/utils.h"
#include "siphash.h"
#include <string.h>
#include <stdlib.h>


// -----------------------------------------------------------------------------
// config
// -----------------------------------------------------------------------------

// must be consistent across restarts. We could save it in the region and
// randomize on every new hash created which would make the whole thing more DoS
// resistant. Will deal with that stuff later.
static struct sipkey sipkey = {{ 0xc60243215c6ee9d1, 0xcd9cc80b04763259 }};


// -----------------------------------------------------------------------------
// ret
// -----------------------------------------------------------------------------

enum ret_code
{
    ret_err = -1,
    ret_ok = 0,
    ret_skip = 1,
    ret_stop = 2,
    ret_resize = 3,
};

inline static struct ilka_hash_ret make_ret(enum ret_code code, ilka_off_t old)
{
    return (struct ilka_hash_ret) { code, old };
}


// -----------------------------------------------------------------------------
// key
// -----------------------------------------------------------------------------

static uint64_t key_hash(struct ilka_hash_key key)
{
    struct siphash sip;
    sip24_init(&sip, &sipkey);
    sip24_update(&sip, key.data, key.len);
    return sip24_final(&sip);
}

static bool key_equals(struct ilka_hash_key lhs, struct ilka_hash_key rhs)
{
    if (lhs.len != rhs.len) return false;
    return !memcmp(lhs.data, rhs.data, lhs.len);
}

static ilka_off_t key_alloc(struct ilka_region *r, struct ilka_hash_key key)
{
    size_t n = sizeof(size_t) + key.len;

    ilka_off_t off = ilka_alloc(r, n);
    if (!off) return off;

    size_t *p = ilka_write(r, off, n);
    *p = key.len;
    memcpy(p + 1, key.data, key.len);

    return off;
}

static void key_free(struct ilka_region *r, ilka_off_t off)
{
    const size_t *len = ilka_read(r, off, sizeof(size_t));
    ilka_free(r, off, sizeof(size_t) + *len);
}

static struct ilka_hash_key key_from_off(struct ilka_region *r, ilka_off_t off)
{
    const size_t *len = ilka_read(r, off, sizeof(size_t));
    const void *data = ilka_read(r, off + sizeof(size_t), *len);

    return (struct ilka_hash_key) { *len, data };
}

inline static bool key_check(
        struct ilka_region *r, ilka_off_t off, struct ilka_hash_key key)
{
    return key_equals(key, key_from_off(r, off));
}


// -----------------------------------------------------------------------------
// state
// -----------------------------------------------------------------------------

enum state
{
    state_nil = 0,
    state_set = 1,
    state_move = 2,
    state_tomb = 3
};

static inline enum state state_get(ilka_off_t v) { return (v >> 62) & 0x3; }
static inline ilka_off_t state_clear(ilka_off_t v) { return v & ~(0x3UL << 62); }
static inline ilka_off_t state_trans(ilka_off_t v, enum state s)
{
    ilka_assert(state_get(v) < s,
            "invalid state transition: %d -> %d", state_get(v), s);
    return state_clear(v) | (((ilka_off_t) s) << 62);
}


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
        struct hash_bucket *b, struct ilka_region *r, struct ilka_hash_key key)
{
    ilka_off_t old_key = ilka_atomic_load(&b->key, morder_relaxed);
    switch (state_get(old_key)) {
    case state_nil: return make_ret(ret_skip, 0);
    case state_tomb: return make_ret(ret_skip, 0);
    case state_move: return make_ret(ret_resize, 0);
    case state_set:
        if (!key_check(r, state_clear(old_key), key))
            return make_ret(ret_skip, 0);
        break;
    }

    ilka_off_t old_val = ilka_atomic_load(&b->val, morder_relaxed);
    switch (state_get(old_val)) {
    case state_nil: return make_ret(ret_skip, 0);
    case state_tomb: return make_ret(ret_skip, 0);
    case state_move: return make_ret(ret_resize, 0);
    case state_set: return make_ret(ret_ok, state_clear(old_val));
    }

    ilka_unreachable();
}

static void bucket_tomb_part(ilka_off_t *v, enum morder mo)
{
    ilka_off_t new;
    ilka_off_t old = ilka_atomic_load(v, morder_relaxed);
    do {
        if (state_get(old) == state_tomb) {
            ilka_atomic_fence(mo);
            return;
        }
        new = state_trans(old, state_tomb);
    } while (!ilka_atomic_cmp_xchg(v, &old, new, mo));
}

static void bucket_tomb(struct hash_bucket *b)
{
    // morder: we commit both writes at the same time when tombing the value.
    bucket_tomb_part(&b->key, morder_relaxed);
    bucket_tomb_part(&b->val, morder_release);
}

static struct ilka_hash_ret bucket_put(
        struct hash_bucket *b,
        struct ilka_region *r,
        struct ilka_hash_key key,
        ilka_off_t value,
        ilka_off_t *key_off)
{
    ilka_off_t new_key;
    ilka_off_t old_key = ilka_atomic_load(&b->key, morder_relaxed);
    do {
        switch (state_get(old_key)) {
        case state_tomb: return make_ret(ret_skip, 0);
        case state_move: return make_ret(ret_resize, 0);

        case state_set:
            if (!key_check(r, state_clear(old_key), key))
                return make_ret(ret_skip, 0);
            goto break_key;

        case state_nil:
            if (!*key_off) {
                *key_off = key_alloc(r, key);

                // morder_release: make sure the key is committed before it's
                // published.
                ilka_atomic_fence(morder_release);
            }
            if (!*key_off) return make_ret(ret_err, 0);
            new_key = state_trans(*key_off, state_set);
            break;
        }

        // morder_relaxed: we can commit the key with the value set.
    } while (!ilka_atomic_cmp_xchg(&b->key, &old_key, new_key, morder_relaxed));
  break_key: (void) 0;

    ilka_off_t new_val;
    ilka_off_t old_val = ilka_atomic_load(&b->val, morder_relaxed);
    do {
        switch (state_get(old_val)) {
        case state_tomb: return make_ret(ret_skip, 0);
        case state_move: return make_ret(ret_resize, 0);
        case state_set: return make_ret(ret_skip, state_clear(old_val));
        case state_nil: new_val = state_trans(value, state_set); break;
        }

        // morder_release: make sure both writes are commited before moving on.
    } while (!ilka_atomic_cmp_xchg(&b->val, &old_val, new_val, morder_release));

    return make_ret(ret_ok, 0);
}

static struct ilka_hash_ret bucket_xchg(
        struct hash_bucket *b,
        struct ilka_region *r,
        struct ilka_hash_key key,
        ilka_off_t value,
        ilka_off_t expected)
{
    ilka_off_t old_key = ilka_atomic_load(&b->key, morder_relaxed);
    switch (state_get(old_key)) {
    case state_nil: return make_ret(ret_skip, 0);
    case state_tomb: return make_ret(ret_skip, 0);
    case state_move: return make_ret(ret_resize, 0);

    case state_set:
        if (!key_check(r, state_clear(old_key), key))
            return make_ret(ret_skip, 0);
        break;
    }

    ilka_off_t new_val;
    ilka_off_t old_val = ilka_atomic_load(&b->val, morder_relaxed);
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
    } while (!ilka_atomic_cmp_xchg(&b->val, &old_val, new_val, morder_release));

    return make_ret(ret_ok, clean_val);
}

static struct ilka_hash_ret bucket_del(
        struct hash_bucket *b, struct ilka_region *r, struct ilka_hash_key key)
{
    ilka_off_t old_key = ilka_atomic_load(&b->key, morder_relaxed);
    switch (state_get(old_key)) {
    case state_nil: return make_ret(ret_skip, 0);
    case state_tomb: return make_ret(ret_skip, 0);
    case state_move: return make_ret(ret_resize, 0);
    case state_set:
        if (!key_check(r, state_clear(old_key), key))
            return make_ret(ret_skip, 0);
        break;
    }

    ilka_off_t new_val;
    ilka_off_t old_val = ilka_atomic_load(&b->val, morder_relaxed);
    do {
        switch(state_get(old_val)) {
        case state_nil: return make_ret(ret_skip, 0);
        case state_tomb: return make_ret(ret_skip, 0);
        case state_move: return make_ret(ret_resize, 0);
        case state_set:
            new_val = state_trans(old_val, state_tomb);
            break;
        }

        // morder_relaxed: It should not be possible for the tomb of the key to
        // be reordered before the tomb of the val because that would change the
        // semantic of the sequential program (there are return statements in
        // the for-loop. As a result we can just commit this write along with
        // the tomb of the key.
    } while (!ilka_atomic_cmp_xchg(&b->val, &old_val, new_val, morder_relaxed));

    // morder_release: commit both the key and val writes.
    bucket_tomb_part(&b->key, morder_release);

    return make_ret(ret_ok, state_clear(old_val));
}

static bool bucket_lock(struct hash_bucket *b)
{
    ilka_off_t new_key;
    ilka_off_t old_key = ilka_atomic_load(&b->key, morder_relaxed);
    do {
        switch (state_get(old_key)) {
        case state_tomb: return false;
        case state_move: new_key = old_key; goto break_key_lock;
        case state_nil: new_key = state_trans(old_key, state_tomb); break;
        case state_set: new_key = state_trans(old_key, state_move); break;
        }

        // morder_relaxed: we can commit the key with the value lock.
    } while (!ilka_atomic_cmp_xchg(&b->key, &old_key, new_key, morder_relaxed));
  break_key_lock: (void) 0;

    enum state key_state = state_get(new_key);

    ilka_off_t new_val;
    ilka_off_t old_val = ilka_atomic_load(&b->val, morder_relaxed);
    do {
        switch (state_get(old_val)) {
        case state_tomb: return false;
        case state_move: new_val = old_val; goto break_val_lock;
        case state_nil: new_val = state_trans(old_val, state_tomb); break;
        case state_set: new_val = state_trans(old_val, key_state); break;
        }

        // morder_release: make sure both locks are committed before moving on.
    } while (!ilka_atomic_cmp_xchg(&b->val, &old_val, new_val, morder_release));
  break_val_lock: (void) 0;

    enum state val_state = state_get(new_key);
    if (key_state == state_move && val_state == state_tomb) {
        bucket_tomb(b);
        return false;
    }

    ilka_assert(key_state == val_state,
            "unmatched state for key and val: %d != %d", key_state, val_state);
    return val_state == state_move;
}


// -----------------------------------------------------------------------------
// table
// -----------------------------------------------------------------------------

struct ilka_packed hash_table
{
    size_t cap; // must be first.
    ilka_off_t next;

    struct hash_bucket buckets[];
};

static size_t table_size(size_t cap)
{
    return sizeof(struct hash_table) + cap * sizeof(struct hash_bucket);
}

static const struct hash_table * table_read(struct ilka_region *r, ilka_off_t off)
{
    const size_t *cap = ilka_read(r, off, sizeof(size_t));
    return ilka_read(r, off, table_size(*cap));
}

static const struct hash_bucket * bucket_write(
        struct ilka_region *r, ilka_off_t off, size_t i)
{
    return ilka_write(r,
            off + sizeof(struct hash_table) + i * sizeof(struct hash_bucket),
            sizeof(struct hash_bucket));
}


// -----------------------------------------------------------------------------
// basics
// -----------------------------------------------------------------------------

struct ilka_hash
{
    struct ilka_region *r;
    ilka_off_t meta;
};

struct ilka_packed hash_meta
{
    size_t len;
    ilka_off_t table;
};

struct ilka_hash * ilka_hash_alloc(struct ilka_region *r)
{
    struct ilka_hash *h = malloc(sizeof(struct ilka_hash));
    if (!h) {
        ilka_fail_errno("out-of-memory for hash struct: %lu", sizeof(struct ilka_hash));
        return NULL;
    }

    h->r = r;
    h->meta = ilka_alloc(r, sizeof(struct hash_meta));
    if (!h->meta) goto fail_meta;

    struct hash_meta *meta = ilka_write(r, h->meta, sizeof(struct hash_meta));
    memset(meta, 0, sizeof(struct hash_meta));

    return h;

  fail_meta:
    free(h);
    return NULL;
}

bool ilka_hash_free(struct ilka_hash *h)
{
    const struct hash_meta *meta = ilka_read(h->r, h->meta, sizeof(struct hash_meta));

    ilka_off_t table = meta->table;
    while (table) {
        const struct hash_table *ht = table_read(h->r, table);

        for (size_t i = 0; i < ht->cap; ++i)
            key_free(h->r, state_clear(ht->buckets[i].key));

        ilka_off_t next = ht->next;
        ilka_free(h->r, table, table_size(ht->cap));
        table = next;
    }

    ilka_free(h->r, h->meta, sizeof(struct hash_meta));

    return ilka_hash_close(h);
}

struct ilka_hash * ilka_hash_open(struct ilka_region *r, ilka_off_t off)
{
    struct ilka_hash *h = malloc(sizeof(struct ilka_hash));
    *h = (struct ilka_hash) { r, off };
    return h;
}

bool ilka_hash_close(struct ilka_hash *h)
{
    free(h);
    return true;
}

ilka_off_t ilka_hash_off(struct ilka_hash *h)
{
    return h->meta;
}


// -----------------------------------------------------------------------------
// sizing
// -----------------------------------------------------------------------------

size_t ilka_hash_cap(struct ilka_hash *h)
{
    return 0;
}

bool ilka_hash_reserve(struct ilka_hash *h, size_t cap)
{
    return false;
}

size_t ilka_hash_len(struct ilka_hash *h)
{
    return 0;
}

bool ilka_hash_resize(struct ilka_hash *h, size_t len)
{
    return false;
}


// -----------------------------------------------------------------------------
// read
// -----------------------------------------------------------------------------

int ilka_hash_has(struct ilka_hash *h, struct ilka_hash_key key)
{
    return -1;
}

ilka_off_t ilka_hash_get(struct ilka_hash *h, struct ilka_hash_key key)
{
    return 0;
}


// -----------------------------------------------------------------------------
// write
// -----------------------------------------------------------------------------

struct ilka_hash_ret ilka_hash_del(struct ilka_hash *h, struct ilka_hash_key key)
{
    return make_ret(ret_err, 0);
}

struct ilka_hash_ret ilka_hash_put(
        struct ilka_hash *h, struct ilka_hash_key key, ilka_off_t value)
{
    return make_ret(ret_err, 0);
}

struct ilka_hash_ret ilka_hash_xchg(
        struct ilka_hash *h, struct ilka_hash_key key, ilka_off_t value)
{
    return make_ret(ret_err, 0);
}

struct ilka_hash_ret ilka_hash_cmp_xchg(
        struct ilka_hash *h,
        struct ilka_hash_key key,
        ilka_off_t expected,
        ilka_off_t value)
{
    return make_ret(ret_err, 0);
}
