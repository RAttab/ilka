/* node.c
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   trie node implementation.
*/

#include "node.h"
#include "kv.h"


// -----------------------------------------------------------------------------
// get
// -----------------------------------------------------------------------------

int
trie_node_get(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key_it, uint64_t *value)
{
    while (1) {
        void *p_node = ilka_region_read(r, node);

        struct trie_kvs_info info;
        trie_kvs_decode(&info, p_node);

        if (ilka_key_end(key_it)) {
            if (!info.has_value) return 0;
            *value = info.value;
            return 1;
        }
        if (ilka_key_leftover(key_it) < info.key_len) return 0;

        uint64_t key = ilka_key_pop(&key_it, info.key_len);
        struct trie_kv kv = trie_kvs_get(&info, key, p_node);

        if (kv.state == trie_kvs_state_branch) {
            node = kv.val;
            continue;
        }

        if (kv.state == trie_kvs_state_terminal) {
            if (!ilka_key_end(key_it)) return 0;
            *value = info.value;
            return 1;
        }

        if (kv.state == trie_kvs_state_empty) return 0;

        ilka_error("expected <%d> got <%d>", trie_kvs_state_branch, kv.state);
    }
}


// -----------------------------------------------------------------------------
// lb-ub
// -----------------------------------------------------------------------------

static void
key_bounds(
        struct ilka_key_it *key_it, size_t key_len,
        uint64_t * restrict lb, uint64_t * restrict ub)
{
    size_t leftover = ilka_key_leftover(*key_it);
    size_t n = key_len > leftover ? leftover : key_len;
    size_t shift = key_len - n;

    *lb = ilka_key_pop(key_it, n);
    *lb <<= shift;
    *ub = *lb | ((1ULL << shift) - 1);
}

int
trie_node_lb(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key_it, struct ilka_key_it lb, uint64_t *value)
{
    while (1) {
        void *p_node = ilka_region_read(r, node);

        struct trie_kvs_info info;
        trie_kvs_decode(&info, p_node);

        if (info.has_value && ilka_key_end(key_it)) {
            *value = info.value;
            return 1;
        }

        uint64_t key_lb, key_ub;
        key_bounds(&key_it, info.key_len, &key_lb, &key_ub);
        struct trie_kv kv = trie_kvs_ub(&info, key_lb, p_node);

        if (kv.state == trie_kvs_state_empty) return 0;
        if (kv.key > key_ub) return 0;

        ilka_key_push(&lb, kv.key, info.key_len);

        if (kv.state == trie_kvs_state_terminal) {
            *value = kv.val;
            return 1;
        }

        if (kv.state == trie_kvs_state_branch) {
            node = kv.val;
            continue;
        }

        ilka_error("expected <%d> got <%d>", trie_kvs_state_branch, kv.state);
    }
}

int
trie_node_ub(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key_it, struct ilka_key_it ub, uint64_t *value)
{
    while (1) {
        void *p_node = ilka_region_read(r, node);

        struct trie_kvs_info info;
        trie_kvs_decode(&info, p_node);

        uint64_t key_lb, key_ub;
        key_bounds(&key_it, info.key_len, &key_lb, &key_ub);
        struct trie_kv kv = trie_kvs_lb(&info, key_ub, p_node);

        if (kv.state == trie_kvs_state_empty) {
            if (info.has_value && ilka_key_end(key_it)) {
                *value = info.value;
                return 1;
            }
            return 0;
        }
        if (kv.key > key_ub) return 0;

        ilka_key_push(&ub, kv.key, info.key_len);

        if (kv.state == trie_kvs_state_terminal) {
            *value = kv.val;
            return 1;
        }

        if (kv.state == trie_kvs_state_branch) {
            node = kv.val;
            continue;
        }

        ilka_error("expected <%d> got <%d>", trie_kvs_state_branch, kv.state);
    }
}


// -----------------------------------------------------------------------------
// next-prev
// -----------------------------------------------------------------------------

