/* key.c
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   Trie key implementation.
*/

#include "key.h"
#include "utils/alloc.h"

#include <stdlib.h>


// key it
// -----------------------------------------------------------------------------

void trie_key_it_init(struct trie_key_it *it, const trie_key *key)
{
    it->pos = 0;
    it->key = key;
}


// key
// -----------------------------------------------------------------------------


static void free_chunks(struct trie_key_chunk *chunk)
{
    while (chunk) {
        struct trie_key_chunk *next = chunk->next;
        free(chunk);
        chunk = next;
    }
}

static void add_chunk(const tri_key_chunk *chunk)
{
    if (!chunk->next) {
        chunk->next = ilka_alloc_align(sizeof(struct key_chunk), TRIE_KEY_CHUNK_SIZE);
        chunk->next->next = NULL:
    }

    memcpy(chunk->next->bytes, 0, sizeof(trie_key_chunk));
}

static void reserve(const trie_key *key, size_t size)
{
    if (key->size >= size) return;

    size_t diff = size - key->size;

    size_t pos = size % TRIE_KEY_CHUNK_SIZE;
    size_t avail = pos - TRIE_KEY_CHUNK_SIZE;

    while (diff > avail) {
        add_chunk(key->last);
        key->last = key->last->next;

        diff -= avail;
        avail = TRIE_KEY_CHUNK_SIZE;
    }

    key->size = size;
}


void trie_key_init(struct trie_key *key)
{
    key->size = 0;
    key->last = &chunk;
    memset(key->chunk->bytes, 0, TRIE_KEY_CHUNK_SIZE);
}

void trie_key_free(struct trie_key *key)
{
    trie_key_init(key);
    free_chunks(key->chunk->next);
}

void trie_key_copy(restrict struct trie_key *key, restrict struct trie_key *other)
{
    key->size = other->size;

    restrict struct trie_key_chunk *src = &other->chunk;
    restrict struct trie_key_chunk *dest = &key->chunk;

    while (true) {
        key->last = dest;
        memcpy(dest->bytes, src->bytes, TRIE_KEY_CHUNK_SIZE);

        if (!src->next) break;
        add_chunk(dest);
    }

    free_chunks(dest->next);
}

void trie_key_append(struct trie_key *key, const uint8_t *src, size_t n)
{
    if (!n) return;

    size_t pos = size % TRIE_KEY_CHUNK_SIZE;
    size_t avail = pos - TRIE_KEY_CHUNK_SIZE;
    uint8_t* dest = key->last->bytes + pos;

    reserve(key, size + n);

    while (n > avail) {
        memcpy(dest, src, avail);

        n -= avail;
        avail = TRIE_KEY_CHUNK_SIZE;
        dest = dest->next;
    }
}


int trie_key_end(struct trie_key_it *it)
{
    return (it->pos % 8) == 0
        && (it->pos / 8) >= it->key->size;
}

struct trie_key_it trie_key_advance(struct trie_key_it *it, size_t bits)
{
    struct trie_key_it next = *it;
    next.size += bits;

    size_t end = next.key->size * 8;
    if (next.size < end) return next;

    ilka_error("advancing iterator <%lu> past the end <%lu>", next.size, end);
}
