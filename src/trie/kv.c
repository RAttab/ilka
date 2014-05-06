/* kv.c
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   kv implementation.
*/

#include "kv.h"
#include "utils/bits.h"

// -----------------------------------------------------------------------------
// kvs
// -----------------------------------------------------------------------------

enum { MAX_BITS = sizeof(uint64_t) * 8 };

static size_t prefix_bits(const struct trie_kv *kvs, size_t n)
{
    uint64_t same = -1ULL;

    for (size_t i = 1; i < n; ++i)
        same &= ~(kvs[i-1].key ^ kvs[i].key);

    return clz(~same);
}

static uint64_t prefix_mask(size_t bits)
{
    return ~((1ULL << (MAX_BITS - bits)) - 1);
}

static void calc_bits(
        struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t n)
{
    uint64_t suffix_mask = ~prefix_mask(info->prefix_bits);

    info->key_bits = MAX_BITS;
    info->val_bits = MAX_BITS;

    for (size_t i = 0; i < n; ++i) {
        uin8_t key_bits = MAX_BITS - clz(kvs[i].key & suffix_mask);
        if (key_bits < info->key_bits) info->key_bits = key_bits;

        uin8_t val_bits = MAX_BITS - clz(kvs[i].val);
        if (val_bits < info->val_bits) info->val_bits = val_bits;
    }

    info->key_bits = ceil_pow2(info->val_bits);
    info->val_bits = ceil_pow2(info->val_bits);
}

static void calc_prefix_bits(struct trie_kvs_info *info, uint64_t key)
{
    if (!info->prefix_bits || info->key_bits == info->key_len) {
        info->prefix_bits = 0;
        info->prefix = 0;
        return;
    }

    info->prefix_bits = ceil_div(info->prefix_bits, 8) * 8;
    info->prefix = key & prefix_mask(info->prefix_bits);

    // zero-out any overlapping bits in the prefix.
    size_t total_bits = key_bits + info->prefix_bits;
    if (total_bits > info->key_len) {
        size_t overlap = total_bits - info->key_len;
        uint64_t mask = (1ULL << (info->key_bits + overlap)) - 1;
        info->prefix &= ~mask;
    }
}

static size_t bucket_start_byte(struct trie_kvs_info *info)
{

}

void trie_kvs_info(
        struct trie_kvs_info *info, size_t key_len,
        const struct trie_kv *kvs, size_t n)
{
    if (!n) ilka_error("empty kvs");
    if (!is_pow2(key_len))
        ilka_error("key length <%zu> must be a power of 2", key_len);

    memset(info, 0, sizeof(struct trie_kvs_info));
    info->key_len = key_len;

    info->prefix_bits = prefix_bits(kvs, n);
    calc_bits(info, kvs, n);
    calc_prefix_bits(info, kvs[0].key);
}

size_t trie_kvs_size(const struct trie_kvs_info *info)
{

}

void trie_kvs_decode(
        struct trie_kvs_info *info,
        const void *data, size_t n)
{

}

void trie_kvs_encode(
        const struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t kvs_size,
        void *dest, size_t dest_size)
{

}

void trie_kvs_extract(
        const struct trie_kvs_info *info,
        struct trie_kv *kvs, size_t n)
{

}

void trie_kvs_add(struct trie_kv *kvs, size_t n, struct trie_kv kv)
{

}

voir trie_kvs_set(struct trie_kvs_info *info, struct trie_kv kv)
{

}

struct trie_kv trie_kvs_get(struct trie_kvs_info *info, struct trie_key_it *it)
{

}


struct trie_kv trie_kvs_lb(struct trie_kvs_info *info, struct trie_key_it *it)
{

}

struct trie_kv trie_kvs_ub(struct trie_kvs_info *info, struct trie_key_it *it)
{

}

int trie_kvs_add_inplace(struct trie_kvs_info *info, struct trie_kv kv)
{

}

voir trie_kvs_rmv(struct trie_kvs_info *info, uint64_t key)
{

}


void trie_kvs_burst(
        struct trie_kvs_burst_info *burst,
        const struct trie_kv *kvs, size_t n)
{

}