int
trie_node_next(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key_it, struct ilka_key_it next, uint64_t *value)
{
    void *p_node = ilka_region_read(r, node);

    struct trie_kvs_info info;
    trie_kvs_decode(&info, p_node);

    uint64_t key;
    size_t leftover = ilka_key_leftover(key_it);

    /* Consume as much of the key as possible but also backtrack if there's no
     * next down that branch. */

    if (leftover >= info.key_len) {
        key = ilka_key_pop(&key_it, info.key_len);
        struct trie_kv kv = trie_kvs_get(&info, key, p_node);

        if (kv.state == trie_kvs_state_branch) {
            ilka_key_push(&next, key, info.key_len);

            if (trie_node_next(r, kv.val, key_it, next, value))
                return 1;

            (void) ilka_key_pop(&next, info.key_len);
        }

        /* our next isn't down this branch so start searching after our key. */
        if (key > key + 1) return 0;
        key++;
    }

    else {
        uint64_t key_ub;
        key_bounds(&key_it, info.key_len, &key, &key_ub);
    }


    /* Search for the first element down a branch or return 0 if there's none.
     * Note that info.value should not be checked if we just finished consuming
     * the key. */

    while (1) {
        struct trie_kv kv = trie_kvs_ub(&info, key, p_node);
        if (kv.state == trie_kvs_state_empty) return 0;

        ilka_key_push(&next, kv.key, info.key_len);

        if (kv.state == trie_kvs_state_terminal) {
            *value = kv.val;
            return 1;
        }

        if (kv.state == trie_kvs_state_branch) {
            node = kv.val;
            p_node = ilka_region_read(r, node);
            trie_kvs_decode(&info, p_node);

            if (info.has_value && ilka_key_end(key_it)) {
                *value = info.value;
                return 1;
            }

            uint64_t key_ub;
            key_bounds(&key_it, info.key_len, &key, &key_ub);

            continue;
        }

        ilka_error("expected <%d> got <%d>", trie_kvs_state_branch, kv.state);
    }
}

int
trie_node_prev(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key_it, struct ilka_key_it prev, uint64_t *value)
{
    void *p_node = ilka_region_read(r, node);

    struct trie_kvs_info info;
    trie_kvs_decode(&info, p_node);

    uint64_t key;
    size_t leftover = ilka_key_leftover(key_it);

    /* Consume as much of the key as possible but also backtrack if there's no
     * prev down that branch. */

    if (leftover >= info.key_len) {
        key = ilka_key_pop(&key_it, info.key_len);
        struct trie_kv kv = trie_kvs_get(&info, key, p_node);

        if (kv.state == trie_kvs_state_branch) {
            ilka_key_push(&prev, key, info.key_len);

            if (trie_node_prev(r, kv.val, key_it, prev, value))
                return 1;

            (void) ilka_key_pop(&prev, info.key_len);
        }

        /* our prev isn't down this branch so start searching after our key. */
        if (key < key - 1) return 0;
        key--;
    }

    else {
        uint64_t key_ub;
        key_bounds(&key_it, info.key_len, &key, &key_ub);
    }


    /* Search for the first element down a branch or return 0 if there's none.
     * Note that info.value should not be checked if we just finished consuming
     * the key. */

    while (1) {
        struct trie_kv kv = trie_kvs_lb(&info, key, p_node);
        if (kv.state == trie_kvs_state_empty) return 0;

        ilka_key_push(&prev, kv.key, info.key_len);

        if (kv.state == trie_kvs_state_terminal) {
            *value = kv.val;
            return 1;
        }

        if (kv.state == trie_kvs_state_branch) {
            node = kv.val;
            p_node = ilka_region_read(r, node);
            trie_kvs_decode(&info, p_node);

            if (info.has_value && ilka_key_end(key_it)) {
                *value = info.value;
                return 1;
            }

            uint64_t key_lb;
            key_bounds(&key_it, info.key_len, &key_lb, &key);

            continue;
        }

        ilka_error("expected <%d> got <%d>", trie_kvs_state_branch, kv.state);
    }
}


