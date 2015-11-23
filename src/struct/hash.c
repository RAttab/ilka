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

static const size_t probe_window = 8;
static const size_t grow_threshold = 4;
static const size_t default_cap = 8;


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

struct table_ret
{
    enum ret_code code;
    const struct hash_table *table;
};


// -----------------------------------------------------------------------------
// struct
// -----------------------------------------------------------------------------

struct ilka_hash
{
    struct ilka_region *region;
    ilka_off_t meta;
};


// -----------------------------------------------------------------------------
// log
// -----------------------------------------------------------------------------

#ifdef ILKA_HASH_LOG
# define hash_log(t, ...) ilka_log(t, __VA_ARGS__)
#else
# define hash_log(t, ...) do { (void) t; } while (false)
#endif


// -----------------------------------------------------------------------------
// implementation
// -----------------------------------------------------------------------------

static void meta_clean_tables(struct ilka_hash *ht);

#include "hash_key.c"
#include "hash_bucket.c"
#include "hash_table.c"


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static enum ret_code check_key(const void *key, size_t key_len)
{
    if (!key) {
        ilka_fail("invalid nil key");
        return ret_err;
    }

    if (!key_len) {
        ilka_fail("invalid nil key_len");
        return ret_err;
    }

    return ret_ok;
}

static enum ret_code check_value(const char *name, ilka_off_t value)
{
    // Required to disambiguate return values (not-there vs there but wrong
    // value) and the value 0 is used internally to determined whether a bucket
    // was set or not.
    if (!value) {
        ilka_fail("invalid nil value");
        return ret_err;
    }

    if (state_clear(value) != value) {
        ilka_fail("invalid offset for '%s': %lu", name, value);
        return ret_err;
    }

    return ret_ok;
}


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


// -----------------------------------------------------------------------------
// basics
// -----------------------------------------------------------------------------

struct ilka_hash * ilka_hash_alloc(struct ilka_region *region)
{
    struct ilka_hash *ht = malloc(sizeof(struct ilka_hash));
    if (!ht) {
        ilka_fail_errno("out-of-memory for hash struct: %lu",
                sizeof(struct ilka_hash));
        return NULL;
    }

    ht->region = region;
    ht->meta = ilka_alloc(region, sizeof(struct hash_meta));
    if (!ht->meta) goto fail_meta;

    struct hash_meta *meta = ilka_write(region, ht->meta, sizeof(struct hash_meta));
    memset(meta, 0, sizeof(struct hash_meta));
    return ht;

    free(ht);
  fail_meta:
    return NULL;
}

bool ilka_hash_free(struct ilka_hash *ht)
{
    const struct hash_table *table = meta_table(ht);
    if (table && table_free(ht, table)) return false;

    ilka_free(ht->region, ht->meta, sizeof(struct hash_meta));
    return ilka_hash_close(ht);
}

struct ilka_hash * ilka_hash_open(struct ilka_region *region, ilka_off_t off)
{
    struct ilka_hash *ht = malloc(sizeof(struct ilka_hash));
    if (!ht) goto fail_malloc;

    ht->region = region;
    ht->meta = off;

    return ht;

    free(ht);
  fail_malloc:
    return NULL;
}

bool ilka_hash_close(struct ilka_hash *ht)
{
    free(ht);
    return true;
}

ilka_off_t ilka_hash_off(struct ilka_hash *ht)
{
    return ht->meta;
}


// -----------------------------------------------------------------------------
// sizing
// -----------------------------------------------------------------------------

size_t ilka_hash_len(struct ilka_hash *ht)
{
    return meta_len(ht);
}

size_t ilka_hash_cap(struct ilka_hash *ht)
{
    const struct hash_table *table = meta_table(ht);
    while (table) {
        ilka_off_t next = ilka_atomic_load(&table->next, morder_relaxed);
        if (!next) return table->cap;

        table = table_read(ht, next);
    }

    return 0;
}

