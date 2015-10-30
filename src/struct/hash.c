/* hash.c
   Rémi Attab (remi.attab@gmail.com), 20 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "hash.h"
#include "list.h"

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

    struct ilka_list *tables;
};


// -----------------------------------------------------------------------------
// implementation
// -----------------------------------------------------------------------------

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
// basics
// -----------------------------------------------------------------------------

struct ilka_packed hash_meta
{
    size_t len;
    struct ilka_list_node tables;
};

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
    ht->tables = ilka_list_alloc(
            ht->region,
            ht->meta + offsetof(struct hash_meta, tables),
            offsetof(struct hash_table, next));
    if (!ht->tables) goto fail_tables;

    return ht;

    ilka_list_close(ht->tables);
  fail_tables:
    free(ht);
  fail_meta:
    return NULL;
}

bool ilka_hash_free(struct ilka_hash *ht)
{
    ilka_off_t table_off = ilka_list_head(ht->tables);
    while (table_off) {
        const struct hash_table *table = table_read(ht, table_off);

        for (size_t i = 0; i < table->cap; ++i)
            key_free(ht, state_clear(table->buckets[i].key));

        ilka_off_t next = ilka_list_next(ht->tables, &table->next);
        ilka_free(ht->region, table_off, table_len(table->cap));
        table_off = next;
    }

    ilka_free(ht->region, ht->meta, sizeof(struct hash_meta));

    return ilka_hash_close(ht);
}

struct ilka_hash * ilka_hash_open(struct ilka_region *region, ilka_off_t off)
{
    struct ilka_hash *ht = malloc(sizeof(struct ilka_hash));
    if (!ht) goto fail_malloc;

    ht->region = region;
    ht->meta = off;
    ht->tables = ilka_list_open(
            ht->region,
            ht->meta + offsetof(struct hash_meta, tables),
            offsetof(struct hash_table, next));
    if (!ht->tables) goto fail_tables;


    return ht;

    ilka_list_close(ht->tables);
  fail_tables:
    free(ht);
  fail_malloc:
    return NULL;
}

bool ilka_hash_close(struct ilka_hash *ht)
{
    ilka_list_close(ht->tables);
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

size_t ilka_hash_cap(struct ilka_hash *ht)
{
    (void) ht;
    return 0;
}

bool ilka_hash_reserve(struct ilka_hash *ht, size_t cap)
{
    (void) ht;
    (void) cap;
    return false;
}

size_t ilka_hash_len(struct ilka_hash *ht)
{
    (void) ht;
    return 0;
}

bool ilka_hash_resize(struct ilka_hash *ht, size_t len)
{
    (void) ht;
    (void) len;
    return false;
}


// -----------------------------------------------------------------------------
// read
// -----------------------------------------------------------------------------

struct ilka_hash_ret ilka_hash_get(
        struct ilka_hash *ht, const void *key, size_t key_len)
{
    if (check_key(key, key_len)) return make_ret(ret_err, 0);

    ilka_off_t table_off = ilka_list_head(ht->tables);
    if (!table_off) return make_ret(ret_stop, 0);

    struct hash_key hkey = make_key(key, key_len);
    return table_get(ht, table_read(ht, table_off), &hkey);
}


// -----------------------------------------------------------------------------
// write
// -----------------------------------------------------------------------------

struct ilka_hash_ret ilka_hash_put(
        struct ilka_hash *ht, const void *key, size_t key_len, ilka_off_t value)
{
    if (check_key(key, key_len)) return make_ret(ret_err, 0);
    if (check_value("value", value)) return make_ret(ret_err, 0);

    ilka_off_t table_off;
    while (true) {
        table_off = ilka_list_head(ht->tables);
        if (table_off) break;

        struct hash_meta *meta =
            ilka_write(ht->region, ht->meta, sizeof(struct hash_meta));

        table_off = table_alloc(ht, default_cap);
        if (ilka_list_set(ht->tables, &meta->tables, table_off)) break;
        ilka_free(ht->region, table_off, table_len(default_cap));
    }

    struct hash_key hkey = make_key(key, key_len);
    return table_put(ht, table_read(ht, table_off), &hkey, value);
}

struct ilka_hash_ret _ilka_hash_xchg(
        struct ilka_hash *ht,
        const void *key,
        size_t key_len,
        ilka_off_t expected,
        ilka_off_t value)
{
    ilka_off_t table_off = ilka_list_head(ht->tables);
    if (!table_off) return make_ret(ret_stop, 0);

    struct hash_key hkey = make_key(key, key_len);
    return table_xchg(ht, table_read(ht, table_off), &hkey, expected, value);
}

struct ilka_hash_ret ilka_hash_xchg(
        struct ilka_hash *ht, const void *key, size_t key_len, ilka_off_t value)
{
    if (check_key(key, key_len)) return make_ret(ret_err, 0);
    if (check_value("value", value)) return make_ret(ret_err, 0);

    return _ilka_hash_xchg(ht, key, key_len, 0, value);
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

    return _ilka_hash_xchg(ht, key, key_len, expected, value);
}

struct ilka_hash_ret _ilka_hash_del(
        struct ilka_hash *ht,
        const void *key,
        size_t key_len,
        ilka_off_t expected)
{
    ilka_off_t table_off = ilka_list_head(ht->tables);
    if (!table_off) return make_ret(ret_stop, 0);

    struct hash_key hkey = make_key(key, key_len);
    return table_del(ht, table_read(ht, table_off), &hkey, expected);
}

struct ilka_hash_ret ilka_hash_del(
        struct ilka_hash *ht, const void *key, size_t key_len)
{
    if (check_key(key, key_len)) return make_ret(ret_err, 0);

    return _ilka_hash_del(ht, key, key_len, 0);
}

struct ilka_hash_ret ilka_hash_cmp_del(
        struct ilka_hash *ht,
        const void *key,
        size_t key_len,
        ilka_off_t expected)
{
    if (check_key(key, key_len)) return make_ret(ret_err, 0);
    if (check_value("expected", expected)) return make_ret(ret_err, 0);

    return _ilka_hash_del(ht, key, key_len, expected);
}
