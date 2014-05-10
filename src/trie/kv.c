/* kv.c
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   kv implementation.


Static layout:

   byte  bit  len  field                   f(x)

      0    0    4  lock
           5    4  key.len                 x << 2
      1    0    4  key.bits                x << 2
           5    4  key.shift               x << 2
      2    0    4  key.prefix_bits         x << 3
           5    4  key.prefix_shift        x << 2
      3    0    4  val.bits                x << 2
           5    4  val.shift               x << 2
      4    0    4  val.prefix_bits         x << 3
           5    4  val.prefix_shift        x << 2
      5    0    8  buckets

Dynamic layout:

   field        range   len

   key.prefix   [0, 7]  key.prefix_bits - key.prefix_shift
   val.prefix   [0, 7]  val.prefix_bits - val.prefix_shift
   state        [1, 8]  buckets * 2
   buckets              (key.bits + val.bits) * buckets

Bucket layout:

   field        range     len

   key          [0, 8]    key.bits
   val          [0, 8]    val.bits

Compression Fn:

   prefix:   (value >> (bits + prefix_shift)) & ((1 << prefix_bits) - 1)
   bucket:   (value >> shift) & ((1 << bits) - 1)


Notes:

   - val_bits can be 0. If key_bits is 8 then this could allow us to have 256
     buckets. This is a bit extreme and is unlikely to really happen so we cap
     the number of buckets at 64.

   - since there are currently 3 states that are mutally exclusive so we encode
     them with 2 bits for each buckets.

   - prefixes are omitted if their corresponding prefix_bits are 0. Note that
     neither prefix_len nor prefix_shift will be omitted. They will simply be 0.

Todo:

   - make use of is_bucket_abs to avoid iterating over every buckets when
     searching for a bucket.

   - Could try to run the same compression but over ~ of the value. Choose which
     one compresses at the end. Unlikely to be worth it though.

   - Could try to detect sequences (4, 5, 6, 7...) but again, not sure it's
     worth it.
*/

#include "kv.h"
#include "utils/arch.h"
#include "utils/bits.h"
#include "utils/bit_coder.h"


// -----------------------------------------------------------------------------
// constants
// -----------------------------------------------------------------------------

enum { STATIC_HEADER_SIZE = 6 };


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static uint8_t is_bucket_abs(struct trie_kvs_info *info)
{
    return (1ULL << info->key.bits) > info->buckets;
}

static size_t key_abs_bucket(struct trie_kvs_info *info, uint64_t key)
{
    uint64_t bucket = key >> info->key.shift;
    bucket &= (1ULL << info->key.bits) - 1;
    return bucket;
}


// -----------------------------------------------------------------------------
// calc kvs
// -----------------------------------------------------------------------------

static void adjust_bits(struct trie_kvs_encode_info *encode, size_t max_bits)
{
    encode->shift &= 0x3;

    encode->bits = max_bits - encode->prefix_bits - info->key.shift;
    encode->bits = ceil_div(encode->bits, 4) * 4;

    encode->prefix_bits = max_bits - encode->bits;
    encode->prefix &= ~((1ULL << encode->bits) - 1);

    encode->prefix_shift = ctz(prefix) & 0x3;
    encode->prefix_bits =
        ceil_div(64 - clz(encode->prefix) - encode->prefix_shift, 8) * 8;
}

