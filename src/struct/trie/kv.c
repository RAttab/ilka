/* kv.c
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   kv implementation.


Static layout:

   byte  bit  len  field                   f(x)

      0    0    1  has_value
           1    1  is_abs_buckets
           2    6  key_len                 x << 1
      1    0    4  value_bits              (x << 2) + 1
           4    4  value_shift             x << 2
      2    0    4  key.bits                (x << 2) + 1
           4    4  key.shift               x << 2
      3    0    4  key.prefix_bits         x << 2
           4    4  key.prefix_shift        x << 2
      4    0    4  val.bits                (x << 2) + 1
           4    4  val.shift               x << 2
      5    0    4  val.prefix_bits         x << 2
           4    4  val.prefix_shift        x << 2
      6    0    8  buckets

Dynamic layout:

   field        range   len

   value        [0, 7]  value_bits - value_shift;
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

   - All bits field would naturally have a range of 0-64 which requires 5 bits
     to encode after shifting. To get around that issue we either extreme value
     of that range so that everything fits in 4 bits.

     value_bits: We split off the fifth bit into the has_value flag.
     key_bits and val_bits: can't be 0 so we substract one before encoding.
     prefix_bits: can't be 64 because key_bits and val_bits can't be 0.

   - since there are currently 3 states that are mutally exclusive so we encode
     them with 2 bits for each buckets.

   - prefixes are omitted if their corresponding prefix_bits are 0. Note that
     neither prefix_len nor prefix_shift will be omitted. They will simply be 0.

   - if is_abs_bucket is set then no keys are stored since they can be derived
     from the bucket index.

Todo:

   - Could try to run the same compression but over ~ of the value. Choose which
     one compresses at the end. Unlikely to be worth it though.

   - Could try to detect sequences (4, 5, 6, 7...) but again, not sure it's
     worth it.
*/

#include "kv.h"
#include "utils/arch.h"
#include "utils/bits.h"
#include "utils/bit_coder.h"

#include <string.h>
#include <stdio.h>

// -----------------------------------------------------------------------------
// constants
// -----------------------------------------------------------------------------

enum { STATIC_HEADER_SIZE = 7 };

// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static size_t
key_to_bucket(const struct trie_kvs_info *info, uint64_t key)
{
    uint64_t bucket = key >> info->key.shift;
    bucket &= (1ULL << info->key.bits) - 1;
    return bucket;
}

static size_t
calc_bucket_size(const struct trie_kvs_info *info)
{
    size_t size = info->is_abs_buckets ? 0 : info->key.bits + info->key.padding;
    return size+ info->val.bits + info->val.padding;
}

static enum trie_kvs_state
get_bucket_state(const struct trie_kvs_info *info, size_t bucket)
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
    size_t i = bucket < 32 ? 0 : 1;
    size_t shift = (bucket % 32) * 2;

    info->state[i] &= ~(0x3UL << shift);
    info->state[i] |= state << shift;
}

static size_t
next_bucket_impl(const struct trie_kvs_info *info, size_t bucket)
{
    if (bucket <= 32) {
        size_t bit = bitfield_next(info->state[0], bucket * 2) / 2;
        if (bit < 32) return bit;

        bucket = 0;
    }
    else bucket %= 32;

    return bitfield_next(info->state[1], bucket * 2) / 2;
}

static size_t
next_bucket(const struct trie_kvs_info *info, size_t bucket)
{
    while (bucket < info->buckets) {
        bucket = next_bucket_impl(info, bucket);
        if (get_bucket_state(info, bucket) != trie_kvs_state_tombstone) break;
        bucket++;
    }

    return bucket;
}

static size_t
next_empty_bucket_impl(uint64_t bf)
{
    const uint64_t mask = 0x5555555555555555ULL;

    uint64_t s = ((bf >> 1) | bf) & mask;
    s |= s << 1;
    return ctz(~s) / 2;
}

