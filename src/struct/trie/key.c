/* key.c
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   key implementation.
*/

#include "key.h"
#include "utils/bits.h"
#include "utils/alloc.h"
#include "utils/error.h"
#include "utils/endian.h"
#include "utils/bit_coder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// -----------------------------------------------------------------------------
// key
// -----------------------------------------------------------------------------

static void
free_chunks(struct ilka_key_chunk *chunk)
{
    while (chunk) {
        struct ilka_key_chunk *next = chunk->next;
        free(chunk);
        chunk = next;
    }
}

static void
add_chunk(struct ilka_key_chunk *chunk)
{
    if (!chunk->next) {
        chunk->next = ilka_aligned_alloc(ILKA_KEY_CHUNK_SIZE, sizeof(struct ilka_key_chunk));
        chunk->next->next = NULL;
    }

    memset(chunk->next->bytes, 0, ILKA_KEY_CHUNK_SIZE);
}

static void
reserve(struct ilka_key *key, size_t size)
{
    if (key->size >= size) return;

    size_t diff = size - key->size;
    size_t pos = size % ILKA_KEY_CHUNK_SIZE;
    size_t avail = ILKA_KEY_CHUNK_SIZE - pos;

    while (diff > avail) {
        add_chunk(key->last);
        key->last = key->last->next;

        diff -= avail;
        avail = ILKA_KEY_CHUNK_SIZE;
    }
}


void
ilka_key_init(struct ilka_key *key)
{
    memset(key, 0, sizeof(struct ilka_key));
    key->last = &key->chunk;
}

void
ilka_key_free(struct ilka_key *key)
{
    free_chunks(key->chunk.next);
    ilka_key_init(key);
}

void
ilka_key_reset(struct ilka_key *key)
{
    key->size = 0;
    key->last = &key->chunk;
    memset(&key->chunk.bytes, 0, ILKA_KEY_CHUNK_SIZE);
}

void
ilka_key_copy(struct ilka_key * restrict key, struct ilka_key * restrict other)
{
    key->size = other->size;

    struct ilka_key_chunk * restrict src = &other->chunk;
    struct ilka_key_chunk * restrict dest = &key->chunk;

    while (1) {
        key->last = dest;
        memcpy(dest->bytes, src->bytes, ILKA_KEY_CHUNK_SIZE);

        if (!src->next) break;
        add_chunk(dest);
    }

    free_chunks(dest->next);
}


// -----------------------------------------------------------------------------
// iterators
// -----------------------------------------------------------------------------

struct ilka_key_it
ilka_key_begin(struct ilka_key *key)
{
    struct ilka_key_it it = {
        .key = key,
        .chunk = &key->chunk
    };
    return it;
}

int
ilka_key_end(struct ilka_key_it it)
{
    return (it.pos % 8) == 0
        && (it.pos / 8) >= it.key->size;
}

size_t
ilka_key_leftover(struct ilka_key_it it)
{
    size_t bit_pos = it.pos % 8;
    size_t bit_leftover = bit_pos ? 8 - bit_pos : 0;
    return bit_leftover + (it.key->size - ceil_div(it.pos, 8)) * 8;
}

static size_t
chunk_pos(struct ilka_key_it it)
{
    return it.pos % (ILKA_KEY_CHUNK_SIZE * 8);
}

static size_t
chunk_avail(struct ilka_key_it it)
{
    return (ILKA_KEY_CHUNK_SIZE * 8) - chunk_pos(it);
}

static void
advance(struct ilka_key_it *it, size_t bits)
{
    if (chunk_avail(*it) < bits)
        it->chunk = it->chunk->next;
    it->pos += bits;
}


// -----------------------------------------------------------------------------
// bits
// -----------------------------------------------------------------------------