bool ilka_hash_reserve(struct ilka_hash *ht, size_t cap)
{
    if (!cap) {
        ilka_fail("invalid nil len");
        return false;
    }

    cap = ceil_pow2(cap);

    const struct hash_table *table = meta_ensure_table(ht, cap);
    if (!table) return false;

    return table_reserve(ht, table, cap);
}


// -----------------------------------------------------------------------------
// ops
// -----------------------------------------------------------------------------

struct ilka_hash_ret ilka_hash_get(
        struct ilka_hash *ht, const void *key, size_t key_len)
{
    if (check_key(key, key_len)) return make_ret(ret_err, 0);

    const struct hash_table *table = meta_table(ht);
    if (!table) return make_ret(ret_stop, 0);

    struct hash_key hkey = make_key(key, key_len);
    return table_get(ht, table, &hkey);
}

int ilka_hash_iterate(struct ilka_hash *ht, ilka_hash_fn_t fn, void *data)
{
    const struct hash_table *table = meta_table(ht);
    return table ? table_iterate(ht, table, fn, data) : ret_ok;
}

struct ilka_hash_ret ilka_hash_put(
        struct ilka_hash *ht, const void *key, size_t key_len, ilka_off_t value)
{
    if (check_key(key, key_len)) return make_ret(ret_err, 0);
    if (check_value("value", value)) return make_ret(ret_err, 0);

    const struct hash_table *table = meta_ensure_table(ht, default_cap);
    if (!table) return make_ret(ret_err, 0);

    struct hash_key hkey = make_key(key, key_len);
    struct ilka_hash_ret ret = table_put(ht, table, &hkey, value);

    if (ret.code == ret_ok) meta_update_len(ht, 1);
    if (hkey.off) key_free(ht, hkey.off);
    return ret;
}

static struct ilka_hash_ret hash_xchg(
        struct ilka_hash *ht,
        const void *key,
        size_t key_len,
        ilka_off_t expected,
        ilka_off_t value)
{
    const struct hash_table *table = meta_table(ht);
    if (!table) return make_ret(ret_stop, 0);

    struct hash_key hkey = make_key(key, key_len);
    return table_xchg(ht, table, &hkey, expected, value);
}

struct ilka_hash_ret ilka_hash_xchg(
        struct ilka_hash *ht, const void *key, size_t key_len, ilka_off_t value)
{
    if (check_key(key, key_len)) return make_ret(ret_err, 0);
    if (check_value("value", value)) return make_ret(ret_err, 0);

    return hash_xchg(ht, key, key_len, 0, value);
}

struct ilka_hash_ret ilka_hash_cmp_xchg(
        struct ilka_hash *ht,
        const void *key,
        size_t key_len,
        ilka_off_t expected,
        ilka_off_t value)
{
    if (check_key(key, key_len)) return make_ret(ret_err, 0);
    if (check_value("value", value)) return make_ret(ret_err, 0);
    if (check_value("expected", expected)) return make_ret(ret_err, 0);

    return hash_xchg(ht, key, key_len, expected, value);
}

static struct ilka_hash_ret hash_del(
        struct ilka_hash *ht,
        const void *key,
        size_t key_len,
        ilka_off_t expected)
{
    const struct hash_table *table = meta_table(ht);
    if (!table) return make_ret(ret_stop, 0);

    struct hash_key hkey = make_key(key, key_len);
    struct ilka_hash_ret ret = table_del(ht, table, &hkey, expected);

    if (ret.code == ret_ok) meta_update_len(ht, -1);

    return ret;
}

struct ilka_hash_ret ilka_hash_del(
        struct ilka_hash *ht, const void *key, size_t key_len)
{
    if (check_key(key, key_len)) return make_ret(ret_err, 0);

    return hash_del(ht, key, key_len, 0);
}

struct ilka_hash_ret ilka_hash_cmp_del(
        struct ilka_hash *ht,
        const void *key,
        size_t key_len,
        ilka_off_t expected)
{
    if (check_key(key, key_len)) return make_ret(ret_err, 0);
    if (check_value("expected", expected)) return make_ret(ret_err, 0);

    return hash_del(ht, key, key_len, expected);
}
