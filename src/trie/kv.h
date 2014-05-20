/* kv.h
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   key-value pair manipulation.


*/

#include "key.h"


// -----------------------------------------------------------------------------
// kvs
// -----------------------------------------------------------------------------

enum trie_kvs_state
{
    trie_kvs_state_empty     = 0,
    trie_kvs_state_branch    = 1,
    trie_kvs_state_terminal  = 2,
    trie_kvs_state_tombstone = 3,
};

struct trie_kv
{
    uint64_t key, val;
    enum trie_kvs_state state;
};

struct trie_kvs_encode_info
{
    uint8_t bits;
    uint8_t shift;
    uint8_t prefix_bits;
    uint8_t prefix_shift;
    uint8_t padding;
    uint64_t prefix;
};


struct trie_kvs_info
{
    uint8_t size;
    uint8_t key_len;

    uint8_t buckets;
    uint8_t is_abs_buckets;

    uint8_t value_bits;
    uint8_t value_shift;

    uint8_t value_offset;
    uint8_t state_offset;
    uint8_t bucket_offset;

    struct trie_kvs_encode_info key;
    struct trie_kvs_encode_info val;

    uint64_t value;
    uint64_t state[2];
};


int trie_kvs_info(
        struct trie_kvs_info *info, size_t key_len,
        int has_value, uint64_t value,
        const struct trie_kv *kvs, size_t kvs_n);

void trie_kvs_decode(struct trie_kvs_info *info);

void trie_kvs_encode(
        const struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t kvs_n,
        void *data);

void trie_kvs_extract(
        const struct trie_kvs_info *info,
        struct trie_kv *kvs, size_t kvs_n,
        const void *data);

size_t trie_kvs_count(struct trie_kvs_info *info);

struct trie_kv trie_kvs_get(struct trie_kvs_info *info, uint64_t key, const void *data);
struct trie_kv trie_kvs_lb(struct trie_kvs_info *info, uint64_t key, const void *data);
struct trie_kv trie_kvs_ub(struct trie_kvs_info *info, uint64_t key, const void *data);

void trie_kvs_add(struct trie_kv *kvs, size_t kvs_n, struct trie_kv kv);
int trie_kvs_can_add_inplace(struct trie_kvs_info *info, struct trie_kv kv);
void trie_kvs_add_inplace(struct trie_kvs_info *info, struct trie_kv kv, void *data);

void trie_kvs_set(struct trie_kv *kvs, size_t kvs_n, struct trie_kv kv);
int trie_kvs_can_set_inplace(struct trie_kvs_info *info, struct trie_kv kv);
void trie_kvs_set(struct trie_kvs_info *info, struct trie_kv kv, void *data);

int trie_kvs_can_set_value_inplace(struct trie_kvs_info *info, uint64_t value);
void trie_kvs_set_value_inplace(struct trie_kvs_info *info, uint64_t value);

void trie_kvs_remove(struct trie_kvs_info *info, uint64_t key, void *data);


// -----------------------------------------------------------------------------
// kvs burst
// -----------------------------------------------------------------------------

enum { TRIE_KVS_MAX_BUCKETS = 256 };

struct trie_kvs_burst_suffix
{
    uint8_t size;
    struct trie_kv kvs[TRIE_KVS_MAX_BUCKETS];
};

struct trie_kvs_burst_info
{
    uint8_t prefix_bits;
    uint8_t prefixes;
    struct trie_kv prefix[TRIE_KVS_MAX_BUCKETS];

    uint8_t suffix_bits;
    struct trie_kvs_burst_suffix suffix[TRIE_KVS_MAX_BUCKETS];
};

void trie_kvs_burst(
        struct trie_kvs_burst_info *burst, size_t key_len
        const struct trie_kv *kvs, size_t kvs_n);
