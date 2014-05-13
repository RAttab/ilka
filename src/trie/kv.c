/* kv.c
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   kv implementation.


Static layout:

   byte  bit  len  field                   f(x)

      0    0    1  lock
           1    3  flags                   is_abs_buckets
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

   - if is_abs_bucket is set then no keys are stored since they can be derived
     from the bucket index.

   - if !is_abs_bucket then multiple tombstoned buckets may be found for a
     single key but only one non-tombstoned bucket exists at any given time.

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

static size_t
key_to_bucket(struct trie_kvs_info *info, uint64_t key)
{
    uint64_t bucket = key >> info->key.shift;
    bucket &= (1ULL << info->key.bits) - 1;
    return bucket;
}

static enum trie_kvs_state
get_bucket_state(struct trie_kvs_info *info, size_t bucket)
{
    size_t i = bucket > 32 ? 1 : 0;
    uint64_t state = info->state[i];
    return (state >> (bucket % 32 * 2)) & 0x3;
}

static void
set_bucket_state(
        struct trie_kvs_info *info, size_t bucket, enum trie_kvs_state s)
{
    uint64_t state = s;
    size_t i = bucket > 32 ? 1 : 0;
    size_t shift = (bucket % 32 * 2);

    info->state[i] &= ~(0x3 << shift);
    info->state[i] |= (state & 0x3) << shift;
}

static size_t
next_bucket(struct trie_kvs_info *info, size_t bucket)
{
    if (bucket <= 32) {
        size_t bit = bitfield_next(info->state[0], bucket * 2) / 2;
        if (bit < 32) return bit;

        bucket = 0;
    }
    else bucket %= 32;

    return bit_field_next(info->state[1], bucket * 2) / 2;
}


// -----------------------------------------------------------------------------
// calc kvs
// -----------------------------------------------------------------------------

static void
adjust_bits(struct trie_kvs_encode_info *encode, size_t max_bits)
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

static void
calc_bits(struct trie_kvs_info *info, const struct trie_kv *kvs, size_t kvs_n)
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

static void
calc_buckets(struct trie_kvs_info *info)
{
    size_t leftover = ILKA_CACHE_LINE - STATIC_HEADER_SIZE;
    leftover -= info->key.prefix_bits;
    leftover -= info->val.prefix_bits;

    size_t abs_buckets = 1ULL << info->key.bits;
    size_t abs_avail = (leftover - abs_buckets * 2) / info->val.bits;
    if (abs_avail >= abs_buckets) {
        info->buckets = abs_buckets;
        info->is_abs_buckets = 1;
        return;
    }

    size_t bucket_size = info->key.bits + info->val.bits;

    info->buckets = leftover / bucket_size;
    if (info->buckets > 32) info->buckets = 32;

    while (info->buckets * 2 + info->buckets * bucket_size > leftover)
        info->buckets--;
}

void
trie_kvs_info(
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

static void
decode_info(
        struct bit_decoder *coder,
        struct trie_kvs_encode_info *encode,
        size_t max_bits)
{
    encode->bits = bit_decode(coder, 4) << 2;
    encode->shift = bit_decode(coder, 4) << 2;

    encode->prefix_bits = bit_decode(&coder, 4) << 3;
    encode->prefix_shift = bit_decode(&coder, 4) << 2;
}

static void
decode_prefix(struct bit_decoder *coder, struct trie_kvs_encode_info *encode)
{
    if (!encode->prefix_bits) return;

    encode->prefix = bit_decode(encode->prefix_bits);
    encode->prefix <<= (encode->prefix_shift + encode->bits);
}

static void
decode_states(struct bit_decoder *coder, struct trie_kvs_info *info)
{
    size_t bits = ceil_div(info->buckets * 2, 4);

    size_t b0 = info->buckets <= 32 ? bits : 64;
    size_t b1 = info->buckets <= 32 ?    0 : bits - 64;

    /* atomic acquire: states must be fully read before we read any buckets. */
    info->state[0] = bit_decode_atomic(coder, b0, memory_order_acquire);
    info->state[1] = bit_decode_atomic(coder, b1, memory_order_acquire);
}

