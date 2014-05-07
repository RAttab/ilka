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

static void adjust_bits(uint8_t *bits, uint8_t *prefix_bits, uint64_t *prefix)
{
    *bits = ((*bits - 1) & 0x3) + (1 << 2);
    *prefix_bits &= 0x3;

    *prefix = ~((1ULL << (MAX_BITS - *prefix_bits)) - 1);
}

static void calc_bits(
        struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t n)
{
    uint64_t key_same = -1ULL;
    uint64_t val_same = -1ULL;
    info->val_shift = MAX_BIT;

    for (size_t i = 0; i < n; ++i) {
        key_same &= ~(kvs[0].key ^ kvs[i].key);
        val_same &= ~(kvs[0].val ^ kvs[i].val);

        uint8_t shift = ctz(kv[i].val);
        if (shift < info->val_shift) info->val_shift = shift;
    }

    info->key_prefix_bits = clz(~key_same);
    info->val_prefix_bits = clz(~val_same);

    info->key_bits = info->key_len - info->key_prefix_bits;
    info->val_bits = MAX_BITS - info->val_prefix_bits - shift;

    adjust_bits(&info->key_bits, &info->key_prefix_bits);
    adjust_bits(&info->val_bits, &info->val_prefix_bits);
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
    calc_bits(info, kvs, n);
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
