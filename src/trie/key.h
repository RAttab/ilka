/* key.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   Trie key.
*/

#pragma once

#include <stdint.h>


// -----------------------------------------------------------------------------
// key
// -----------------------------------------------------------------------------

enum
{
    TRIE_KEY_CHUNK_WORDS = 7,
    TRIE_KEY_CHUNK_SIZE = TRIE_KEY_CHUNK_WORDS * sizeof(uint64_t)
};

struct trie_key_chunk
{
    union {
        uint8_t bytes[TRIE_KEY_CHUNK_SIZE];
        uint64_t words[TRIE_KEY_CHUNK_SIZE / sizeof(uint64_t)];
    };
    struct trie_key_chunk *next;
};

struct trie_key
{
    size_t size; // bytes
    struct trie_key_chunk *last;
    struct trie_key_chunk chunk;
};

struct trie_key_it
{
    size_t pos; // bits
    struct trie_key *key;
    struct trie_key_chunk *chunk;
};

void trie_key_init(struct trie_key *key);
void trie_key_free(struct trie_key *key);
void trie_key_reset(struct trie_key *key);
void trie_key_copy(restrict struct trie_key *key, restrict struct trie_key *other);

int trie_key_end(struct trie_key_it it);
struct trie_key_it trie_key_begin(struct trie_key *key);

uint64_t trie_key_pop(struct trie_key_it *it, size_t bits);
uint64_t trie_key_peek(struct trie_key_it it, size_t bits);
void trie_key_push(struct trie_key_it *it, uint64_t data, size_t bits);
int trie_key_consume(struct trie_key_it *it, uint64_t data, size_t bits);

void trie_key_append_16(struct trie_key *key, uint16_t data);
void trie_key_append_32(struct trie_key *key, uint32_t data);
void trie_key_append_64(struct trie_key *key, uint64_t data);
void trie_key_append_bytes(
        struct trie_key *key, restrict const uint8_t *data, size_t n);

struct trie_key_it trie_key_extract_16(struct trie_key_it it, uint16_t *data);
struct trie_key_it trie_key_extract_32(struct trie_key_it it, uint32_t *data);
struct trie_key_it trie_key_extract_64(struct trie_key_it it, uint64_t *data);
struct trie_key_it trie_key_extract_bytes(
        struct trie_key_it it, restrict uint8_t *data, size_t n);
