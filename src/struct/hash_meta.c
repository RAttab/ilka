/* hash_meta.c
   Rémi Attab (remi.attab@gmail.com), 25 Nov 2015
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// meta
// -----------------------------------------------------------------------------

struct ilka_packed hash_meta
{
    size_t len;
    ilka_off_t tables;
};

static size_t meta_len(struct ilka_hash *ht)
{
    const struct hash_meta *meta =
        ilka_read(ht->region, ht->meta, sizeof(struct hash_meta));

    // morder_relaxed: len is an estimate and is expected to be off within a
    // quiescent period so we don't put any ordering constraints on it.
    return ilka_atomic_load(&meta->len, morder_relaxed);
}

static void meta_update_len(struct ilka_hash *ht, int value)
{
    struct hash_meta *meta =
        ilka_write(ht->region, ht->meta, sizeof(struct hash_meta));

    // morder_relaxed: len is an estimate and is expected to be off within a
    // quiescent period so we don't put any ordering constraints on it.
    (void) ilka_atomic_fetch_add(&meta->len, value, morder_relaxed);
}

static const struct hash_table * meta_table(struct ilka_hash *ht)
{
    const struct hash_meta *meta =
        ilka_read(ht->region, ht->meta, sizeof(struct hash_meta));

    ilka_off_t table_off = ilka_atomic_load(&meta->tables, morder_relaxed);
    const struct hash_table *table = NULL;

    while (table_off) {
        table = table_read(ht, table_off);
        if (!ilka_atomic_load(&table->marked, morder_relaxed)) break;

        table_off = ilka_atomic_load(&table->next, morder_relaxed);
    }

    return table;
}

static const struct hash_table * meta_ensure_table(struct ilka_hash *ht, size_t cap)
{
    const struct hash_meta *meta =
        ilka_read(ht->region, ht->meta, sizeof(struct hash_meta));

    ilka_off_t table_off = ilka_atomic_load(&meta->tables, morder_relaxed);

    if (!table_off) {
        ilka_off_t new_off = table_alloc(ht, cap);
        if (!new_off) return NULL;

        struct hash_meta *wmeta =
            ilka_write(ht->region, ht->meta, sizeof(struct hash_meta));

        if (ilka_atomic_cmp_xchg(&wmeta->tables, &table_off, new_off, morder_release))
            table_off = new_off;
        else ilka_free(ht->region, new_off, table_len(cap));
    }

    ilka_assert(table_off, "unexpected nil table offset");
    return table_read(ht, table_off);
}

static void meta_clean_tables(struct ilka_hash *ht)
{
    struct hash_meta *meta =
        ilka_write(ht->region, ht->meta, sizeof(struct hash_meta));

    ilka_off_t new_head;
    ilka_off_t old_head = ilka_atomic_load(&meta->tables, morder_relaxed);
    do {
        new_head = old_head;

        while (new_head) {
            const struct hash_table *table = table_read(ht, new_head);
            if (!ilka_atomic_load(&table->marked, morder_relaxed)) break;
            new_head = table->next;
        }

        if (new_head == old_head) return;
    } while (!ilka_atomic_cmp_xchg(&meta->tables, &old_head, new_head, morder_relaxed));

    ilka_off_t off = old_head;
    while (off != new_head) {
        const struct hash_table *table = table_read(ht, off);
        table_defer_free(ht, table);
        off = ilka_atomic_load(&table->next, morder_relaxed);
    }
}