static size_t
next_empty_bucket(const struct trie_kvs_info *info)
{
    size_t bucket = next_empty_bucket_impl(info->state[0]);
    if (bucket < 32) return bucket;

    if (info->buckets <= 32) return info->buckets;
    return next_empty_bucket_impl(info->state[1]) + 32;
}


// -----------------------------------------------------------------------------
// calc kvs
// -----------------------------------------------------------------------------

static void
calc_value(uint64_t value, uint8_t *bits, uint8_t *shift)
{
    if (!value) {
        *bits = 0;
        return;
    }

    *shift = ctz(value) & ~0x3;
    *bits = 64 - clz(value >> *shift);
    *bits = ceil_div(*bits, 4) * 4;
}

static void
adjust_bits(struct trie_kvs_encode_info *encode, size_t max_bits)
{
    if (encode->prefix_bits < 64) {
        encode->shift &= ~0x3;
        encode->bits = max_bits - encode->prefix_bits - encode->shift;
        encode->bits = ceil_div(encode->bits, 4) * 4;
    }
    else {
        encode->bits = 4; /* can't be 0. */
        encode->shift = 0;
    }

    size_t non_prefix_bits = encode->bits + encode->shift;
    encode->prefix_bits = max_bits - non_prefix_bits;

    uint64_t mask = encode->prefix_bits ? ~((1ULL << non_prefix_bits) - 1) : 0;
    encode->prefix &= mask;

    calc_value(encode->prefix, &encode->prefix_bits, &encode->prefix_shift);
}

static void
calc_bits(struct trie_kvs_info *info, const struct trie_kv *kvs, size_t kvs_n)
{
    uint64_t key_same = -1ULL;
    uint64_t val_same = -1ULL;

    info->key.shift = info->key_len;
    info->val.shift = 64;

    info->key.prefix = kvs[0].key;
    info->val.prefix = kvs[0].val;

    for (size_t i = 0; i < kvs_n; ++i) {
        key_same &= ~(kvs[0].key ^ kvs[i].key);
        val_same &= ~(kvs[0].val ^ kvs[i].val);

        uint8_t key_shift = ctz(kvs[i].key);
        uint8_t val_shift = ctz(kvs[i].val);

        if (key_shift < info->key.shift) info->key.shift = key_shift;
        if (val_shift < info->val.shift) info->val.shift = val_shift;
    }

    info->key.prefix_bits = clz(~key_same);
    info->val.prefix_bits = clz(~val_same);

    info->key.prefix_bits -= 64 - info->key_len;

    adjust_bits(&info->key, info->key_len);
    adjust_bits(&info->val, 64);
}

static void
calc_padding(struct trie_kvs_info *info)
{
    if (!((info->key.bits == 64) ^ (info->val.bits == 64))) return;
    if (!((info->key.bits % 8) ^ (info->val.bits % 8))) return;

    if (info->key.bits == 64)
        info->val.padding = 8 - (info->val.bits % 8);
    else info->key.padding = 8 - (info->key.bits % 8);
}

static void
calc_buckets(struct trie_kvs_info *info)
{
    size_t leftover = (ILKA_CACHE_LINE - STATIC_HEADER_SIZE) * 8;
    leftover -= info->value_bits;
    leftover -= info->key.prefix_bits;
    leftover -= info->val.prefix_bits;

    /* padding before the states bitfield */
    if (leftover % 8) leftover += 8 - (leftover % 8);

    if (info->key.bits == 4) {
        size_t abs_key_bits =
            info->key_len < info->key.bits ? info->key_len : info->key.bits;
        size_t abs_buckets = 1UL << abs_key_bits;
        size_t abs_bucket_bits = info->val.bits ? info->val.bits : 1;
        size_t abs_avail = (leftover - abs_buckets * 2) / abs_bucket_bits;

        if (abs_avail >= abs_buckets) {
            info->buckets = abs_buckets;
            info->is_abs_buckets = 1;
            return;
        }
    }

    size_t bucket_size = calc_bucket_size(info);

    info->buckets = leftover / bucket_size;
    if (info->buckets > 64) info->buckets = 64;

    while (info->buckets * 2 + info->buckets * bucket_size > leftover)
        info->buckets--;
}

