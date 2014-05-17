/* key.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   key
*/

#pragma once

#include <stdint.h>


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
    union {
        uint8_t bytes[ILKA_KEY_CHUNK_SIZE];
        uint64_t words[ILKA_KEY_CHUNK_SIZE / sizeof(uint64_t)];
    };
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
void ilka_key_copy(restrict struct ilka_key *key, restrict struct ilka_key *other);

int ilka_key_end(struct ilka_key_it it);
struct ilka_key_it ilka_key_begin(struct ilka_key *key);

uint64_t ilka_key_pop(struct ilka_key_it *it, size_t bits);
uint64_t ilka_key_peek(struct ilka_key_it it, size_t bits);
void ilka_key_push(struct ilka_key_it *it, uint64_t data, size_t bits);
int ilka_key_consume(struct ilka_key_it *it, uint64_t data, size_t bits);

void ilka_key_append_16(struct ilka_key *key, uint16_t data);
void ilka_key_append_32(struct ilka_key *key, uint32_t data);
void ilka_key_append_64(struct ilka_key *key, uint64_t data);
void ilka_key_append_bytes(struct ilka_key *key, restrict const uint8_t *data, size_t n);

struct ilka_key_it ilka_key_extract_16(struct ilka_key_it it, uint16_t *data);
struct ilka_key_it ilka_key_extract_32(struct ilka_key_it it, uint32_t *data);
struct ilka_key_it ilka_key_extract_64(struct ilka_key_it it, uint64_t *data);
struct ilka_key_it ilka_key_extract_bytes(struct ilka_key_it it, restrict uint8_t *data, size_t n);