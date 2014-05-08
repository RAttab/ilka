/* kv.c
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   kv implementation.


Static layout:

   byte  bit  len  field                   f(x)

      0    0    4  lock
           5    4  key_len                 x << 2
      1    0    4  key_prefix_bits         x << 2
           5    4  key_shift               x << 2
      2    0    4  key_prefix_encode_bits  x << 3
           5    4  key_prefix_shift        x << 2
      3    0    4  val_prefix_bits         x << 2
           5    4  val_shift               x << 2
      4    0    4  val_prefix_encode_bits  x << 3
           5    4  val_prefix_shift        x << 2
      5    0    8  buckets

      key_bits = key_len - key_prefix_bits
      val_bits = MAX_BITS - val_prefix_bits - val_shift


Dynamic layout:

   field        range     len

   key_prefix   [ 0,  7]  key_prefix_encode_bits - key_prefix_shift
   val_prefix   [ 0,  7]  val_prefix_encode_bits - val_prefix_shift
   state        [ 1, 16]  buckets * 2
   key_buckets            key_bits * buckets
   val_buckets            val_bits * buckets


Notes:

   - val_bits can be 0. If key_bits is 8 then this could allow us to have 256
     buckets. This is a bit extreme and is unlikely to really happen so we cap
     the number of buckets at 64.

   - since there are currently 3 states that are mutally exclusive so we encode
     them with 2 bits for each buckets.

   - prefixes are omitted if their corresponding prefix_bits are 0. Note that
     neither prefix_len nor prefix_shift will be omitted. They will simply be 0.

   - prefix_encode_bits and prefix_shift can be derived from the prefix itself
     and is therefor not stored in trie_kvs_info.

*/

#include "kv.h"
#include "utils/arch.h"
#include "utils/bits.h"

// -----------------------------------------------------------------------------
// kvs
// -----------------------------------------------------------------------------

enum
{
    MAX_BITS = sizeof(uint64_t) * 8,

    STATIC_HEADER_SIZE = 6
};


// -----------------------------------------------------------------------------
// calc kvs
// -----------------------------------------------------------------------------

static void adjust_bits(struct trie_kvs_encode_info *encode, size_t max_bits)
{
    encode->shift &= 0x3;

    encode->bits = max_bits - encode->prefix_bits - info->key.shift;
    encode->bits = ((encode->bits - 1) & 0x3) + (1 << 2);

    encode->prefix_bits &= 0x3;
    encode->prefix = ~((1ULL << (MAX_BITS - encode->prefix_bits)) - 1);

    encode->prefix_shift = ctz(prefix) & 0x3;
    encode->prefix_encode_bits =
        ceil_div(MAX_BITS - clz(encode->prefix) - encode->prefix_shift, 8) * 8;
}

static void calc_bits(
        struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t n)
{
    uint64_t key_same = -1ULL;
    uint64_t val_same = -1ULL;

    info->val.shift = MAX_BIT;
    info->key.shift = MAX_BIT;

    for (size_t i = 0; i < n; ++i) {
        key_same &= ~(kvs[0].key ^ kvs[i].key);
        val_same &= ~(kvs[0].val ^ kvs[i].val);

        uint8_t key_shift = ctz(kv[i].key);
        uint8_t val_shift = ctz(kv[i].val);

        if (key_shift < info->key.shift) info->key.shift = key_shift;
        if (val_shift < info->val.shift) info->val.shift = val_shift;
    }

    info->key.prefix_bits = clz(~key_same);
    info->val.prefix_bits = clz(~val_same);

    adjust_bits(&info->key, info->key_len);;
    adjust_bits(&info->val, MAX_bitS);
}

static void calc_buckets(struct trie_kvs_info *info)
{
    size_t leftover = ILKA_CACHE_LINE - STATIC_HEADER_SIZE;
    if (info->key.prefix_bits) leftover -= prefix_bits(info->key.prefix) >> 3;
    if (info->val.prefix_bits) leftover -= prefix_bits(info->val.prefix) >> 3;

    size_t bucket_size = info->key.bits + info->val.bits;

    info->buckets = leftover / bucket_size;
    if (info->buckets > MAX_BITS) info->buckets = MAX_BITS;

    while (info->buckets * 2 + info->buckets * bucket_size > leftover)
        info->buckets--;
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
    calc_buckets(info);
}