int
trie_kvs_info(
        struct trie_kvs_info *info, size_t key_len,
        int has_value, uint64_t value,
        const struct trie_kv *kvs, size_t kvs_n)
{
    if (!is_pow2(key_len))
        ilka_error("key length <%zu> must be a power of 2", key_len);

    memset(info, 0, sizeof(struct trie_kvs_info));
    info->key_len = key_len;
    info->size = ILKA_CACHE_LINE;

    if (has_value) {
        info->has_value = 1;
        info->value = value;
        calc_value(info->value, &info->value_bits, &info->value_shift);

        /* Since 64 is a valid value, we need to cap the lower end to 4 to fit
         * the encoded value within 4 bit. */
        if (!info->value_bits) info->value_bits = 4;
    }

    if (kvs_n) {
        calc_bits(info, kvs, kvs_n);
        calc_padding(info);
        calc_buckets(info);
    }
    /* While we have no buckets, bits can't be 0 if to get consistent decoding */
    else info->key.bits = info->val.bits = info->key_len;

    return kvs_n <= info->buckets;
}


// -----------------------------------------------------------------------------
// decode
// -----------------------------------------------------------------------------

static void
decode_info(struct bit_decoder *coder, struct trie_kvs_encode_info *encode)
{
    encode->bits = (bit_decode(coder, 4) + 1) << 2;
    encode->shift = bit_decode(coder, 4) << 2;

    encode->prefix_bits = bit_decode(coder, 4) << 2;
    encode->prefix_shift = bit_decode(coder, 4) << 2;
}

static void
decode_states(struct bit_decoder *coder, struct trie_kvs_info *info)
{
    bit_decode_align(coder);

    size_t bits = info->buckets * 2;

    /* atomic acquire: states must be fully read before we read any buckets. */
    enum morder order = morder_relaxed;

    if (info->buckets <= 32)
        info->state[0] = bit_decode_atomic(coder, bits, order);
    else {
        info->state[0] = bit_decode_atomic(coder, 64, order);
        info->state[1] = bit_decode_atomic(coder, bits - 64, order);
    }
}

static struct trie_kv
decode_bucket(
        const struct trie_kvs_info *info, size_t bucket, const void *data)
{
    struct bit_decoder coder;
    bit_decoder_init(&coder, data, info->size);

    size_t bucket_bits = calc_bucket_size(info);
    bit_decode_skip(&coder, info->bucket_offset + bucket_bits * bucket);

    struct trie_kv kv = {
        .state = get_bucket_state(info, bucket)
    };

    /* atomic relaxed: we only need to ensure that the value is read
     * atomically. Ordering is ensured by the state bitfield. */
    enum morder order = morder_relaxed;

    if (!info->is_abs_buckets) {
        kv.key = bit_decode_atomic(&coder, info->key.bits, order);
        bit_decode_skip(&coder, info->key.padding);
    }
    else kv.key = bucket;
    kv.key = (kv.key << info->key.shift) | info->key.prefix;

    kv.val = bit_decode_atomic(&coder, info->val.bits, order);
    kv.val = (kv.val << info->val.shift) | info->val.prefix;

    return kv;
}

