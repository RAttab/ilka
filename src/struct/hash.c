/* hash.c
   Rémi Attab (remi.attab@gmail.com), 20 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "hash.h"

#include "siphash.h"
#include <string.h>
#include <stdlib.h>


// -----------------------------------------------------------------------------
// config
// -----------------------------------------------------------------------------

static const uint64_t state_mask = 0x3UL;
static const uint64_t mark_mask = 0x1UL;

// must be consistent across restarts. We could save it in the region and
// randomize on every new hash created which would make the whole thing more DoS
// resistant. Will deal with that stuff later.
static struct sipkey sipkey = {{ 0xc60243215c6ee9d1, 0xcd9cc80b04763259 }};


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


// -----------------------------------------------------------------------------
// table
// -----------------------------------------------------------------------------

struct ilka_packed hash_bucket
{
    ilka_off_t key;
    ilka_off_t value;
};

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
            key_free(h->r, ht->buckets[i].key & ~state_mask);

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
    return (struct ilka_hash_ret) { -1, 0 };
}

struct ilka_hash_ret ilka_hash_put(
        struct ilka_hash *h, struct ilka_hash_key key, ilka_off_t value)
{
    return (struct ilka_hash_ret) { -1, 0 };
}
