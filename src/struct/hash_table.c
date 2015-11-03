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
    // Must be first for table_read to work properly.
    size_t cap;

    struct ilka_list_node next;

    // Helps make the struct self-sufficient (aka. no need to pass extra
    // parameters all over the place).
    ilka_off_t table_off;

    // Avoids invalidating the table header when updating buckets.
    uint64_t padding[5];

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
    if (!off) return 0;

    struct hash_table *table = ilka_write(ht->region, off, len);
    memset(table, 0, len);

    table->cap = cap;
    table->table_off = off;

    return off;
}

static bool table_free(struct ilka_hash *ht, const struct hash_table *table)
{
    ilka_off_t next_off = ilka_list_next(ht->tables, &table->next);
    ilka_assert(next_off != ILKA_LIST_ERROR, "unexpected error from ilka_list_next");
    if (next_off) {
        ilka_fail("unable to free with concurrent writes");
        return false;
    }

    for (size_t i = 0; i < table->cap; ++i) {
        const struct hash_bucket* bucket = &table->buckets[i];

        switch (state_get(bucket->key)) {
        case state_nil: break;
        case state_tomb: break;

        case state_set:
            key_free(ht, state_clear(bucket->key));
            break;

        case state_move:
            ilka_fail("unable to free with concurrent writes");
            return false;
        }
    }

    return true;
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

struct table_window
{
    const struct hash_table *table;
    size_t start;
    struct hash_bucket *frames[2];
};

static struct hash_bucket *table_window_bucket(
        const struct table_window *wnd, size_t i)
{
    if (wnd->start + i < wnd->table->cap)
        return &wnd->frames[0][i];
    return &wnd->frames[1][wnd->start + i - wnd->table->cap];
}

static struct table_window table_write_window(
        struct ilka_hash *ht, const struct hash_table *table, size_t start)
{
    ilka_off_t buckets_off =
        table->table_off + offsetof(struct hash_table, buckets);

    struct table_window window = {
        .table = table,
        .start = start,
        .frames = { 0, 0 }
    };

    size_t len = probe_window;
    if (start + len > table->cap) len = table->cap - start;

    window.frames[0] = ilka_write(ht->region,
            buckets_off + (start * sizeof(struct hash_bucket)),
            len * sizeof(struct hash_bucket));

    if (len == probe_window) return window;

    window.frames[1] = ilka_write(ht->region,
            buckets_off, (probe_window - len) * sizeof(struct hash_bucket));

    return window;
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
    ilka_assert(next != ILKA_LIST_ERROR, "unexpected error from ilka_list_next");
    if (!next) return (struct table_ret) { ret_ok, NULL };

    const struct hash_table *dst_table = table_read(ht, next);

    struct table_window window = table_write_window(ht, src_table, start);
    for (size_t i = 0; i < len; ++i) {
        struct hash_bucket *src = table_window_bucket(&window, i);

        if (!bucket_lock(ht, src)) continue;

        struct hash_key key = key_from_off(ht, state_clear(src->key));
        ilka_off_t val = state_clear(src->val);

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
    if (!next) return (struct table_ret) { ret_err, 0 };

    struct hash_table *cur = table_write(ht, table);

    int lret = ilka_list_set(ht->tables, &cur->next, next);
    ilka_assert(lret >= 0, "unexpected error from ilka_list_set");
    if (!lret) {
        ilka_free(ht->region, next, table_len(cap));
        return table_move(ht, table, start, probe_window);
    }

    struct table_ret tret = table_move(ht, table, 0, table->cap);
    if (tret.code == ret_err) {
        // \todo: we need to recover the table... somehow...
        return tret;
    }

    lret = ilka_list_del(ht->tables, &cur->next);
    ilka_assert(lret >= 0, "unexpected error from ilka_list_del");
    if (lret) ilka_defer_free(ht->region, table->table_off, table_len(table->cap));

    return (struct table_ret) { ret_ok, table_read(ht, next) };
}

static bool table_reserve(
        struct ilka_hash *ht,
        const struct hash_table *table,
        size_t cap)
{
    if (cap <= table->cap) return true;

    ilka_off_t next = table_alloc(ht, cap);
    if (!next) return false;

    struct hash_table *cur = table_write(ht, table);

    int lret = ilka_list_set(ht->tables, &cur->next, next);
    ilka_assert(lret >= 0, "unexpected error from ilka_list_set");
    if (!lret) {
        ilka_free(ht->region, next, table_len(cap));

        next = ilka_list_next(ht->tables, &table->next);
        ilka_assert(next != ILKA_LIST_ERROR, "unexpected error from ilka_list_next");
        return table_reserve(ht, table_read(ht, next), cap);
    }

    struct table_ret tret = table_move(ht, table, 0, table->cap);
    if (tret.code == ret_err) {
        // \todo: we need to recover the table... somehow...
        return false;
    }

    lret = ilka_list_del(ht->tables, &cur->next);
    ilka_assert(lret >= 0, "unexpected error from ilka_list_del");
    if (lret) ilka_defer_free(ht->region, table->table_off, table_len(table->cap));

    return true;
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
    struct table_window window = table_write_window(ht, table, start);

    for (size_t i = 0; i < probe_window; ++i) {
        struct hash_bucket *bucket = table_window_bucket(&window, i);

        struct ilka_hash_ret ret = bucket_put(ht, bucket, key, value);
        if (ret.code == ret_skip) continue;
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

static struct ilka_hash_ret table_xchg(
        struct ilka_hash *ht,
        const struct hash_table *table,
        struct hash_key *key,
        ilka_off_t expected,
        ilka_off_t value)
{
    size_t start = key_hash(key) % table->cap;
    struct table_window window = table_write_window(ht, table, start);

    for (size_t i = 0; i < probe_window; ++i) {
        struct hash_bucket *bucket = table_window_bucket(&window, i);

        struct ilka_hash_ret ret = bucket_xchg(ht, bucket, key, expected, value);
        if (ret.code == ret_skip) continue;
        if (ret.code == ret_resize) break;
        return ret;
    }

    struct table_ret ret = table_move(ht, table, start, probe_window);
    if (ret.code == ret_err) return make_ret(ret_err, 0);
    if (ret.table) return table_xchg(ht, ret.table, key, expected, value);

    return make_ret(ret_stop, 0);
}

static struct ilka_hash_ret table_del(
        struct ilka_hash *ht,
        const struct hash_table *table,
        struct hash_key *key,
        ilka_off_t expected)
{
    size_t start = key_hash(key) % table->cap;
    struct table_window window = table_write_window(ht, table, start);

    for (size_t i = 0; i < probe_window; ++i) {
        struct hash_bucket *bucket = table_window_bucket(&window, i);

        struct ilka_hash_ret ret = bucket_del(ht, bucket, key, expected);
        if (ret.code == ret_skip) continue;
        if (ret.code == ret_resize) break;
        return ret;
    }

    struct table_ret ret = table_move(ht, table, start, probe_window);
    if (ret.code == ret_err) return make_ret(ret_err, 0);
    if (ret.table) return table_del(ht, ret.table, key, expected);

    return make_ret(ret_stop, 0);
}
