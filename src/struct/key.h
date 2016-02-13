/* key.h
   Rémi Attab (remi.attab@gmail.com), 06 Feb 2016
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

#include "ilka.h"


// -----------------------------------------------------------------------------
// key
// -----------------------------------------------------------------------------

struct ilka_key
{
    uint32_t len;
    uint32_t cap;

    union {
        uint8_t *out;
        uint8_t in[sizeof(uint8_t *)];
    } data;
};

bool ilka_key_init(struct ilka_key *);
void ilka_key_free(struct ilka_key *);

void ilka_key_clear(struct ilka_key *);
bool ilka_key_reserve(struct ilka_key *, size_t cap);

int ilka_key_cmp(struct ilka_key *lhs, struct ilka_key *rhs);
bool ilka_key_copy(struct ilka_key *src, struct ilka_key *dest);

const uint8_t *ilka_key_data(struct ilka_key *);


// -----------------------------------------------------------------------------
// region
// -----------------------------------------------------------------------------

ilka_off_t ilka_key_region_save(struct ilka_region *, struct ilka_key *);
void ilka_key_region_free(struct ilka_region *, ilka_off_t off);
struct ilka_key_it ilka_key_region_load(
        struct ilka_region *, ilka_off_t off, struct ilka_key_it it);


// -----------------------------------------------------------------------------
// it
// -----------------------------------------------------------------------------

struct ilka_key_it
{
    struct ilka_key *key;
    size_t bit;
};

struct ilka_key_it ilka_key_at(struct ilka_key *, size_t byte);
struct ilka_key_it ilka_key_at_bit(struct ilka_key *, size_t bit);
bool ilka_key_err(struct ilka_key_it it);
bool ilka_key_end(struct ilka_key_it it);

size_t ilka_key_delta(struct ilka_key_it lhs, struct ilka_key_it rhs);
size_t ilka_key_delta_bits(struct ilka_key_it lhs, struct ilka_key_it rhs);
size_t ilka_key_remaining(struct ilka_key_it it);
size_t ilka_key_remaining_bits(struct ilka_key_it it);

struct ilka_key_it ilka_key_write_8(struct ilka_key_it it, uint8_t data);
struct ilka_key_it ilka_key_write_16(struct ilka_key_it it, uint16_t data);
struct ilka_key_it ilka_key_write_32(struct ilka_key_it it, uint32_t data);
struct ilka_key_it ilka_key_write_64(struct ilka_key_it it, uint64_t data);
struct ilka_key_it ilka_key_write_str(struct ilka_key_it it, const char *data, size_t len);
struct ilka_key_it ilka_key_write_bytes(struct ilka_key_it it, const uint8_t *data, size_t len);

struct ilka_key_it ilka_key_read_8(struct ilka_key_it it, uint8_t *data);
struct ilka_key_it ilka_key_read_16(struct ilka_key_it it, uint16_t *data);
struct ilka_key_it ilka_key_read_32(struct ilka_key_it it, uint32_t *data);
struct ilka_key_it ilka_key_read_64(struct ilka_key_it it, uint64_t *data);
struct ilka_key_it ilka_key_read_str(struct ilka_key_it it, char *data, size_t len);
struct ilka_key_it ilka_key_read_bytes(struct ilka_key_it it, uint8_t *data, size_t len);
