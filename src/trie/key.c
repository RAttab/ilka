/* key.c
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   Trie key implementation.
*/

#include "key.h"
#include "utils/misc.h"
#include "utils/alloc.h"

#include <stdlib.h>
#include <endian.h> // linux specific


// -----------------------------------------------------------------------------
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
        chunk->next = ilka_malloc_align(sizeof(struct trie_key_chunk), TRIE_KEY_CHUNK_SIZE);
        chunk->next->next = NULL:
    }

    memset(chunk->next->bytes, 0, sizeof(struct trie_key_chunk));
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
    memset(key, 0, sizeof(struct trie_key));
    key->last = &chunk;
}

void trie_key_free(struct trie_key *key)
{
    free_chunks(key->chunk->next);
    trie_key_init(key);
}

void trie_key_reset(struct trie_key *key)
{
    key->size = 0;
    key->last = &chunk;
    memset(key->chunk, 0, TRIE_KEY_CHUNK_SIZE);
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


// -----------------------------------------------------------------------------
// iterators
// -----------------------------------------------------------------------------

static void advance(struct trie_key_it *it, size_t bits)
{
    size_t old = (it->pos / 8) / TRIE_KEY_CHUNK_SIZE;
    it->pos += bits;
    size_t cur = (it->pos / 8) / TRIE_KEY_CHUNK_SIZE;

    if (old == cur) return;

    it->chunk = it->chunk->next;
    if (it->chunk) return;

    ilka_error("moved into an empty chunk at pos <%lu> of <%lu>",
            it->pos / 8, it->key->pos);
}


struct trie_key_it trie_key_begin(struct trie_key *key)
{
    return { .key = key, .chunk = &key->chunk };
}

int trie_key_end(struct trie_key_it it)
{
    return (it.pos % 8) == 0
        && (it.pos / 8) >= it.key->size;
}


uint64_t trie_key_peek(struct trie_key_it it, size_t bits)
{
    if (bits > sizeof(uint64_t))
        ilka_error("poping <%lu> bits beyond static limits <64>", bits);

    size_t end = it.key->size * 8;
    if (bits + it.pos > end)
        ilka_error("poping <%lu> bits beyond end of key <%lu>", bits, end);


    size_t i = it.pos % 64;
    size_t n = 64 - i;

    size_t word = (it.pos / 64) % TRIE_KEY_CHUNK_WORDS;
    uint64_t data = it.chunk->words[word] >> i;

    if (bits > n) {
        struct trie_key_chunk *chunk = it->chunk;

        word++;
        if (word == TRIE_KEY_CHUNK_WORDS) {
            chunk = chunk->next;
            word = 0;
        }

        data |= chunk->words[word] << n;
    }

    data &= (1 << bits) - 1;
}


uint64_t trie_key_pop(struct trie_key_it *it, size_t bits)
{
    uint64_t data = trie_key_peek(*it, bits);
    advance(it, bits);
    return data;
}

void trie_key_push(struct trie_key_it *it, uint64_t data, size_t bits)
{
    reserve(it->key, ilka_ceil_div_s(it->pos + bits));

    data &= (1 << bits) - 1;

    size_t i = it->pos % 64;
    size_t n = 64 - i;

    size_t word = (it->pos / 64) % TRIE_KEY_CHUNK_WORDS;
    it->chunk[word] |= data << i;

    // will move the chunk forward if needed,
    advance(it, bits);

    if (bits > n) {
        word = (word + 1) % TRIE_KEY_CHUNK_WORDS;
        it->chunk[word] = data >> n;
    }

    it->key->size = ilka_ceil_div_s(it->pos, 8);
}

int trie_key_consume(struct trie_key_it *it, uint64_t data, size_t bits)
{
    uint64_t head = trie_key_peek(*it, bits);

    data &= (1 << bits) - 1;
    if (head != data) return 0;

    advance(it, bits);
    return 1;
}



// -----------------------------------------------------------------------------
// append
// -----------------------------------------------------------------------------


void trie_key_append_bytes(
        struct trie_key *key, restrict const uint8_t *src, size_t n)
{
    if (!n) return;

    size_t pos = key->size % TRIE_KEY_CHUNK_SIZE;
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

void trie_key_append_16(struct trie_key *key, uint16_t data)
{
    data = htobe16(data);
    trie_key_append_bytes(key, &data);
}

void trie_key_append_32(struct trie_key *key, uint32_t data)
{
    data = htobe32(data);
    trie_key_append_bytes(key, &data);
}

void trie_key_append_word(struct trie_key *key, uint64_t data)
{
    data = htobe64(data);
    trie_key_append_bytes(key, &data);
}


// -----------------------------------------------------------------------------
// extract
// -----------------------------------------------------------------------------

struct trie_key_it trie_key_extract_bytes(
        struct trie_key_it it, restrict uint8_t *data, size_t n)
{
    if (it.pos % 8)
        ilka_error("extracting from misaligned key iterator <%lu>", it.pos);

    if (it.pos / 8 + n > it.key->size) {
        ilka_error("extracting bytes past the end of key <%lu + %lu > %lu>",
                it.pos / 8, n, it.key->size);
    }

    if (!n) return;

    size_t pos = (it.pos / 8) % TRIE_KEY_CHUNK_SIZE;
    size_t avail = TRIE_KEY_CHUNK_SIZE - pos;

    while (n) {
        size_t size = n > avail ? avail : n;
        memcpy(data, it.chunk->bytes, size);

        n -= size;
        data += size;
        avail = TRIE_KEY_CHUNK_SIZE;
        if (size == avail) it.chunk = it.chunk->next;
    }

    it.pos += n * 8;
    return it;
}

struct trie_key_it trie_key_extract_16(struct trie_key_it it, uint16_t *data)
{
    it = trie_key_extract_bytes(it, data, sizeof(uint16_t));
    *data = betoh16(*data);
    return it;
}

struct trie_key_it trie_key_extract_32(struct trie_key_it it, uint32_t *data)
{
    it = trie_key_extract_bytes(it, data, sizeof(uint32_t));
    *data = betoh32(*data);
    return it;
}

struct trie_key_it trie_key_extract_64(struct trie_key_it it, uint64_t *data)
{
    it = trie_key_extract_bytes(it, data, sizeof(uint32_t));
    *data = betoh32(*data);
    return it;
}