void
trie_kvs_decode(struct trie_kvs_info *info, const void *data)
{
    memset(info, 0, sizeof(struct trie_kvs_info));
    info->size = ILKA_CACHE_LINE;

    struct bit_decoder coder;
    bit_decoder_init(&coder, data, info->size);

    info->has_value = bit_decode(&coder, 1);
    info->is_abs_buckets = bit_decode(&coder, 1);
    info->key_len = bit_decode(&coder, 6) << 1;

    if (info->has_value) {
        info->value_bits = (bit_decode(&coder, 4) + 1) << 2;
        info->value_shift = bit_decode(&coder, 4) << 2;
    }
    else bit_decode_skip(&coder, 8);

    decode_info(&coder, &info->key);
    decode_info(&coder, &info->val);
    calc_padding(info);

    info->buckets = bit_decode(&coder, 8);

    info->value_offset = bit_decoder_offset(&coder);
    if (info->has_value)
        info->value = bit_decode(&coder, info->value_bits) << info->value_shift;
    info->key.prefix = bit_decode(&coder, info->key.prefix_bits) << info->key.prefix_shift;
    info->val.prefix = bit_decode(&coder, info->val.prefix_bits) << info->val.prefix_shift;

    info->state_offset = bit_decoder_offset(&coder);
    decode_states(&coder, info);

    bit_decode_align(&coder);
    info->bucket_offset = bit_decoder_offset(&coder);
}


// -----------------------------------------------------------------------------
// encode
// -----------------------------------------------------------------------------

static void
encode_info(struct bit_encoder *coder, const struct trie_kvs_encode_info *encode)
{
    bit_encode(coder, (encode->bits >> 2) - 1, 4);
    bit_encode(coder, encode->shift >> 2, 4);

    bit_encode(coder, encode->prefix_bits >> 2, 4);
    bit_encode(coder, encode->prefix_shift >> 2, 4);
}

static void
encode_states(struct bit_encoder *coder, struct trie_kvs_info *info)
{
    bit_encode_align(coder);

    size_t bits = info->buckets * 2;

    if (info->buckets <= 32)
        bit_encode(coder, info->state[0], bits);
    else {
        bit_encode(coder, info->state[0], 64);
        bit_encode(coder, info->state[1], bits - 64);
    }
}

static void
encode_state(
        struct trie_kvs_info *info,
        size_t bucket, enum trie_kvs_state state,
        void *data)
{
    struct bit_encoder coder;
    bit_encoder_init(&coder, data, info->size);
    bit_encode_skip(&coder, info->state_offset + bucket * 2);

    /* atomic release: make sure that any written buckets are visible before
     * updating the state. */
    bit_encode_atomic(&coder, state, 2, morder_release);

    set_bucket_state(info, bucket, state);
}

static int
can_encode(const struct trie_kvs_encode_info *encode, uint64_t value)
{
    if (value & ((1UL << encode->shift) - 1)) return 0;

    uint64_t prefix = value & ~((1UL << (encode->shift + encode->bits)) - 1);
    if (prefix ^ encode->prefix) return 0;

    return 1;
}

static void
encode_bucket(
        struct trie_kvs_info *info,
        size_t bucket, struct trie_kv kv,
        void *data)
{
    struct bit_encoder coder;
    bit_encoder_init(&coder, data, info->size);

    size_t bucket_bits = calc_bucket_size(info);
    bit_encode_skip(&coder, info->bucket_offset + bucket_bits * bucket);

    /* atomic relaxed: we only need to ensure that that we write the entire
     * value in one instruction. Ordering is enforced by states. */
    enum morder order = morder_relaxed;

    if (!info->is_abs_buckets) {
        bit_encode_atomic(&coder, kv.key >> info->key.shift, info->key.bits, order);
        bit_encode_skip(&coder, info->key.padding);
    }

    bit_encode_atomic(&coder, kv.val >> info->val.shift, info->val.bits, order);
}

static void
encode_bucket_value(
        struct trie_kvs_info *info,
        size_t bucket, uint64_t value,
        void *data)
{
    struct bit_encoder coder;
    bit_encoder_init(&coder, data, info->size);

    size_t bucket_bits = calc_bucket_size(info);
    bit_encode_skip(&coder, info->bucket_offset + bucket_bits * bucket);

    if (!info->is_abs_buckets)
        bit_encode_skip(&coder, info->key.bits + info->key.padding);

    /* atomic release: if we're only writting the value then we're not updating
     * the state which means that we need to make it immediately visible to
     * readers. */
    enum morder order = morder_release;
    bit_encode_atomic(&coder, value >> info->val.shift, info->val.bits, order);
}


