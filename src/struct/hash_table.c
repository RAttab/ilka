/* hash_table.c
   Rémi Attab (remi.attab@gmail.com), 28 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// prototypes
// -----------------------------------------------------------------------------

static struct ilka_hash_ret table_put(
        const struct hash_table *t,
        struct ilka_region *r,
        struct hash_key *key,
        ilka_off_t value);


// -----------------------------------------------------------------------------
// table
// -----------------------------------------------------------------------------

struct ilka_packed hash_table
{
    size_t cap; // must be first.
    struct ilka_list_node next;

    ilka_off_t table_off;
    ilka_off_t buckets_off;
    ilka_off_t list_head;

    uint64_t padding[3];
    struct hash_bucket buckets[];
};


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static size_t table_len(size_t cap)
{
    return sizeof(struct hash_table) + cap * sizeof(struct hash_bucket);
}

static ilka_off_t table_alloc(struct ilka_region *r, size_t cap, ilka_off_t list_head)
{
    size_t len = table_len(cap);
    ilka_off_t off = ilka_alloc(r, len);
    struct hash_table *table = ilka_write(r, off, len);
    memset(table, 0, len);

    table->cap = cap;
    table->table_off = off;
    table->buckets_off = off + offsetof(struct hash_table, buckets);
    table->list_head = list_head;

    return off;
}

static struct ilka_list table_list(
        const struct hash_table *t, struct ilka_region *r)
{
    return ilka_list_open(r, t->list_head, offsetof(struct hash_table, next));
}


// -----------------------------------------------------------------------------
// read-write
// -----------------------------------------------------------------------------

static const struct hash_table * table_read(struct ilka_region *r, ilka_off_t off)
{
    const size_t *cap = ilka_read(r, off, sizeof(size_t));
    return ilka_read(r, off, table_len(*cap));
}

static struct hash_table * table_write(
        const struct hash_table *t, struct ilka_region *r)
{
    return ilka_write(r, t->table_off, sizeof(struct hash_table));
}

static struct hash_bucket * table_write_bucket(
        const struct hash_table *t, struct ilka_region *r, size_t i)
{
    return ilka_write(r,
            t->buckets_off + i * sizeof(struct hash_bucket),
            sizeof(struct hash_bucket));
}

static struct hash_bucket * table_write_window(
        const struct hash_table *t, struct ilka_region *r, size_t start)
{
    return ilka_write(r,
            t->buckets_off + start * sizeof(struct hash_bucket),
            probe_window * sizeof(struct hash_bucket));
}


// -----------------------------------------------------------------------------
// resize
// -----------------------------------------------------------------------------

static struct table_ret table_move(
        const struct hash_table *src_table,
        struct ilka_region *r,
        size_t start,
        size_t len)
{
    struct ilka_list list = table_list(src_table, r);

    ilka_off_t next = ilka_list_next(&list, &src_table->next);
    if (!next) return (struct table_ret) { ret_ok, NULL };

    const struct hash_table *dst_table = table_read(r, next);

    struct hash_bucket *src = table_write_window(src_table, r, start);
    for (size_t i = 0; i < len; ++i) {
        if (!bucket_lock(src, r)) continue;

        struct hash_key key = key_from_off(r, state_clear(src[i].key));
        ilka_off_t val = state_clear(src[i].val);

        struct ilka_hash_ret ret = table_put(dst_table, r, &key, val);
        if (ret.code == ret_err) return (struct table_ret) { ret_err, NULL };

        ilka_assert(ret.code == ret_ok || ret.code == ret_stop,
                "unexpected ret code: %d", ret.code);

        // morder_relaxed: these are not linearlization point and just purely
        // bookeeping so no ordering requirements applies.
        bucket_tomb_key(&src->key, r, morder_relaxed);
        bucket_tomb_val(&src->val, morder_relaxed);
    }

    return (struct table_ret) { ret_ok, dst_table };
}

static size_t table_resize_cap(const struct hash_table *t, size_t start)
{
    size_t tombstones = 0;
    for (size_t i = 0; i < probe_window; ++i) {
        const struct hash_bucket *b = &t->buckets[(start + i) % t->cap];

        ilka_off_t key = ilka_atomic_load(&b->key, morder_relaxed);
        if (state_get(key) == state_tomb) {
            tombstones++;
            continue;
        }

        ilka_off_t val = ilka_atomic_load(&b->val, morder_relaxed);
        if (state_get(val) == state_tomb)
            tombstones++;
    }

    return tombstones < grow_threshold ? t->cap * 2 : t->cap;
}

static struct table_ret table_resize(
        const struct hash_table *t,
        struct ilka_region *r,
        size_t start)
{
    size_t cap = table_resize_cap(t, start);
    ilka_off_t next = table_alloc(r, cap, t->list_head);
    struct hash_table *cur = table_write(t, r);

    struct ilka_list list = table_list(t, r);

    if (!ilka_list_set(&list, &cur->next, next)) {
        ilka_free(r, next, table_len(cap));
        return table_move(t, r, start, probe_window);
    }

    struct table_ret ret = table_move(t, r, 0, t->cap);
    if (ret.code == ret_err) {
        // \todo: we need to recover the table... somehow...
        return ret;
    }

    if (ilka_list_del(&list, &cur->next))
        ilka_defer_free(r, t->table_off, table_len(t->cap));

    return (struct table_ret) { ret_ok, table_read(r, next) };
}


// -----------------------------------------------------------------------------
// ops
// -----------------------------------------------------------------------------

static struct ilka_hash_ret table_get(
        const struct hash_table *t,
        struct ilka_region *r,
        struct hash_key *key)
{
    size_t start = key_hash(key) % t->cap;
    for (size_t i = 0; i < probe_window; ++i) {
        const struct hash_bucket *b = &t->buckets[(start + i) % t->cap];

        struct ilka_hash_ret ret = bucket_get(b, r, key);
        if (ret.code == ret_skip) continue;
        if (ret.code == ret_stop) break;
        if (ret.code == ret_resize) break;
        return ret;
    }

    struct table_ret ret = table_move(t, r, start, probe_window);
    if (ret.code == ret_err) return make_ret(ret_err, 0);
    if (ret.table) return table_get(ret.table, r, key);

    return make_ret(ret_stop, 0);
}

static struct ilka_hash_ret table_put(
        const struct hash_table *t,
        struct ilka_region *r,
        struct hash_key *key,
        ilka_off_t value)
{
    size_t start = key_hash(key) % t->cap;
    struct hash_bucket *buckets = table_write_window(t, r, start);

    for (size_t i = 0; i < probe_window; ++i) {
        struct hash_bucket *b = &buckets[(start + i) % t->cap];

        struct ilka_hash_ret ret = bucket_put(b, r, key, value);
        if (ret.code == ret_skip) continue;
        if (ret.code == ret_stop) return ret;
        if (ret.code == ret_resize) break;
        return ret;
    }

    struct table_ret ret = table_move(t, r, start, probe_window);
    if (ret.code == ret_err) return make_ret(ret_err, 0);
    if (ret.table) return table_put(ret.table, r, key, value);

    ret = table_resize(t, r, start);
    if (ret.code == ret_err) return make_ret(ret_err, 0);
    return table_put(ret.table, r, key, value);
}
