/* key.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   key
*/

#pragma once

#include <stdint.h>
#include <stddef.h>


// -----------------------------------------------------------------------------
// key
// -----------------------------------------------------------------------------

enum
{
    ILKA_KEY_CHUNK_WORDS = 7,
    ILKA_KEY_CHUNK_SIZE = ILKA_KEY_CHUNK_WORDS * sizeof(uint64_t)
};

struct ilka_key_chunk
{
    uint8_t bytes[ILKA_KEY_CHUNK_SIZE];
    struct ilka_key_chunk *next;
};

struct ilka_key
{
    size_t size; // bytes
    struct ilka_key_chunk *last;
    struct ilka_key_chunk chunk;
};

struct ilka_key_it
{
    size_t pos; // bits
    struct ilka_key *key;
    struct ilka_key_chunk *chunk;
};

void ilka_key_init(struct ilka_key *key);
void ilka_key_free(struct ilka_key *key);
void ilka_key_reset(struct ilka_key *key);
void ilka_key_copy(struct ilka_key * restrict key, struct ilka_key * restrict other);

int ilka_key_end(struct ilka_key_it it);
struct ilka_key_it ilka_key_begin(struct ilka_key *key);
size_t ilka_key_leftover(struct ilka_key_it it);

int ilka_key_cmp(struct ilka_key *lhs, struct ilka_key *rhs);

void ilka_key_write_8(struct ilka_key_it *it, uint8_t data);
void ilka_key_write_16(struct ilka_key_it *it, uint16_t data);
void ilka_key_write_32(struct ilka_key_it *it, uint32_t data);
void ilka_key_write_64(struct ilka_key_it *it, uint64_t data);
void ilka_key_write_str(struct ilka_key_it *it, const char *data, size_t data_n);
void ilka_key_write_bytes(struct ilka_key_it *it, const uint8_t *data, size_t data_n);

uint8_t ilka_key_read_8(struct ilka_key_it *it);
uint16_t ilka_key_read_16(struct ilka_key_it *it);
uint32_t ilka_key_read_32(struct ilka_key_it *it);
uint64_t ilka_key_read_64(struct ilka_key_it *it);
void ilka_key_read_str(struct ilka_key_it *it, char *data, size_t data_n);
void ilka_key_read_bytes(struct ilka_key_it *it, uint8_t *data, size_t data_n);


// -----------------------------------------------------------------------------
// private interface
// -----------------------------------------------------------------------------

uint64_t ilka_key_peek(struct ilka_key_it it, size_t bits);
uint64_t ilka_key_pop(struct ilka_key_it *it, size_t bits);
void ilka_key_push(struct ilka_key_it *it, uint64_t data, size_t bits);

void ilka_key_print_it(struct ilka_key_it it);
void ilka_key_print_chunk(struct ilka_key_chunk *chunk);
