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
// implementation
// -----------------------------------------------------------------------------

struct ilka_hash
{
    struct ilka_region *region;
    ilka_off_t meta;
};

#include "hash_key.c"
#include "hash_bucket.c"
#include "hash_table.c"


// -----------------------------------------------------------------------------
// basics
// -----------------------------------------------------------------------------

struct ilka_packed hash_meta
{
    size_t len;
    struct ilka_list_node tables;
};

static struct ilka_list hash_list(struct ilka_hash *ht)
{
    return ilka_list_open(
            ht->region,
            ht->meta + offsetof(struct hash_meta, tables),
            offsetof(struct hash_table, next));
}

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

  fail_meta:
    free(ht);
    return NULL;
}

bool ilka_hash_free(struct ilka_hash *ht)
{
    struct ilka_list list = hash_list(ht);

    ilka_off_t table_off = ilka_list_head(&list);
    while (table_off) {
        const struct hash_table *table = table_read(ht, table_off);

        for (size_t i = 0; i < table->cap; ++i)
            key_free(ht, state_clear(table->buckets[i].key));

        ilka_off_t next = ilka_list_next(&list, &table->next);
        ilka_free(ht->region, table_off, table_len(table->cap));
        table_off = next;
    }

    ilka_free(ht->region, ht->meta, sizeof(struct hash_meta));

    return ilka_hash_close(ht);
}

struct ilka_hash * ilka_hash_open(struct ilka_region *region, ilka_off_t off)
{
    struct ilka_hash *ht = malloc(sizeof(struct ilka_hash));
    *ht = (struct ilka_hash) { region, off };
    return ht;
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

int ilka_hash_has(struct ilka_hash *ht, const void *key, size_t key_len)
{
    (void) ht;
    (void) key;
    (void) key_len;
    return -1;
}

ilka_off_t ilka_hash_get(struct ilka_hash *ht, const void *key, size_t key_len)
{
    (void) ht;
    (void) key;
    (void) key_len;
    return 0;
}


// -----------------------------------------------------------------------------
// write
// -----------------------------------------------------------------------------

struct ilka_hash_ret ilka_hash_del(
        struct ilka_hash *ht, const void *key, size_t key_len)
{
    (void) ht;
    (void) key;
    (void) key_len;
    return make_ret(ret_err, 0);
}

struct ilka_hash_ret ilka_hash_put(
        struct ilka_hash *ht, const void *key, size_t key_len, ilka_off_t value)
{
    (void) ht;
    (void) key;
    (void) key_len;
    (void) value;
    return make_ret(ret_err, 0);
}

struct ilka_hash_ret ilka_hash_xchg(
        struct ilka_hash *ht, const void *key, size_t key_len, ilka_off_t value)
{
    (void) ht;
    (void) key;
    (void) key_len;
    (void) value;
    return make_ret(ret_err, 0);
}

struct ilka_hash_ret ilka_hash_cmp_xchg(
        struct ilka_hash *ht,
        const void *key, size_t key_len,
        ilka_off_t expected,
        ilka_off_t value)
{
    (void) ht;
    (void) key;
    (void) key_len;
    (void) expected;
    (void) value;
    return make_ret(ret_err, 0);
}


// -----------------------------------------------------------------------------
// compiler shutter-upper.
// -----------------------------------------------------------------------------

void _shutup()
{
    struct hash_key key = make_key(0, 0);
    (void) bucket_get(0, 0, &key);
    (void) bucket_xchg(0, 0, &key, 0, 0);
    (void) bucket_del(0, 0, &key);
    (void) table_write_bucket(0, 0, 0);
    (void) table_get(0, 0, &key);
}
