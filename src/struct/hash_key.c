/* hash_key.c
   Rémi Attab (remi.attab@gmail.com), 28 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// key
// -----------------------------------------------------------------------------

struct hash_key
{
    size_t len;
    const void *data;

    uint64_t hash;
    ilka_off_t off;
};

static uint64_t _key_hash(const void *data, size_t len)
{
    struct siphash sip;
    sip24_init(&sip, &sipkey);
    sip24_update(&sip, data, len);
    return sip24_final(&sip);
}

static uint64_t key_hash(struct hash_key *key)
{
    if (key->hash) return key->hash;
    return key->hash = _key_hash(key->data, key->len);
}

static struct hash_key make_key(const void *data, size_t len)
{
    return (struct hash_key) { len, data, _key_hash(data, len), 0 };
}

static bool key_equals(struct hash_key *lhs, struct hash_key *rhs)
{
    if (lhs->len != rhs->len) return false;
    return !memcmp(lhs->data, rhs->data, lhs->len);
}

static ilka_off_t key_alloc(struct ilka_hash *ht, struct hash_key *key)
{
    if (key->off) return key->off;

    size_t n = sizeof(size_t) + key->len;

    key->off = ilka_alloc(ht->region, n);
    if (!key->off) return key->off;

    size_t *p = ilka_write(ht->region, key->off, n);
    *p = key->len;
    memcpy(p + 1, key->data, key->len);

    // morder_release: make sure the key is committed before it's published.
    ilka_atomic_fence(morder_release);

    return key->off;
}

static void key_free(struct ilka_hash *ht, ilka_off_t off)
{
    const size_t *len = ilka_read(ht->region, off, sizeof(size_t));
    ilka_defer_free(ht->region, off, sizeof(size_t) + *len);
}

static struct hash_key key_from_off(struct ilka_hash *ht, ilka_off_t off)
{
    const size_t *len = ilka_read(ht->region, off, sizeof(size_t));
    const void *data = ilka_read(ht->region, off + sizeof(size_t), *len);

    return (struct hash_key) { *len, data, 0, off };
}

static bool key_check(struct ilka_hash *ht, ilka_off_t off, struct hash_key *rhs)
{
    struct hash_key lhs = key_from_off(ht, off);
    return key_equals(&lhs, rhs);
}
