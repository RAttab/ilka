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

#include "hash_key.c"
#include "hash_bucket.c"
#include "hash_table.c"


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
        ilka_free(h->r, table, table_len(ht->cap));
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
    (void) h;
    return 0;
}

bool ilka_hash_reserve(struct ilka_hash *h, size_t cap)
{
    (void) h;
    (void) cap;
    return false;
}

size_t ilka_hash_len(struct ilka_hash *h)
{
    (void) h;
    return 0;
}

bool ilka_hash_resize(struct ilka_hash *h, size_t len)
{
    (void) h;
    (void) len;
    return false;
}


// -----------------------------------------------------------------------------
// read
// -----------------------------------------------------------------------------

int ilka_hash_has(struct ilka_hash *h, const void *key, size_t key_len)
{
    (void) h;
    (void) key;
    (void) key_len;
    return -1;
}

ilka_off_t ilka_hash_get(struct ilka_hash *h, const void *key, size_t key_len)
{
    (void) h;
    (void) key;
    (void) key_len;
    return 0;
}


// -----------------------------------------------------------------------------
// write
// -----------------------------------------------------------------------------

struct ilka_hash_ret ilka_hash_del(
        struct ilka_hash *h, const void *key, size_t key_len)
{
    (void) h;
    (void) key;
    (void) key_len;
    return make_ret(ret_err, 0);
}

struct ilka_hash_ret ilka_hash_put(
        struct ilka_hash *h, const void *key, size_t key_len, ilka_off_t value)
{
    (void) h;
    (void) key;
    (void) key_len;
    (void) value;
    return make_ret(ret_err, 0);
}

struct ilka_hash_ret ilka_hash_xchg(
        struct ilka_hash *h, const void *key, size_t key_len, ilka_off_t value)
{
    (void) h;
    (void) key;
    (void) key_len;
    (void) value;
    return make_ret(ret_err, 0);
}

struct ilka_hash_ret ilka_hash_cmp_xchg(
        struct ilka_hash *h,
        const void *key, size_t key_len,
        ilka_off_t expected,
        ilka_off_t value)
{
    (void) h;
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