static void calc_bits(
        struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t kvs_n)
{
    uint64_t key_same = -1ULL;
    uint64_t val_same = -1ULL;

    info->val.shift = MAX_BIT;
    info->key.shift = MAX_BIT;

    info->key.prefix = kv[0].key;
    info->val.prefix = kv[0].val;

    for (size_t i = 0; i < kvs_n; ++i) {
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
    leftover -= info->key.prefix_bits;
    leftover -= info->val.prefix_bits;

    size_t bucket_size = info->key.bits + info->val.bits;

    info->buckets = leftover / bucket_size;
    if (info->buckets > 32) info->buckets = 32;

    while (info->buckets * 2 + info->buckets * bucket_size > leftover)
        info->buckets--;
}

void trie_kvs_info(
        struct trie_kvs_info *info, size_t key_len,
        const struct trie_kv *kvs, size_t kvs_n)
{
    if (!n) ilka_error("empty kvs");
    if (!is_pow2(key_len))
        ilka_error("key length <%zu> must be a power of 2", key_len);

    memset(info, 0, sizeof(struct trie_kvs_info));
    info->key_len = key_len;
    calc_bits(info, kvs, kvs_n);
    calc_buckets(info);

    return kvs_n <= info->buckets;
}


// -----------------------------------------------------------------------------
// decode
// -----------------------------------------------------------------------------

static void decode_info(
        struct bit_decoder *coder,
        struct trie_kvs_encode_info *encode,
        size_t max_bits)
{
    encode->bits = bit_decode(coder, 4) << 2;
    encode->shift = bit_decode(coder, 4) << 2;

    encode->prefix_bits = bit_decode(&coder, 4) << 3;
    encode->prefix_shift = bit_decode(&coder, 4) << 2;
}

static void decode_prefix(
        struct bit_decoder *coder,
        struct trie_kvs_encode_info *encode)
{
    if (!encode->prefix_bits) return;

    encode->prefix = bit_decode(encode->prefix_bits);
    encode->prefix <<= (encode->prefix_shift + encode->bits);
}

static void decode_state(
        struct bit_decoder *coder,
        struct trie_kvs_info *info)
{
    size_t bits = ceil_div(info->buckets, 4);
    uint64_t mask = (1 << bits) - 1;

    uint64_t state = bit_decode(coder, bits * 2);
    uint32_t s0 = (state >> bits) & mask;
    uint32_t s1 = state & mask;

    info->branches  =  s0 & ~s1;
    info->terminal  = ~s0 &  s1;
    info->tombstone =  s0 &  s1;
}

static struct trie_kv decode_bucket(
        struct bit_decoder coder,
        struct trie_kvs_info *info,
        size_t bucket)
{
    size_t bucket_bits = info->key.bits + info->val.bits;
    bit_decode_skip(&coder, bucket_bits * bucket);

    uint64_t mask = 1ULL << bucket;
    struct trie_kv kv = {
        .terminal = info->terminal & mask,
        .tombstone = info->tombstone & mask
    };

    kv.key = bit_decode(&coder, info->key.bits) << info->key.shift;
    kv.key |= info->key.prefix << info->key.bits;

    kv.val = bit_decode(&coder, info->val.bits) << info->val.shift;
    kv.val |= info->val.prefix << info->val.bits;

    return kv;
}

void trie_kvs_decode(
        struct trie_kvs_info *info,
        const void *data, size_t data_n)
{
    struct bit_decoder coder;
    bit_decoder_init(&coder, data, data_n);

    memset(info, 0, sizeof(struct trie_kvs_info));

    bit_decode_skip(&coder, 4); // lock.
    info->key_len = bit_decode(&coder, 4) << 2;

    decode_info(&coder, &info->key, info->key_len);
    decode_info(&coder, &info->val, 64);

    info->buckets = bit_decode(&coder, 8);

    decode_prefix(&coder, &info->key);
    decode_prefix(&coder, &info->val);

    info->state_offset = bit_decoder_offset(&coder);
    decode_state(&coder, info);

    info->bucket_offset = bit_decoder_offset(&coder);
}


// -----------------------------------------------------------------------------
// encode
// -----------------------------------------------------------------------------

static void encode_info(
        struct bit_encoder *coder,
        struct trie_kvs_encode_info *encode,
        size_t max_bits)
{
    bit_encode(coder, encode->bits >> 2, 4);
    bit_encode(coder, encode->shift >> 2, 4);

    bit_encode(coder, encode->prefix_bits >> 3, 4);
    bit_encode(coder, encode->prefix_shift >> 2, 4);
}

static void encode_prefix(
        struct bit_encoder *coder,
        struct trie_kvs_encode_info *encode)
{
    if (!encode->prefix_bits) return;

    size_t bits = encode->prefix_bits;
    size_t shift = (encode->prefix_shift + encode->bits);

    bit_encode(coder, encode->prefix >> shift, bits);
}

static void encode_state(
        struct bit_encoder *coder,
        struct trie_kvs_info *info)
{
    if (coder->pos) ilka_error("misaligned state at pos <%zu>", coder->pos);

    size_t bits = ceil_div(info->buckets, 4);

    uint64_t s0 = info->branches | info->tombstone;
    uint64_t s1 = info->terminal | info->tombstone;

    bit_encode(coder, s0 << bits | s1, bits * 2);
}

static void encode_bucket(
        struct trie_encoder coder,
        struct trie_kvs_info *info,
        struct trie_kv kv, size_t bucket)
{
    size_t bucket_bits = info->key.bits + info->val.bits;
    bit_encode_skip(&coder, bucket_bits * bucket);

    uint64_t mask = 1ULL << bucket;
    if (kv.terminal) info->terminal |= mask;
    else if (kv.tombstone) info->tombstone |= mask;
    else info->branches |= mask;

    bit_encode(&coder, kv.key >> info->key.shift, info->key.bits);
    bit_encode(&coder, kv.val >> info->val.shift, info->val.bits);
}

void trie_kvs_encode(
        const struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t kvs_n,
        void *data, size_t data_n)
{
    struct bit_encoder coder;
    bit_encoder_init(&coder, data, data_n);

    memset(data, 0, data_n);

    bit_encode_skip(&coder, 4); // lock.
    bit_encode(&coder, info->key_len >> 2, 4);

    encode_info(&coder, &info->key, info->key_len);
    encode_info(&coder, &info->val, 64);

    info->buckets = bit_encode(&coder, 8);

    info->state_offset = bit_encoder_offset(&coder);
    encode_state(&code, info);

    encode_prefix(&coder, &info->key);
    encode_prefix(&coder, &info->val);

    info->bucket_offset = bit_encoder_offset(&coder);

    int is_abs = is_bucket_abs(info);
    for (size_t i = 0; i < kvs_n; ++i) {
        size_t bucket = is_abs ? i : key_abs_bucket(info, kvs[i].key);
        encode_bucket(*coder, info, kvs[i], bucket);
    }
}


// -----------------------------------------------------------------------------
// extract
// -----------------------------------------------------------------------------

size_t trie_kvs_extract(
        const struct trie_kvs_info *info,
        struct trie_kv *kvs, size_t kvs_n,
        const void *data, size_t data_n)
{
    if (kvs_n < info->buckets) {
        ilka_error("kvs array of size <%zu> requires at least <%zu> buckets",
                kvs_n, info->buckets);
    }

    uint64_t buckets = info->branches | info->tombstone | info->terminal;

    struct bit_decoder coder;
    bit_decoder_init(&coder, data, data_n);

    size_t i = 0;
    for (size_t bucket = 0; bucket < info->buckets; ++bucket) {
        uint64_t mask = 1ULL << bucket;
        if (!(buckets & mask)) continue;

        kvs[i] = decode_bucket(coder, bucket);
        if (i && kvs[i].key == kvs[i - 1].key) continue;

        ++i;
    }

    return i;
}


// -----------------------------------------------------------------------------
// add/set
// -----------------------------------------------------------------------------

void trie_kvs_add(struct trie_kv *kvs, size_t kvs_n, struct trie_kv kv)
{
    for (size_t i = 0; i < kvs_n; ++i) {
        struct trie_kv *it = &kvs[i];

        if (!(it->branch | it->terminal | it->tombstone)) {
            *it = kv;
            return;
        }

        if (kv.key > it->key) continue;
        if (kv.key == it->key)
            ilka_error("inserting duplicate key <%p>", (void*) kv.key);

        struct trie_kv tmp = *it;
        *it = kv;
        kv = tmp;
    }

    ilka_error("kvs of size <%zu> is too small to insert <%p>",
            kvs_n, (void*) kv.key);
}

void trie_kvs_set(struct trie_kv *kvs, size_t kvs_n, struct trie_kv kv)
{
    for (size_t i = 0; i < kvs_n; ++i) {
        if (kv.key != kvs[i].key) continue;

        kvs[i] = kv;
        return;
    }

    ilka_error("key <%p> doesn't exist in kvs", (void*) kv.key);
}


// -----------------------------------------------------------------------------
// add/set inplace
// -----------------------------------------------------------------------------

static int can_add_bucket(struct trie_kvs_info *info, uint64_t key)
{
    if (is_bucket_abs(info)) {
        uint64_t buckets = branches | terminal | tombstone;
    }
}

static int can_encode(
        struct trie_kvs_encode_info *encode, uint64_t value, uint64_t max_bits)
{
    if (value & ((1 << encode->shift) - 1)) return 0;

    uint64_t prefix = value & ~((1 << (encode->shift + encode->bits)) - 1);
    if (prefix ^ encode->prefix) return 0;

    return 1;
}

int trie_kvs_can_add_inplace(struct trie_kvs_info *info, struct trie_kv kv)
{
    return can_add_bucket(info, kv.key)
        && can_encode(&info->key, kv.key, info->key_len)
        && can_encode(&info->val, kv.val, 64);
}

void trie_kvs_add_inplace(
        struct trie_kvs_info *info,
        struct trie_kv kv,
        void *data, size_t data_n)
{

}

int trie_kvs_can_set_inplace(struct trie_kvs_info *info, struct trie_kv kv)
{

}

void trie_kvs_set(struct trie_kvs_info *info, struct trie_kv kv, void *data, size_t data_n)
{

}

void trie_kvs_rmv(
        struct trie_kvs_info *info,
        uint64_t key,
        void *data, size_t data_n)
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


// -----------------------------------------------------------------------------
// burst
// -----------------------------------------------------------------------------

void trie_kvs_burst(
        struct trie_kvs_burst_info *burst,
        const struct trie_kv *kvs, size_t n)
{

}