// -----------------------------------------------------------------------------
// add
// -----------------------------------------------------------------------------

static ilka_ptr_t
add_burst(
        struct ilka_region *r, size_t key_len,
        int has_value, uint64_t node_value,
        struct trie_kv *kvs, size_t kvs_n,
        struct ilka_key_it key_it, uint64_t value);


static ilka_ptr_t
write_node(
        struct ilka_region *r, size_t key_len,
        int has_value, uint64_t value,
        struct trie_kv *kvs, size_t kvs_n)
{
    struct trie_kvs_info info;
    trie_kvs_info(&info, key_len, has_value, value, kvs, kvs_n);

    ilka_ptr_t node = ilka_region_alloc(r, info.size, info.size);
    {
        void *p_node = ilka_region_pin_write(r, node);
        trie_kvs_encode(&info, kvs, kvs_n, p_node);
        ilka_region_unpin_write(r, p_node);
    }
    return node;
}

static ilka_ptr_t
new_node(
        struct ilka_region *r,
        int has_value, uint64_t node_value,
        struct ilka_key_it key_it, uint64_t value)
{
    struct trie_kv kv;
    size_t bits = ilka_key_leftover(key_it);

    if (bits <= 64 && pop(bits) == 1) {
        kv.key = ilka_key_pop(&key_it, bits);
        kv.val = value;
        kv.state = trie_kvs_state_terminal;
    }
    else {
        bits = leading_bit(bits);
        if (bits > 64) bits = 64;

        kv.key = ilka_key_pop(&key_it, bits);
        kv.val = new_node(r, 0, 0, key_it, value);
        kv.state = trie_kvs_state_branch;
    }

    return write_node(r, bits, has_value, node_value, &kv, 1);
}


static int
add_burst_suffix(
        struct ilka_region *r,
        struct trie_kvs_burst_info *burst,
        struct ilka_key_it *key_it, uint64_t value,
        uint64_t prefix, int add_to_suffix, int is_value_of_prefix)
{
    size_t leftover = ilka_key_leftover(*key_it);
    int is_suffix_burst = burst->suffix_bits != leftover - burst->prefix_bits;

    int kv_added = 0;

    for (size_t i = 0; i < burst->prefixes; ++i) {
        int is_search_prefix = prefix == burst->prefix[i].key;
        kv_added |= is_search_prefix;

        if (add_to_suffix && is_suffix_burst && is_search_prefix) {
            burst->prefix[i].val = add_burst(
                    r, burst->suffix_bits, 0, 0,
                    burst->suffix[i].kvs, burst->suffix[i].size,
                    *key_it, value);
            continue;
        }

        if (!is_value_of_prefix && is_search_prefix) {
            struct trie_kv kv = {
                .key = ilka_key_pop(key_it, leftover - burst->prefix_bits),
                .val = value,
                .state = trie_kvs_state_terminal
            };
            trie_kvs_add(burst->suffix[i].kvs, burst->suffix[i].size, kv);
        }

        int has_value = is_search_prefix && is_value_of_prefix;
        burst->prefix[i].val = write_node(
                r, burst->suffix_bits, has_value, value,
                burst->suffix[i].kvs, burst->suffix[i].size);
    }

    return kv_added;
}

static ilka_ptr_t
add_burst(
        struct ilka_region *r, size_t key_len,
        int has_value, uint64_t node_value,
        struct trie_kv *kvs, size_t kvs_n,
        struct ilka_key_it key_it, uint64_t value)
{
    size_t leftover = ilka_key_leftover(key_it);
    ilka_assert(leftover <= key_len, "leftover bits <%zu> greater then key length <%zu>",
            leftover, key_len);

    struct trie_kvs_burst_info burst;
    trie_kvs_burst(&burst, key_len, kvs, kvs_n);

    int add_to_suffix = leftover > burst.prefix_bits;
    int is_value_of_prefix = leftover == burst.prefix_bits;

