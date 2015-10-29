/* hash_table.c
   Rémi Attab (remi.attab@gmail.com), 28 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// prototypes
// -----------------------------------------------------------------------------

static struct ilka_hash_ret table_put(
        struct ilka_hash *ht,
        const struct hash_table *table,
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

    uint64_t padding[4];
    struct hash_bucket buckets[];
};


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static size_t table_len(size_t cap)
{
    return sizeof(struct hash_table) + cap * sizeof(struct hash_bucket);
}

static ilka_off_t table_alloc(struct ilka_hash *ht, size_t cap)
{
    size_t len = table_len(cap);
    ilka_off_t off = ilka_alloc(ht->region, len);
    struct hash_table *table = ilka_write(ht->region, off, len);
    memset(table, 0, len);

    table->cap = cap;
    table->table_off = off;
    table->buckets_off = off + offsetof(struct hash_table, buckets);

    return off;
}


// -----------------------------------------------------------------------------
// read-write
// -----------------------------------------------------------------------------

static const struct hash_table * table_read(struct ilka_hash *ht, ilka_off_t off)
{
    const size_t *cap = ilka_read(ht->region, off, sizeof(size_t));
    return ilka_read(ht->region, off, table_len(*cap));
}

static struct hash_table * table_write(
        struct ilka_hash *ht, const struct hash_table *table)
{
    return ilka_write(ht->region, table->table_off, sizeof(struct hash_table));
}

static struct hash_bucket * table_write_bucket(
        struct ilka_hash *ht, const struct hash_table *table, size_t i)
{
    return ilka_write(ht->region,
            table->buckets_off + i * sizeof(struct hash_bucket),
            sizeof(struct hash_bucket));
}

static struct hash_bucket * table_write_window(
        struct ilka_hash *ht, const struct hash_table *table, size_t start)
{
    return ilka_write(ht->region,
            table->buckets_off + start * sizeof(struct hash_bucket),
            probe_window * sizeof(struct hash_bucket));
}


// -----------------------------------------------------------------------------
// resize
// -----------------------------------------------------------------------------

static struct table_ret table_move(
        struct ilka_hash *ht,
        const struct hash_table *src_table,
        size_t start,
        size_t len)
{
    ilka_off_t next = ilka_list_next(ht->tables, &src_table->next);
    if (!next) return (struct table_ret) { ret_ok, NULL };

    const struct hash_table *dst_table = table_read(ht, next);

    struct hash_bucket *src = table_write_window(ht, src_table, start);
    for (size_t i = 0; i < len; ++i) {
        if (!bucket_lock(ht, src)) continue;

        struct hash_key key = key_from_off(ht, state_clear(src[i].key));
        ilka_off_t val = state_clear(src[i].val);

        struct ilka_hash_ret ret = table_put(ht, dst_table, &key, val);
        if (ret.code == ret_err) return (struct table_ret) { ret_err, NULL };

        ilka_assert(ret.code == ret_ok || ret.code == ret_stop,
                "unexpected ret code: %d", ret.code);

        // morder_relaxed: these are not linearlization point and just purely
        // bookeeping so no ordering requirements applies.
        bucket_tomb_key(ht, &src->key, morder_relaxed);
        bucket_tomb_val(&src->val, morder_relaxed);
    }

    return (struct table_ret) { ret_ok, dst_table };
}

static size_t table_resize_cap(const struct hash_table *table, size_t start)
{
    size_t tombstones = 0;
    for (size_t i = 0; i < probe_window; ++i) {
        size_t index = (start + i) % table->cap;
        const struct hash_bucket *bucket = &table->buckets[index];

        ilka_off_t key = ilka_atomic_load(&bucket->key, morder_relaxed);
        if (state_get(key) == state_tomb) {
            tombstones++;
            continue;
        }

        ilka_off_t val = ilka_atomic_load(&bucket->val, morder_relaxed);
        if (state_get(val) == state_tomb)
            tombstones++;
    }

    return tombstones < grow_threshold ? table->cap * 2 : table->cap;
}

static struct table_ret table_resize(
        struct ilka_hash *ht,
        const struct hash_table *table,
        size_t start)
{
    size_t cap = table_resize_cap(table, start);
    ilka_off_t next = table_alloc(ht, cap);
    struct hash_table *cur = table_write(ht, table);

    if (!ilka_list_set(ht->tables, &cur->next, next)) {
        ilka_free(ht->region, next, table_len(cap));
        return table_move(ht, table, start, probe_window);
    }

    struct table_ret ret = table_move(ht, table, 0, table->cap);
    if (ret.code == ret_err) {
        // \todo: we need to recover the table... somehow...
        return ret;
    }

    if (ilka_list_del(ht->tables, &cur->next))
        ilka_defer_free(ht->region, table->table_off, table_len(table->cap));

    return (struct table_ret) { ret_ok, table_read(ht, next) };
}


// -----------------------------------------------------------------------------
// ops
// -----------------------------------------------------------------------------

static struct ilka_hash_ret table_get(
        struct ilka_hash *ht,
        const struct hash_table *table,
        struct hash_key *key)
{
    size_t start = key_hash(key) % table->cap;
    for (size_t i = 0; i < probe_window; ++i) {
        size_t index = (start + i) % table->cap;
        const struct hash_bucket *bucket = &table->buckets[index];

        struct ilka_hash_ret ret = bucket_get(ht, bucket, key);
        if (ret.code == ret_skip) continue;
        if (ret.code == ret_stop) break;
        if (ret.code == ret_resize) break;
        return ret;
    }

    struct table_ret ret = table_move(ht, table, start, probe_window);
    if (ret.code == ret_err) return make_ret(ret_err, 0);
    if (ret.table) return table_get(ht, ret.table, key);

    return make_ret(ret_stop, 0);
}

static struct ilka_hash_ret table_put(
        struct ilka_hash *ht,
        const struct hash_table *table,
        struct hash_key *key,
        ilka_off_t value)
{
    size_t start = key_hash(key) % table->cap;
    struct hash_bucket *buckets = table_write_window(ht, table, start);

    for (size_t i = 0; i < probe_window; ++i) {
        size_t index = (start + i) % table->cap;
        struct hash_bucket *bucket = &buckets[index];

        struct ilka_hash_ret ret = bucket_put(ht, bucket, key, value);
        if (ret.code == ret_skip) continue;
        if (ret.code == ret_stop) return ret;
        if (ret.code == ret_resize) break;
        return ret;
    }

    struct table_ret ret = table_move(ht, table, start, probe_window);
    if (ret.code == ret_err) return make_ret(ret_err, 0);
    if (ret.table) return table_put(ht, ret.table, key, value);

    ret = table_resize(ht, table, start);
    if (ret.code == ret_err) return make_ret(ret_err, 0);
    return table_put(ht, ret.table, key, value);
}
