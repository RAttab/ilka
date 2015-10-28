/* hash_table.c
   Rémi Attab (remi.attab@gmail.com), 28 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// table
// -----------------------------------------------------------------------------

static struct table_ret table_move_window(
        const struct hash_table *src_table, struct ilka_region *r, size_t start);

struct ilka_packed hash_table
{
    size_t cap; // must be first.
    ilka_off_t next;
    ilka_off_t buckets_off;

    uint64_t padding[5];
    struct hash_bucket buckets[];
};

static size_t table_len(size_t cap)
{
    return sizeof(struct hash_table) + cap * sizeof(struct hash_bucket);
}

static const struct hash_table * table_read(struct ilka_region *r, ilka_off_t off)
{
    const size_t *cap = ilka_read(r, off, sizeof(size_t));
    return ilka_read(r, off, table_len(*cap));
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

static struct ilka_hash_ret table_move_bucket(
        struct ilka_region *r,
        struct hash_bucket *src,
        const struct hash_table *dst_table,
        struct hash_key *key,
        ilka_off_t *key_off)
{
    size_t dst_start = key_hash(key) % dst_table->cap;
    struct hash_bucket *dst = table_write_window(dst_table, r, dst_start);

    ilka_off_t value = state_clear(src->val);
    for (size_t j = 0; j < probe_window; ++j) {

        struct ilka_hash_ret ret = bucket_put(dst, r, key, value, key_off);
        if (ret.code == ret_skip) continue;
        if (ret.code == ret_err) return ret;

        if (ret.code == ret_ok || ret.code == ret_stop)
            return make_ret(ret_ok, 0);

        if (ret.code == ret_resize) {
            struct table_ret ret = table_move_window(dst_table, r, dst_start);
            if (ret.code == ret_err) return make_ret(ret.code, 0);

            return table_move_bucket(r, src, ret.table, key, key_off);
        }

        ilka_unreachable();
    }

    ilka_todo("table_move_bucket needs to resize when full.");
}

static struct table_ret table_move_window(
        const struct hash_table *src_table, struct ilka_region *r, size_t start)
{
    ilka_off_t next = ilka_atomic_load(&src_table->next, morder_relaxed);
    if (!next) return (struct table_ret) { ret_ok, NULL };

    const struct hash_table *dst_table = table_read(r, next);

    struct hash_bucket *src = table_write_window(src_table, r, start);
    for (size_t i = 0; i < probe_window; ++i) {
        if (!bucket_lock(src, r)) continue;

        ilka_off_t key_off = state_clear(src[i].key);
        struct hash_key key = key_from_off(r, key_off);

        struct ilka_hash_ret ret =
            table_move_bucket(r, &src[i], dst_table, &key, &key_off);
        if (ret.code == ret_err) return (struct table_ret) { ret_err, NULL };

        ilka_assert(ret.code == ret_ok, "unexpected ret code: %d", ret.code);

        // morder_relaxed: these are not linearlization point and just purely
        // bookeeping so no ordering requirements applies.
        bucket_tomb_key(&src->key, r, morder_relaxed);
        bucket_tomb_val(&src->val, morder_relaxed);
    }

    return (struct table_ret) { ret_ok, dst_table };
}

static struct ilka_hash_ret table_get(
        const struct hash_table *t,
        struct ilka_region *r,
        struct hash_key *key)
{
    size_t start = key_hash(key) % t->cap;
    for (size_t i = 0; i < probe_window; ++i) {
        const struct hash_bucket *b = &t->buckets[start + i % t->cap];

        struct ilka_hash_ret ret = bucket_get(b, r, key);
        if (ret.code == ret_skip) continue;
        if (ret.code == ret_stop) break;
        if (ret.code == ret_resize) break;
        return ret;
    }

    struct table_ret ret = table_move_window(t, r, start);
    if (ret.code == ret_err) return make_ret(ret_err, 0);
    if (ret.table) return table_get(ret.table, r, key);

    return make_ret(ret_stop, 0);
}