uint64_t
ilka_key_peek(struct ilka_key_it it, size_t bits)
{
    if (ilka_unlikely(bits > sizeof(uint64_t) * 8))
        ilka_error("poping <%lu> bits beyond static limits <64>", bits);

    size_t end = it.key->size * 8;
    if (ilka_unlikely(bits + it.pos > end))
        ilka_error("poping <%lu> bits beyond end of key <%lu>", bits, end);


    struct bit_decoder coder;
    bit_decoder_init(&coder, it.chunk->bytes, ILKA_KEY_CHUNK_SIZE);
    bit_decode_skip(&coder, chunk_pos(it));

    size_t avail = bit_decoder_leftover(&coder);
    uint64_t data = bit_decode(&coder, avail >= bits ? bits : avail);

    if (avail < bits) {
        bit_decoder_init(&coder, it.chunk->next->bytes, ILKA_KEY_CHUNK_SIZE);
        data |= bit_decode(&coder, bits - avail) << avail;
    }

    return data;
}


uint64_t
ilka_key_pop(struct ilka_key_it *it, size_t bits)
{
    uint64_t data = ilka_key_peek(*it, bits);
    advance(it, bits);
    return data;
}

void
ilka_key_push(struct ilka_key_it *it, uint64_t data, size_t bits)
{
    reserve(it->key, ceil_div(it->pos + bits, 8));

    struct bit_encoder coder;
    bit_encoder_init(&coder, it->chunk, ILKA_KEY_CHUNK_SIZE);
    bit_encode_skip(&coder, chunk_pos(*it));

    size_t avail = bit_encoder_leftover(&coder);
    bit_encode(&coder, data, avail >= bits ? bits : avail);

    advance(it, bits);

    if (avail < bits) {
        bit_encoder_init(&coder, it->chunk, ILKA_KEY_CHUNK_SIZE);
        bit_encode(&coder, data >> avail, bits - avail);
    }

    it->key->size = ceil_div(it->pos, 8);
}


// -----------------------------------------------------------------------------
// cmp
// -----------------------------------------------------------------------------

int
ilka_key_cmp(struct ilka_key *lhs, struct ilka_key *rhs)
{
    size_t n = lhs->size < rhs->size ? lhs->size : rhs->size;

    struct ilka_key_chunk *lhs_chunk = &lhs->chunk;
    struct ilka_key_chunk *rhs_chunk = &rhs->chunk;

    while (n > 0) {
        size_t avail = n < ILKA_KEY_CHUNK_SIZE ? n : ILKA_KEY_CHUNK_SIZE;

        int r = memcmp(lhs_chunk->bytes, rhs_chunk->bytes, avail);
        if (r) return r;

        n -= avail;
        if (avail == ILKA_KEY_CHUNK_SIZE) {
            lhs_chunk = lhs_chunk->next;
            rhs_chunk = rhs_chunk->next;
        }
    }

    return lhs->size - rhs->size;
}


// -----------------------------------------------------------------------------
// write
// -----------------------------------------------------------------------------

void
ilka_key_write_8(struct ilka_key_it *it, uint8_t data)
{
    ilka_key_write_bytes(it, &data, sizeof(data));
}

void
ilka_key_write_16(struct ilka_key_it *it, uint16_t data)
{
    data = htobe16(data);
    ilka_key_write_bytes(it, (uint8_t*) &data, sizeof(data));
}

void
ilka_key_write_32(struct ilka_key_it *it, uint32_t data)
{
    data = htobe32(data);
    ilka_key_write_bytes(it, (uint8_t*) &data, sizeof(data));
}

void
ilka_key_write_64(struct ilka_key_it *it, uint64_t data)
{
    data = htobe64(data);
    ilka_key_write_bytes(it, (uint8_t*) &data, sizeof(data));
}

void
ilka_key_write_str(struct ilka_key_it *it, const char *data, size_t data_n)
{
    ilka_key_write_bytes(it, (const uint8_t *) data, data_n);
}

