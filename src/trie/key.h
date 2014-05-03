/* key.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   Trie key.
*/

#pragma once

#include <stdint.h>


// key it
// -----------------------------------------------------------------------------

struct trie_key;

struct trie_key_it
{
    size_t pos; // bit-level granularity;
    struct trie_key *key;
};

void trie_key_it_init(struct trie_key_it *it, const trie_key *key);

// key
// -----------------------------------------------------------------------------

enum { TRIE_KEY_CHUNK_SIZE = 8 * 8 }; // should be about a cache-line.

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
    size_t size; // byte-level granularity.
    struct trie_key_chunk *last;
    struct trie_key_chunk chunk;
};

void trie_key_init(struct trie_key *key);
void trie_key_free(struct trie_key *key);
void trie_key_copy(restrict struct trie_key *key, restrict struct trie_key *other);

void trie_key_append(struct trie_key *key, const uint8_t *data, size_t n);

int trie_key_end(struct trie_key_it *it);
struct trie_key_it trie_key_advance(struct trie_key_it *it, size_t bits);
