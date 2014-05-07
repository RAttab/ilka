/* kv.h
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   key-value pair manipulation.


*/

#include "key.h"


// -----------------------------------------------------------------------------
// kvs
// -----------------------------------------------------------------------------

struct trie_kv
{
    uint64_t key, val;
    int terminal, tombstone;
};

struct trie_kvs_info
{
    uint8_t key_len;
    uint8_t key_bits;
    uint8_t key_prefix_bits;

    uint8_t val_bits;
    uint8_t val_shift;
    uint8_t val_prefix_bits;

    uint8_t buckets;

    uint64_t key_prefix;
    uint64_t val_prefix;

    uint32_t present;
    uint32_t terminal;
    uint32_t tombstone;

    void *data;
    size_t data_size;
};


void trie_kvs_info(
        struct trie_kvs_info *info, size_t key_len,
        const struct trie_kv *kvs, size_t n);

size_t trie_kvs_size(const struct trie_kvs_info *info);

void trie_kvs_decode(
        struct trie_kvs_info *info,
        const void *data, size_t n);

void trie_kvs_encode(
        const struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t kvs_size,
        void *dest, size_t dest_size);

void trie_kvs_extract(
        const struct trie_kvs_info *info,
        struct trie_kv *kvs, size_t n);

void trie_kvs_add(struct trie_kv *kvs, size_t n, struct trie_kv kv);

int trie_kvs_add_inplace(struct trie_kvs_info *info, struct trie_kv kv);
voir trie_kvs_rmv(struct trie_kvs_info *info, uint64_t key);
voir trie_kvs_set(struct trie_kvs_info *info, struct trie_kv kv);

struct trie_kv trie_kvs_get(struct trie_kvs_info *info, struct trie_key_it *it);
struct trie_kv trie_kvs_lb(struct trie_kvs_info *info, struct trie_key_it *it);
struct trie_kv trie_kvs_ub(struct trie_kvs_info *info, struct trie_key_it *it);


// -----------------------------------------------------------------------------
// kvs burst
// -----------------------------------------------------------------------------

enum
{
    TRIE_KVS_MAX_BUCKETS = 32,
    TRIE_KVS_MAX_BURST_KV = TRIE_KVS_MAX_BUCKETS / 2;
};


struct trie_kvs_burst_suffix
{
    uint8_t size;
    struct trie_kv kvs[TRIE_KVS_MAX_BURST_KV];
};

struct trie_kvs_burst_info
{
    uint8_t size;

    uint8_t prefix_bits;
    struct trie_kv prefix[TRIE_KVS_MAX_BURST_KV];

    uint8_t suffix_bits;
    struct trie_kvs_burst_suffix suffix[TRIE_KVS_MAX_BURST_KV];
};

void trie_kvs_burst(
        struct trie_kvs_burst_info *burst,
        const struct trie_kv *kvs, size_t n);