static struct trie_kv
decode_bucket(
        struct trie_kvs_info *info, size_t bucket,
        const void *data, size_t data_n)
{
    struct bit_decoder coder;
    bit_decoder_init(&coder, data, data_n);

    size_t bucket_bits = info->key.bits + info->val.bits;
    bit_decode_skip(&coder, info->bucket_offset + bucket_bits * bucket);

    int state = bucket_state(info, bucket);
    struct trie_kv kv = {
        .branch = state == state_branch,
        .terminal = state == state_terminal,
        .tombstone = state == state_tombstone
    };

    /* atomic relaxed: we only need to ensure that the value is read
     * atomically. Ordering is ensured by the state bitfield. */
    enum memory_order order = memory_order_relaxed;

    if (info->is_abs_buckets) kv.key = bucket;
    else kv.key = bit_decode_atomic(&coder, info->key.bits, order);
    kv.key = (kv.key << info->key.shift) | info->key.prefix;

    kv.val = bit_decode_atomic(&coder, info->val.bits, memory_order_relaxed);
    kv.val = (kv.val << info->val.shift) | info->val.prefix;

    return kv;
}

void
trie_kvs_decode(struct trie_kvs_info *info, const void *data, size_t data_n)
{
    struct bit_decoder coder;
    bit_decoder_init(&coder, data, data_n);

    memset(info, 0, sizeof(struct trie_kvs_info));

    bit_decode_skip(&coder, 1); // lock.
    info->is_abs_buckets = bit.decode(&coder, 3);
    info->key_len = bit_decode(&coder, 4) << 2;

    decode_info(&coder, &info->key, info->key_len);
    decode_info(&coder, &info->val, 64);

    info->buckets = bit_decode(&coder, 8);

    decode_prefix(&coder, &info->key);
    decode_prefix(&coder, &info->val);

    info->state_offset = bit_decoder_offset(&coder);
    decode_states(&coder, info);

    info->bucket_offset = bit_decoder_offset(&coder);
}


// -----------------------------------------------------------------------------
// encode
// -----------------------------------------------------------------------------

static void
encode_info(
        struct bit_encoder *coder,
        struct trie_kvs_encode_info *encode,
        size_t max_bits)
{
    bit_encode(coder, encode->bits >> 2, 4);
    bit_encode(coder, encode->shift >> 2, 4);

    bit_encode(coder, encode->prefix_bits >> 3, 4);
    bit_encode(coder, encode->prefix_shift >> 2, 4);
}

static void
encode_prefix(struct bit_encoder *coder, struct trie_kvs_encode_info *encode)
{
    if (!encode->prefix_bits) return;

    size_t bits = encode->prefix_bits;
    size_t shift = (encode->prefix_shift + encode->bits);

    bit_encode(coder, encode->prefix >> shift, bits);
}

static void
encode_states(struct bit_encoder *coder, struct trie_kvs_info *info)
{
    size_t bits = ceil_div(info->buckets * 2, 4);
    bit_encode(coder, info->state[0], info->buckets <= 32 ? bits : 64);
    bit_encode(coder, info->state[1], info->buckets <= 32 ?    0 : bits - 64);
}

static void
encode_state(
        struct trie_kvs_info *info,
        size_t bucket, enum trie_kvs_state state,
        void *data, size_t data_n)
{
    struct bit_encoder coder;
    bit_encoder_init(&coder, data, data_n);
    bit_encode_skip(coder, info->state_offset + bucket * 2);

    /* atomic release: make sure that any written buckets are visible before
     * updating the state. */
    bit_encode_atomic(coder, state, 2, memory_order_release);
}

static int
can_encode(struct trie_kvs_encode_info *encode, uint64_t value, uint64_t max_bits)
{
    if (value & ((1 << encode->shift) - 1)) return 0;

    uint64_t prefix = value & ~((1 << (encode->shift + encode->bits)) - 1);
    if (prefix ^ encode->prefix) return 0;

    return 1;
}