    uint64_t prefix = 0;
    if (add_to_suffix || is_value_of_prefix)
        prefix = ilka_key_pop(&key_it, burst.prefix_bits);

    int kv_added = add_burst_suffix(
            r, &burst, &key_it, value, prefix, add_to_suffix, is_value_of_prefix);

    if (!kv_added && (add_to_suffix || is_value_of_prefix)) {
        struct trie_kv kv = { .key = prefix };
        if (is_value_of_prefix) {
            kv.val = value;
            kv.state = trie_kvs_state_terminal;
        }
        else {
            kv.val = new_node(r, 0, 0, key_it, value);
            kv.state = trie_kvs_state_branch;
        }
        trie_kvs_add(burst.prefix, burst.prefixes, kv);
        kv_added = 1;
    }

    if (leftover < burst.prefix_bits) {
        ilka_assert(!kv_added, "kv added before bursting prefix");
        return add_burst(
                r, burst.prefix_bits, has_value, node_value,
                burst.prefix, burst.prefixes, key_it, value);
    }

    return write_node(
            r, burst.prefix_bits, has_value, node_value,
            burst.prefix, burst.prefixes);
}

static ilka_ptr_t
add_to_node(
        struct ilka_region *r,
        ilka_ptr_t node, void *p_node,
        struct trie_kvs_info *info,
        struct ilka_key_it key_it, uint64_t value)
{
    size_t leftover = ilka_key_leftover(key_it);

    if (leftover == info->key_len) {
        struct trie_kv kv = {
            .key = ilka_key_peek(key_it, info->key_len),
            .val = value,
            .state = trie_kvs_state_terminal
        };

        if (trie_kvs_add_inplace(info, kv, p_node))
            return node;
    }

    const size_t kvs_n = info->buckets;
    struct trie_kv kvs[kvs_n];
    trie_kvs_extract(info, kvs, kvs_n, p_node);

    return add_burst(
            r, info->key_len, info->has_value, info->value,
            kvs, kvs_n, key_it, value);
}

static ilka_ptr_t
add_to_child(
        struct ilka_region *r,
        ilka_ptr_t node, void *p_node,
        struct trie_kvs_info *info,
        struct ilka_key_it key_it, uint64_t value)
{
    struct trie_kv child = {
        .key = ilka_key_pop(&key_it, info->key_len),
        .state = trie_kvs_state_empty
    };
    child = trie_kvs_get(info, child.key, p_node);


    int has_value = 0;
    uintptr_t new_child = 0;

    switch (child.state)
    {
    case trie_kvs_state_terminal: has_value = 1; // intentional fall-through.
    case trie_kvs_state_empty:
        new_child = new_node(r, has_value, child.val, key_it, value);
        break;

    case trie_kvs_state_branch:
        new_child = trie_node_add(r, child.val, key_it, value);
        break;

    default:
        ilka_error("unknown child state <%d>", child.state);

    };

    ilka_assert(new_child > 0, "null child ptr");


    if (child.state == trie_kvs_state_branch && child.val == new_child)
        return node;


    child.val = new_child;
    child.state = trie_kvs_state_branch;

    if (trie_kvs_set_inplace(info, child, p_node))
        return node;

    struct trie_kv kvs[info->buckets];
    trie_kvs_extract(info, kvs, info->buckets, p_node);
    trie_kvs_set(kvs, info->buckets, child);

    return write_node(
            r, info->key_len, info->has_value, info->value, kvs, info->buckets);
}

int
trie_node_add(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key_it, uint64_t value)
{
    if (!node) return new_node(r, 0, 0, key_it, value);

    void *p_node = ilka_region_read(r, node);

    struct trie_kvs_info info;
    trie_kvs_decode(&info, p_node);

    if (ilka_key_leftover(key_it) <= info.key_len)
        return add_to_node(r, node, p_node, &info, key_it, value);
    return add_to_child(r, node, p_node, &info, key_it, value);
}