void
ilka_key_write_bytes(struct ilka_key_it *it, const uint8_t *data, size_t data_n)
{
    if (ilka_unlikely(it->pos % 8))
        ilka_error("writting to misaligned key iterator <%lu>", it->pos);

    reserve(it->key, it->pos / 8 + data_n);

    size_t avail = chunk_avail(*it) / 8;
    it->pos += data_n * 8;
    it->key->size = it->pos / 8;

    while (data_n > 0) {
        size_t to_copy = avail < data_n ? avail : data_n;
        uint8_t *dest = it->chunk->bytes + (ILKA_KEY_CHUNK_SIZE - avail);

        memcpy(dest, data, to_copy);

        data += to_copy;
        data_n -= to_copy;
        if (to_copy == avail) it->chunk = it->chunk->next;
        avail = ILKA_KEY_CHUNK_SIZE;
    }
}


// -----------------------------------------------------------------------------
// read
// -----------------------------------------------------------------------------


uint8_t
ilka_key_read_8(struct ilka_key_it *it)
{
    uint8_t data;
    ilka_key_read_bytes(it, &data, sizeof(data));
    return data;
}

uint16_t
ilka_key_read_16(struct ilka_key_it *it)
{
    uint16_t data;
    ilka_key_read_bytes(it, (uint8_t*) &data, sizeof(data));
    return be16toh(data);
}

uint32_t
ilka_key_read_32(struct ilka_key_it *it)
{
    uint32_t data;
    ilka_key_read_bytes(it, (uint8_t*) &data, sizeof(data));
    return be32toh(data);
}

uint64_t
ilka_key_read_64(struct ilka_key_it *it)
{
    uint64_t data;
    ilka_key_read_bytes(it, (uint8_t*) &data, sizeof(data));
    return be64toh(data);
}

void
ilka_key_read_str(struct ilka_key_it *it, char *data, size_t data_n)
{
    ilka_key_read_bytes(it, (uint8_t *) data, data_n);
}

void
ilka_key_read_bytes(struct ilka_key_it *it, uint8_t *data, size_t data_n)
{
    if (ilka_unlikely(it->pos % 8))
        ilka_error("reading misaligned key iterator <%lu>", it->pos);

    if (ilka_unlikely(data_n + it->pos / 8 > it->key->size)) {
        ilka_error("reading <%lu> bytes beyond end of key <%lu>",
                data_n, it->key->size);
    }

    size_t avail = chunk_avail(*it) / 8;
    it->pos += data_n * 8;

    while (data_n > 0) {
        size_t to_copy = avail < data_n ? avail : data_n;
        uint8_t *src = it->chunk->bytes + (ILKA_KEY_CHUNK_SIZE - avail);

        memcpy(data, src, to_copy);

        data += to_copy;
        data_n -= to_copy;
        if (to_copy == avail) it->chunk = it->chunk->next;
        avail = ILKA_KEY_CHUNK_SIZE;
    }
}


// -----------------------------------------------------------------------------
// debug
// -----------------------------------------------------------------------------

void
ilka_key_print_it(struct ilka_key_it it)
{
    printf("key_it={ pos=%zu/%zu, sz=%zu/%zu, chunk=%p, first=%p, last=%p }\n",
            ceil_div(it.pos, 8), it.pos, it.key->size, it.key->size * 8,
            (void *) it.chunk, (void *) &it.key->chunk, (void *) it.key->last);
}

void
ilka_key_print_chunk(struct ilka_key_chunk *chunk)
{
    uint64_t *ptr = (uint64_t *) chunk->bytes;
    printf("chunk={ next=%p, [ 0:%p, 1:%p, 2:%p, 3:%p, 4:%p, 5:%p, 6:%p ] }\n",
            (void *) chunk->next,
            (void *) ptr[0], (void *) ptr[1], (void *) ptr[2], (void *) ptr[3],
            (void *) ptr[4], (void *) ptr[5], (void *) ptr[6]);
}