static void
encode_bucket(
        struct trie_kvs_info *info,
        size_t bucket, struct trie_kv kv,
        void *data, size_t data_n)
{
    struct bit_encoder coder;
    bit_encoder_init(&coder, data, data_n);

    size_t bucket_bits = info->key.bits + info->val.bits;
    bit_encode_skip(&coder, info->bucket_offset + bucket_bits * bucket);

    /* atomic relaxed: we only need to ensure that that we write the entire
     * value in one instruction. Ordering is enforced by states. */
    enum memory_order order = memory_order_relaxed;

    if (!info->is_abs_buckets)
        bit_encode_atomic(&coder, kv.key >> info->key.shift, info->key.bits, order);
    bit_encode_atomic(&coder, kv.val >> info->val.shift, info->val.bits, order);
}

void
trie_kvs_encode(
        const struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t kvs_n,
        void *data, size_t data_n)
{
    struct bit_encoder coder;
    bit_encoder_init(&coder, data, data_n);

    memset(data, 0, data_n);

    bit_encode_skip(&coder, 1); // lock.

    bit_encode(&coder, info->is_abs_buckets, 3);
    bit_encode(&coder, info->key_len >> 2, 4);

    encode_info(&coder, &info->key, info->key_len);
    encode_info(&coder, &info->val, 64);

    info->buckets = bit_encode(&coder, 8);

    info->state = {};
    for (size_t i = 0; i < kvs_n; ++i) {
        size_t bucket = !info->is_abs_buckets ? i : key_to_bucket(info, kvs[i].key);
        set_bucket_state(info, bucket, kv.state);
    }

    info->state_offset = bit_encoder_offset(&coder);
    encode_states(&code, info);

    encode_prefix(&coder, &info->key);
    encode_prefix(&coder, &info->val);

    info->bucket_offset = bit_encoder_offset(&coder);

    for (size_t i = 0; i < kvs_n; ++i) {
        size_t bucket = !info->is_abs_buckets ? i : key_to_bucket(info, kvs[i].key);
        encode_bucket(info, bucket, kvs[i], data, data_n);
    }
}


// -----------------------------------------------------------------------------
// find key
// -----------------------------------------------------------------------------

static size_t
find_bucket(
        struct trie_kvs_info *info, uint64_t key,
        const void *data, size_t data_n)
{
    if (info->is_abs_buckets) return key_to_bucket(key);

    size_t match = 64;

    for (size_t bucket = next_bucket(info, 0);
         bucket < info->buckets;
         bucket = next_bucket(info, 0))
    {
        struct trie_kv kv = decode_bucket(info, bucket, data, data_n);

        if (kv.key != key) continue;

        /* there may be multiple tombstoned bucket for one key but there can
         * only be one non-tomstoned bucket for a given key. */
        if (kv.state != trie_kvs_state_tombstone) return bucket;
        match = bucket;
    }

    return match;
}


// -----------------------------------------------------------------------------
// extract
// -----------------------------------------------------------------------------

size_t
trie_kvs_extract(
        const struct trie_kvs_info *info,
        struct trie_kv *kvs, size_t kvs_n,
        const void *data, size_t data_n)
{
    if (kvs_n < info->buckets) {
        ilka_error("kvs array of size <%zu> requires at least <%zu> buckets",
                kvs_n, info->buckets);
    }

    size_t i = 0;
    for (size_t bucket = next_bucket(info, 0);
         bucket < info->buckets;
         bucket = next_bucket(info, bucket + 1))
    {
        int skip = 0;
        struct trie_kv kv = decode_bucket(info, bucket, data, data_n);

        for (size_t j = 0; !skip && j < i; ++j) {
            if (kv.key != kvs[j].key) continue;
            if (kvs[j].state == trie_kvs_state_tombstone) kvs[j] = kv;
            skip = 1;
        }

        if (!skip) kvs[i++] = kv;
    }

    return i;
}


