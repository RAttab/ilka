/* key.c
   Rémi Attab (remi.attab@gmail.com), 06 Feb 2016
   FreeBSD-style copyright and disclaimer apply
*/

#include "key.h"
#include "utils/utils.h"
#include <stdlib.h>
#include <string.h>


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static inline bool is_internal(struct ilka_key *key)
{
    return key->cap <= sizeof(key->data.in);
}

static inline const uint8_t * read_data(struct ilka_key *key)
{
    return is_internal(key) ? key->data.in : key->data.out;
}

static inline uint8_t * write_data(struct ilka_key *key)
{
    return is_internal(key) ? key->data.in : key->data.out;
}


// -----------------------------------------------------------------------------
// basics
// -----------------------------------------------------------------------------

bool ilka_key_init(struct ilka_key* key)
{
    memset(key, 0, sizeof(*key));

    key->cap = sizeof(key->data.in);

    return true;
}

void ilka_key_free(struct ilka_key *key)
{
    if (is_internal(key)) return;
    free(key->data.out);
}

const uint8_t *ilka_key_data(struct ilka_key *key)
{
    return read_data(key);
}

void ilka_key_clear(struct ilka_key *key)
{
    ilka_key_free(key);
    ilka_key_init(key);
}

bool ilka_key_reserve(struct ilka_key *key, size_t cap)
{
    ilka_assert(cap <= (1UL << 32) - 1,
            "invalid cap value: %lu > 0xFFFFFFFF", cap);

    if (key->cap >= cap) return true;

    uint32_t new_cap = key->cap ? key->cap : 1;
    while (new_cap < cap) new_cap *= 2;

    uint8_t *new_data = malloc(new_cap);
    if (!new_data) {
        ilka_fail("out-of-memory for key storage: %lu", cap);
        return false;
    }

    memcpy(new_data, read_data(key), key->len);
    key->data.out = new_data;
    key->cap = new_cap;

    return true;
}


int ilka_key_cmp(struct ilka_key *lhs, struct ilka_key *rhs)
{
    size_t len = lhs->len < rhs->len ? lhs->len : rhs->len;

    int result = memcmp(read_data(lhs), read_data(rhs), len);
    if (result) return result;

    if (lhs->len == rhs->len) return 0;
    return lhs->len < rhs->len ? -1 : 1;
}

bool ilka_key_copy(struct ilka_key *src, struct ilka_key *dest)
{
    ilka_assert(src != dest, "unable to self-copy");

    if (!ilka_key_reserve(dest, src->len)) return false;

    memcpy(write_data(dest), read_data(src), src->len);
    return true;
}


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

ilka_off_t ilka_key_region_save(
        struct ilka_region *region, struct ilka_key *key)
{
    uint32_t region_len = key->len + sizeof(key->len);

    ilka_off_t off = ilka_alloc(region, region_len);
    if (!off) return 0;

    uint32_t *ptr = ilka_write(region, off, region_len);
    *ptr = key->len;
    memcpy(ptr + 1, read_data(key), key->len);

    return off;
}

struct ilka_key_it ilka_key_region_load(
        struct ilka_region *region, ilka_off_t off, struct ilka_key_it it)
{
    const uint32_t *len = ilka_read(region, off, sizeof(*len));
    const uint8_t *data = ilka_read(region, off + sizeof(*len), *len);

    return ilka_key_write_bytes(it, data, *len);
}

void ilka_key_region_free(struct ilka_region *region, ilka_off_t off)
{
    const uint32_t *len = ilka_read(region, off, sizeof(*len));
    ilka_free(region, off, *len + sizeof(*len));
}


// -----------------------------------------------------------------------------
// it
// -----------------------------------------------------------------------------

static struct ilka_key_it err_it = {0};

bool ilka_key_err(struct ilka_key_it it)
{
    return !it.key;
}

struct ilka_key_it ilka_key_at(struct ilka_key *key, size_t byte)
{
    return (struct ilka_key_it) { .key = key, .bit = byte * 8 };
}

struct ilka_key_it ilka_key_at_bit(struct ilka_key *key, size_t bit)
{
    return (struct ilka_key_it) { .key = key, .bit = bit };
}

bool ilka_key_end(struct ilka_key_it it)
{
    return it.bit >= it.key->len * 8UL;
}