void
trie_kvs_encode(
        struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t kvs_n,
        void *data)
{
    struct bit_encoder coder;
    bit_encoder_init(&coder, data, info->size);
    memset(data, 0, info->size);

    bit_encode(&coder, info->has_value, 1);
    bit_encode(&coder, info->is_abs_buckets, 1);
    bit_encode(&coder, info->key_len >> 1, 6);

    if (info->has_value) {
        bit_encode(&coder, (info->value_bits >> 2) - 1, 4);
        bit_encode(&coder, info->value_shift >> 2, 4);
    }
    else bit_encode_skip(&coder, 8);

    encode_info(&coder, &info->key);
    encode_info(&coder, &info->val);

    bit_encode(&coder, info->buckets, 8);

    info->value_offset = bit_encoder_offset(&coder);
    bit_encode(&coder, info->value >> info->value_shift, info->value_bits);
    bit_encode(&coder, info->key.prefix >> info->key.prefix_shift, info->key.prefix_bits);
    bit_encode(&coder, info->val.prefix >> info->val.prefix_shift, info->val.prefix_bits);


    info->state[0] = info->state[1] = 0;
    for (size_t i = 0; i < kvs_n; ++i) {
        size_t bucket = !info->is_abs_buckets ? i : key_to_bucket(info, kvs[i].key);
        set_bucket_state(info, bucket, kvs[i].state);
    }
    info->state_offset = bit_encoder_offset(&coder);
    encode_states(&coder, info);


    bit_encode_align(&coder);
    info->bucket_offset = bit_encoder_offset(&coder);
    for (size_t i = 0; i < kvs_n; ++i) {
        size_t bucket = !info->is_abs_buckets ? i : key_to_bucket(info, kvs[i].key);
        encode_bucket(info, bucket, kvs[i], data);
    }
}


// -----------------------------------------------------------------------------
// read kvs
// -----------------------------------------------------------------------------

/* Counts the number of branch or value buckets within the s bitfield. */
static size_t
count_buckets(uint64_t s)
{
    const uint64_t mask = 0x5555555555555555ULL;
    return pop(((s >> 1) ^ s) & mask);
}

size_t
trie_kvs_count(struct trie_kvs_info *info)
{
    return info->has_value
        + count_buckets(info->state[0])
        + count_buckets(info->state[1]);
}

static size_t
find_bucket(struct trie_kvs_info *info, uint64_t key, const void *data)
{
    if (info->is_abs_buckets) return key_to_bucket(info, key);

    for (size_t bucket = next_bucket(info, 0);
         bucket < info->buckets;
         bucket = next_bucket(info, bucket + 1))
    {
        struct trie_kv kv = decode_bucket(info, bucket, data);
        if (kv.key == key) return bucket;
    }

    ilka_error("key <%lu> not found", key);
}

struct trie_kv
trie_kvs_get(struct trie_kvs_info *info, uint64_t key, const void *data)
{
    if (info->is_abs_buckets) {
        size_t bucket = key_to_bucket(info, key);
        return decode_bucket(info, bucket, data);
    }

    for (size_t bucket = next_bucket(info, 0);
         bucket < info->buckets;
         bucket = next_bucket(info, bucket + 1))
    {
        struct trie_kv kv = decode_bucket(info, bucket, data);
        if (kv.key == key) return kv;
    }

    return (struct trie_kv) { .state = trie_kvs_state_empty };
}