// -----------------------------------------------------------------------------
// lock
// -----------------------------------------------------------------------------

void
trie_kvs_lock(struct ilka_region *r, void* data)
{
    ilka_region_lock(r, data, 0x1);
}

void trie_kvs_unlock(struct ilka_region *r, void* data)
{
    ilka_region_unlock(r, data, 0x1);
}


// -----------------------------------------------------------------------------
// add
// -----------------------------------------------------------------------------

void
trie_kvs_add(struct trie_kv *kvs, size_t kvs_n, struct trie_kv kv)
{
    for (size_t i = 0; i < kvs_n; ++i) {
        struct trie_kv *it = &kvs[i];

        if (it->state == trie_kvs_state_empty) {
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

static int
can_add_bucket(struct trie_kvs_info *info, uint64_t key)
{
    if (!info->is_abs_buckets)
        return next_bucket(info, 0) < info->buckets;

    size_t bucket = key_to_bucket(info, key);
    if (get_bucket_state(info, bucket) == trie_kvs_state_empty) return 1;

    ilka_error("trying to add duplicate key <%p>", (void*) key);
}

int
trie_kvs_can_add_inplace(struct trie_kvs_info *info, struct trie_kv kv)
{
    return can_add_bucket(info, kv.key)
        && can_encode(&info->key, kv.key, info->key_len)
        && can_encode(&info->val, kv.val, 64);
}

void
trie_kvs_add_inplace(
        struct trie_kvs_info *info,
        struct trie_kv kv,
        void *data, size_t data_n)
{
    size_t bucket = !info->is_abs_buckets ? next_bucket(info, 0) : key_to_bucket(kv[i].key);
    encode_bucket(info, bucket, kv, data, data_n);
    encode_state(info, bucket, kv.state, data, data_n);
}


// -----------------------------------------------------------------------------
// set
// -----------------------------------------------------------------------------

void
trie_kvs_set(struct trie_kv *kvs, size_t kvs_n, struct trie_kv kv)
{
    for (size_t i = 0; i < kvs_n; ++i) {
        if (kv.key != kvs[i].key) continue;
        kvs[i] = kv;
        return;
    }

    ilka_error("key <%p> doesn't exist in kvs", (void*) kv.key);
}

int
trie_kvs_can_set_inplace(struct trie_kvs_info *info, struct trie_kv kv)
{
    return can_add_bucket(info, kv)
        && can_encode(&info->val, kv.val, 64);
}

void
trie_kvs_set_inplace(
        struct trie_kvs_info *info, struct trie_kv kv,
        void *data, size_t data_n)
{
    size_t bucket = find_bucket(info, 0, data, data_n);
    encode_bucket(info, bucket, kv, data, data_n);
    encode_state(info, bucket, kv.state, data, data_n);
}


// -----------------------------------------------------------------------------
// remove
// -----------------------------------------------------------------------------

void
trie_kvs_remove(
        struct trie_kvs_info *info, uint64_t key,
        void *data, size_t data_n)
{
    size_t bucket = find_bucket(info, key, data, data_n);
    ilka_assert(bucket < info->buckets, "removing empty bucket");

    enum trie_kvs_state state = info->is_abs_buckets ?
        trie_kvs_state_empty : trie_kvs_state_tombstone;

    encode_state(info, bucket, state, data, data_n);
}


// -----------------------------------------------------------------------------
// read
// -----------------------------------------------------------------------------

struct trie_kv
trie_kvs_get(struct trie_kvs_info *info, struct trie_key_it *it)
{

}


struct trie_kv
trie_kvs_lb(struct trie_kvs_info *info, struct trie_key_it *it)
{

}

struct trie_kv
trie_kvs_ub(struct trie_kvs_info *info, struct trie_key_it *it)
{

}


// -----------------------------------------------------------------------------
// burst
// -----------------------------------------------------------------------------

void
trie_kvs_burst(
        struct trie_kvs_burst_info *burst,
        const struct trie_kv *kvs, size_t kvs_n)
{

}