size_t ilka_key_delta_bits(struct ilka_key_it lhs, struct ilka_key_it rhs)
{
    return ilka_likely(lhs.bit < rhs.bit) ?
        rhs.bit - lhs.bit :
        lhs.bit - rhs.bit;
}

size_t ilka_key_delta(struct ilka_key_it lhs, struct ilka_key_it rhs)
{
    return ilka_key_delta_bits(lhs, rhs) / 8;
}

size_t ilka_key_remaining_bits(struct ilka_key_it it)
{
    if (ilka_key_err(it)) return 0;
    return (it.key->len * 8UL) - it.bit;
}

size_t ilka_key_remaining(struct ilka_key_it it)
{
    if (ilka_key_err(it)) return 0;
    return ilka_key_remaining_bits(it) / 8;
}


// -----------------------------------------------------------------------------
// it - write
// -----------------------------------------------------------------------------

struct ilka_key_it ilka_key_write_8(struct ilka_key_it it, uint8_t data)
{
    return ilka_key_write_bytes(it, &data, sizeof(data));
}

struct ilka_key_it ilka_key_write_16(struct ilka_key_it it, uint16_t data)
{
    data = htobe16(data);
    return ilka_key_write_bytes(it, (const uint8_t *) &data, sizeof(data));
}

struct ilka_key_it ilka_key_write_32(struct ilka_key_it it, uint32_t data)
{
    data = htobe32(data);
    return ilka_key_write_bytes(it, (const uint8_t *) &data, sizeof(data));
}

struct ilka_key_it ilka_key_write_64(struct ilka_key_it it, uint64_t data)
{
    data = htobe64(data);
    return ilka_key_write_bytes(it, (const uint8_t *) &data, sizeof(data));
}

struct ilka_key_it ilka_key_write_str(struct ilka_key_it it, const char *data, size_t len)
{
    return ilka_key_write_bytes(it, (const uint8_t *) data, len);
}

struct ilka_key_it ilka_key_write_bytes(struct ilka_key_it it, const uint8_t *data, size_t len)
{
    if (ilka_key_err(it)) return it;
    ilka_assert(it.bit % 8 == 0, "invalid iterator pos: %lu", it.bit);

    if (!ilka_key_reserve(it.key, len)) return err_it;

    memcpy(write_data(it.key) + (it.bit / 8), data, len);

    it.bit += len * 8;
    if (it.key->len < it.bit / 8)
        it.key->len = it.bit / 8;

    return it;
}


// -----------------------------------------------------------------------------
// it - read
// -----------------------------------------------------------------------------

struct ilka_key_it ilka_key_read_8(struct ilka_key_it it, uint8_t *data)
{
    struct ilka_key_it ret = ilka_key_read_bytes(it, data, sizeof(*data));
    if (ilka_key_delta(it, ret) != sizeof(*data)) return err_it;
    return ret;
}

struct ilka_key_it ilka_key_read_16(struct ilka_key_it it, uint16_t *data)
{
    struct ilka_key_it ret = ilka_key_read_bytes(it, (uint8_t *) data, sizeof(*data));
    if (ilka_key_delta(it, ret) != sizeof(*data)) return err_it;

    *data = be16toh(*data);
    return ret;
}

struct ilka_key_it ilka_key_read_32(struct ilka_key_it it, uint32_t *data)
{
    struct ilka_key_it ret = ilka_key_read_bytes(it, (uint8_t *) data, sizeof(*data));
    if (ilka_key_delta(it, ret) != sizeof(*data)) return err_it;

    *data = be32toh(*data);
    return ret;
}

struct ilka_key_it ilka_key_read_64(struct ilka_key_it it, uint64_t *data)
{
    struct ilka_key_it ret = ilka_key_read_bytes(it, (uint8_t *) data, sizeof(*data));
    if (ilka_key_delta(it, ret) != sizeof(*data)) return err_it;

    *data = be64toh(*data);
    return ret;
}

struct ilka_key_it ilka_key_read_str(struct ilka_key_it it, char *data, size_t len)
{
    return ilka_key_read_bytes(it, (uint8_t *) data, len);
}

struct ilka_key_it ilka_key_read_bytes(struct ilka_key_it it, uint8_t *data, size_t len)
{
    ilka_assert(it.bit % 8 == 0, "invalid iterator pos: %lu", it.bit);

    size_t cap = ilka_key_remaining(it);
    if (len > cap) len = cap;

    memcpy(data, read_data(it.key) + (it.bit / 8), len);

    it.bit += len * 8;
    return it;
}