struct trie_kv
trie_kvs_lb(struct trie_kvs_info *info, uint64_t key, const void *data)
{
    /* \todo add a is_abs_bucket shortcut by implementing a prev_bucket util */

    struct trie_kv match = { .key = 0, .state = trie_kvs_state_empty };

    for (size_t bucket = next_bucket(info, 0);
         bucket < info->buckets;
         bucket = next_bucket(info, bucket + 1))
    {
        struct trie_kv kv = decode_bucket(info, bucket, data);

        if (kv.key > key) continue;
        if (match.state != trie_kvs_state_empty && kv.key < match.key)
            continue;

        if (kv.key == key) return kv;
        match = kv;
    }

    return match;
}

struct trie_kv
trie_kvs_ub(struct trie_kvs_info *info, uint64_t key, const void *data)
{
    struct trie_kv match = { .key = 0, .state = trie_kvs_state_empty };

    if (info->is_abs_buckets) {
        size_t bucket = next_bucket(info, key_to_bucket(info, key));
        if (bucket >= info->buckets) return match;
        return decode_bucket(info, bucket, data);
    }

    for (size_t bucket = next_bucket(info, 0);
         bucket < info->buckets;
         bucket = next_bucket(info, bucket + 1))
    {
        struct trie_kv kv = decode_bucket(info, bucket, data);

        if (kv.key < key) continue;
        if (match.state != trie_kvs_state_empty && kv.key > match.key)
            continue;

        if (kv.key == key) return kv;
        match = kv;
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
        const void *data)
{
    if (kvs_n < info->buckets) {
        ilka_error("kvs array of size <%zu> requires at least <%d> buckets",
                kvs_n, info->buckets);
    }

    size_t i = 0;
    for (size_t bucket = next_bucket(info, 0);
         bucket < info->buckets;
         bucket = next_bucket(info, bucket + 1))
    {
        kvs[i++] = decode_bucket(info, bucket, data);
    }

    return i;
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

int
trie_kvs_add_inplace(struct trie_kvs_info *info, struct trie_kv kv, void *data)
{
    if (!can_encode(&info->key, kv.key) || !can_encode(&info->val, kv.val))
        return 0;

    size_t bucket = info->is_abs_buckets ?
        key_to_bucket(info, kv.key) : next_empty_bucket(info);
    if (bucket >= info->buckets) return 0;

    {
        enum trie_kvs_state s = get_bucket_state(info, bucket);
        ilka_assert(s == trie_kvs_state_empty || s == trie_kvs_state_tombstone,
                "adding to non-empty bucket");
    }

    encode_bucket(info, bucket, kv, data);
    encode_state(info, bucket, kv.state, data);

    return 1;
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
trie_kvs_set_inplace(struct trie_kvs_info *info, struct trie_kv kv, void *data)
{
    if (!can_encode(&info->val, kv.val)) return 0;

    size_t bucket = find_bucket(info, 0, data);
    {
        enum trie_kvs_state s = get_bucket_state(info, bucket);
        ilka_assert(s == trie_kvs_state_terminal || s == trie_kvs_state_branch,
                "setting empty bucket");
    }

    encode_bucket_value(info, bucket, kv.val, data);
    encode_state(info, bucket, kv.state, data);

    return 1;
}


// -----------------------------------------------------------------------------
// set value
// -----------------------------------------------------------------------------

int
trie_kvs_set_value_inplace(
        struct trie_kvs_info *info, uint64_t value, void *data)
{
    if (!info->has_value) return 0;
    if (value & ((1UL << info->value_shift) - 1)) return 0;

    value >>= info->value_shift;
    if ((64 - clz(value)) > info->value_bits) return 0;


    struct bit_encoder coder;
    bit_encoder_init(&coder, data, info->size);
    bit_encode_skip(&coder, info->value_offset);

    /* atomic release: make sure that the new value is visible as soon as we
     * write it. */
    bit_encode_atomic(&coder, value, info->value_bits, morder_release);

    return 1;
}


// -----------------------------------------------------------------------------
// remove
// -----------------------------------------------------------------------------

void
trie_kvs_remove(struct trie_kvs_info *info, uint64_t key, void *data)
{
    size_t bucket = find_bucket(info, key, data);
    ilka_assert(bucket < info->buckets, "removing empty bucket");

    enum trie_kvs_state state = info->is_abs_buckets ?
        trie_kvs_state_empty : trie_kvs_state_tombstone;

    encode_state(info, bucket, state, data);
}


// -----------------------------------------------------------------------------
// burst
// -----------------------------------------------------------------------------

void
trie_kvs_burst(
        struct trie_kvs_burst_info *burst, size_t key_len,
        const struct trie_kv *kvs, size_t kvs_n)
{
    burst->prefix_bits = burst->suffix_bits = key_len / 2;
    ilka_assert(burst->prefix_bits + burst->suffix_bits == key_len,
            "misaligned burst key len <%zu>", key_len);

    burst->prefixes = 0;
    memset(&burst->prefix, 0, sizeof(burst->prefix));
    memset(&burst->suffix, 0, sizeof(burst->suffix));

    uint64_t prefix_mask = (1ULL << burst->prefix_bits) - 1;
    uint64_t suffix_mask = (1ULL << burst->suffix_bits) - 1;

    for (size_t i = 0; i < kvs_n; ++i) {
        uint64_t prefix = (kvs[i].key >> burst->suffix_bits) & prefix_mask;
        uint64_t suffix = kvs[i].key & suffix_mask;

        size_t j;
        for (j = 0; j < burst->prefixes; ++j) {
            if (burst->prefix[j].key == prefix) break;
        }

        if (j == burst->prefixes) {
            burst->prefix[j] = (struct trie_kv) {
                .key = prefix,
                .state = trie_kvs_state_branch
            };
            burst->prefixes++;
        }

        size_t k = burst->suffix[j].size++;
        burst->suffix[j].kvs[k] = (struct trie_kv) {
            .key = suffix,
            .val = kvs[i].val,
            .state = kvs[i].state
        };
    }
}


// -----------------------------------------------------------------------------
// print
// -----------------------------------------------------------------------------

void
trie_kvs_print_kv(struct trie_kv kv)
{
    char state =
        kv.state == trie_kvs_state_empty ? 'e' :
        kv.state == trie_kvs_state_branch ? 'b' :
        kv.state == trie_kvs_state_terminal ? 'v' : 't';

    printf("{ key=%p, val=%p, state=%c }",
            (void*) kv.key, (void*) kv.val, state);
}

void
print_encode(const struct trie_kvs_encode_info *encode)
{
    printf("{ bits=%d, shift=%d, prefix_bits=%d, prefix_shift=%d, prefix=%p, pad=%d }\n",
            (int) encode->bits, (int) encode->shift,
            (int) encode->prefix_bits, (int) encode->prefix_shift,
            (void*) encode->prefix, (int) encode->padding);
}

void
trie_kvs_print_info(const struct trie_kvs_info *info)
{
    printf("{\n"
            "\tsize=%d\n"
            "\tkey_len=%d\n"
            "\tbuckets=%d\n"
            "\tis_abs_buckets=%d\n"
            "\toffsets={ val=%d, state=%d, bucket=%d }\n"
            "\tstate=[ %p, %p ]\n",
            (int) info->size, (int) info->key_len,
            (int) info->buckets, (int) info->is_abs_buckets,
            (int) info->value_offset, (int) info->state_offset, (int) info->bucket_offset,
            (void*) info->state[0], (void*) info->state[1]);

    if (info->has_value) {
        printf("\tvalue={ bits=%d, shift=%d, value=%p }\n",
                (int) info->value_bits, (int) info->value_shift, (void*) info->value);
    }

    printf("\tkey="); print_encode(&info->key);
    printf("\tval="); print_encode(&info->val);
    printf("}\n");
}