// -----------------------------------------------------------------------------
// decode
// -----------------------------------------------------------------------------

static void decode_info(
        struct bit_decoder *coder,
        struct trie_kvs_encode_info *encode,
        size_t max_bits)
{
    encode->prefix_bits = bit_decode(coder, 4) << 2;
    encode->shift = bit_decode(coder, 4) << 2;

    encode->prefix_encode_bits = bit_decode(&coder, 4) << 3;
    encode->prefix_shift = bit_decode(&coder, 4) << 2;

    encode->bits = max_bits - encode->prefix_bits;
}

static void decode_prefix(
        struct bit_decoder *coder,
        struct trie_kvs_encode_info *encode)
{
    if (!encode->prefix_bits) return;

    encode->prefix = bit_decode(encode->prefix_encode_bits);
    encode->prefix <<= encode->prefix_shift;
}

static void decode_state(
        struct bit_decoder *coder,
        struct trie_kvs_info *info)
{
    size_t bits = ceil_div(info->buckets, 4);

    uint64_t s0 = bit_decode(coder, bits);
    uint64_t s1 = bit_decode(coder, bits);

    info->branches  =  s0 & ~s1;
    info->terminal  = ~s0 &  s1;
    info->tombstone =  s0 &  s1;
}

static struct trie_kv decode_bucket(
        struct bit_decoder *coder,
        struct trie_kvs_info *info)
{
    // \todo
}

void trie_kvs_decode(
        struct trie_kvs_info *info,
        const void *data, size_t n)
{
    struct bit_decoder coder = { .data = data, .size = n };
    memset(info, 0, sizeof(struct trie_kvs_info));

    bit_decode_skip(&coder, 4); // lock.
    info->key_len = bit_decode(&coder, 4) << 2;

    decode_info(&coder, &info->key, info->key_len);
    decode_info(&coder, &info->val, MAX_BITS);

    info->buckets = bit_decode(&coder, 8);

    decode_prefix(&coder, &info->key);
    decode_prefix(&coder, &info->val);

    decode_state(&code, info);

    // \todo decode_bucket
}


// -----------------------------------------------------------------------------
// encode
// -----------------------------------------------------------------------------

static void encode_info(
        struct bit_encoder *coder,
        struct trie_kvs_encode_info *encode,
        size_t max_bits)
{
    bit_encode(coder, encode->prefix_bits >> 2, 4);
    bit_encode(coder, encode->shift >> 2, 4);

    bit_encode(coder, encode->prefix_encode_bits >> 3, 4);
    bit_encode(coder, encode->prefix_shift >> 2, 4);
}

static void encode_prefix(
        struct bit_encoder *coder,
        struct trie_kvs_encode_info *encode)
{
    if (!encode->prefix_bits) return;

    size_t bits = encode->prefix_encode_bits;
    size_t shift = encode->prefix_shift;

    bit_encode(coder, encode->prefix >> shift, bis);
}

static void encode_state(
        struct bit_encoder *coder,
        struct trie_kvs_info *info)
{
    size_t bits = ceil_div(info->buckets, 4);

    uint64_t s0 = info->branches | info->tombstone;
    uint64_t s1 = info->terminal | info->tombstone;

    bit_encode(coder, s0, bits);
    bit_encode(coder, s1, bits);
}

static void encode_bucket(
        struct trie_encoder *coder,
        struct trie_kvs_info *info,
        struct trie_kv kv)
{
    // \todo
}

void trie_kvs_encode(
        const struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t kvs_size,
        void *data, size_t n)
{
    struct bit_encoder coder = { .data = data, .size = n };
    memset(data, 0, n);

    bit_encode_skip(&coder, 4); // lock.
    bit_encode(&coder, info->key_len >> 2, 4);

    encode_info(&coder, &info->key, info->key_len);
    encode_info(&coder, &info->val, MAX_BITS);

    info->buckets = bit_encode(&coder, 8);

    encode_prefix(&coder, &info->key);
    encode_prefix(&coder, &info->val);

    encode_state(&code, info);

    // \todo encode_bucket
}


// -----------------------------------------------------------------------------
// ops
// -----------------------------------------------------------------------------

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


// -----------------------------------------------------------------------------
// burst
// -----------------------------------------------------------------------------

void trie_kvs_burst(
        struct trie_kvs_burst_info *burst,
        const struct trie_kv *kvs, size_t n)
{

}
