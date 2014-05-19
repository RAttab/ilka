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
    while (true) {
        void *p_node = ilka_region_read(r, node);

        struct trie_kvs_info info;
        trie_kvs_decode(&info, p_node);

        if (ilka_key_end(key)) {
            if (!info.value_bits) return 0;
            *value = info.value;
            return 1;
        }
        if (ilka_key_leftover(key) < info->key_len) return 0;

        uint64_t key = ilka_key_pop(&key_it, info->key_len);
        struct trie_kv kv = trie_kvs_get(&info, key, p_node);

        if (kv.state == trie_kvs_state_branch) {
            node = kv.val;
            continue;
        }

        if (kv.state == trie_kvs_state_value) {
            if (!ilka_key_end(key)) return 0;
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
        restrict uint64_t *lb, restrict uint64_t *ub)
{
    size_t leftover = ilka_key_leftover(key);
    size_t n = key_len > leftover ? leftover : key_len;
    size_t shift = key_len - n;

    *lb = ilka_key_pop(key_it, n);
    *lb <<= shift;
    *ub = *lb | ((1ULL << shift) - 1);
}

int
trie_node_lb(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key_it, struct ilka_key_it lb)
{
    while (true) {
        void *p_node = ilka_region.read(r, node);

        struct trie_kvs_info info;
        trie_kvs_decode(&info, p_node);

        if (info->value_bits && ilka_key_end(key)) return 1;

        uint64_t key_lb, key_ub;
        key_bounds(&key_it, info->key_len, &key_lb, &key_ub);
        struct trie_kv kv = ilka_kvs_ub(&info, key_lb, p_node);

        if (kv.state == trie_kvs_state_empty) return 0;
        if (kv.key > key_ub) return 0;

        ilka_key_push(&lb, kv.key, info->key_len);

        if (kv.state == trie_kvs_state_value) return 1;
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
        struct ilka_key_it key, struct ilka_key_it ub)
{
    while (true) {
        void *p_node = ilka_region.read(r, node);

        struct trie_kvs_info info;
        trie_kvs_decode(&info, p_node);

        uint64_t key_lb, key_ub;
        key_bounds(&key_it, info->key_len, &key_lb, &key_ub);
        struct trie_kv kv = ilka_kvs_ub(&info, key_ub, p_node);

        if (kv.state == trie_kvs_state_empty)
            return info->value_bits && ilka_key_end(key);
        if (kv.key > key_ub) return 0;

        ilka_key_push(&lb, kv.key, info->key_len);

        if (kv.state == trie_kvs_state_value) return 1;
        if (kv.state == trie_kvs_state_branch) {
            node = kv.val;
            continue;
        }

        ilka_error("expected <%d> got <%d>", trie_kvs_state_branch, kv.state);
    }
}

int
trie_node_next(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, struct ilka_key_it prev)
{

}

int
trie_node_prev(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, struct ilka_key_it next)
{

}

int
trie_node_add(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, uint64_t value)
{

}

int
trie_node_cmp_xchg(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, uint64_t *expected, uint64_t desired)
{

}

int
trie_node_cmp_rmv(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, uint64_t *expected)
{

}



// -----------------------------------------------------------------------------
// add kv
// -----------------------------------------------------------------------------


static ilka_ptr write(
        struct ilka_region *r,
        const struct trie_kvs_info *info,
        const struct trie_kv *kvs, size_t n)
{
    ilka_ptr dest = ilka_region_alloc(r, trie_kvs_size(&info));

    {
        void * dest_p = ilka_region_pin_write(r, dest);
        trie_kvs_encode(&info, dest_p, kvs, n);
        ilka_region_unpin_write(r, dest);
    }

    return dest;
}

static ilka_ptr burst_write(
        struct ilka_region *r,
        const struct trie_kv *kvs, size_t n)
{
    struct trie_kvs_info info;
    trie_kvs_info(&info, kvs, n);
    return write(r, &info, kvs, n);
}

static ilka_ptr burst(
        struct ilka_region *r, size_t key_len,
        const struct trie_kv *kvs, size_t n)
{
    struct trie_kvs_burst_info burst;
    trie_kvs_burst(&burst, key_len, kvs, n);

    for (size_t i = 0; i < burst.prefixes; ++i) {
        burst.prefix[i].val =
            burst_write(burst.suffix[i].kvs, burst.suffix[i].size);
    }

    return burst_write(r, burst.prefix, burst.size):
}

static ilka_ptr add_kv(
        struct ilka_region *r, const struct trie_kvs_info *info,
        struct trie_kv kv, ilka_ptr node)
{
    ilka_ptr dest_ptr = 0;

    size_t n = info->num_buckets + 1;
    struct trie_kv *kvs = alloca(n * sizeof(struct trie_kv));

    trie_kvs_extract(info, kvs, n);
    trie_kvs_add(kvs, n, kv);

    struct trie_kvs_info compressed;

    if (trie_kvs_info(&compressed, kvs, n))
         dest = write(r, &compressed, kvs, n);
    else dest = burst(r, info->key_len, kvs, n);

    return dest;
}

static ilka_ptr add_kv_inplace(
        struct ilka_region *r, const struct trie_kvs_info *info,
        struct trie_kv kv, ilka_ptr node)
{
    {
        void *node_p = ilka_region_pin_write(r, node);
        trie_kvs_lock(r, node_p);

        trie_kvs_add_inplace(info, kv, node_p, ILKA_CACHE_LINE);

        trie_kvs_unlock(r, node_p);
        ilka_region_unpin_write(r, node_p);
    }

    return node;
}

static void put(struct ilka_region *r, struct trie_key_it key)
{
    ilka_region_pin_read(r);

    ilka_ptr root = ...;
    void * data = ;

    struct trie_kvs_info info;
    trie_kvs_decode(&info, ilka_region_read(r, root), ILKA_CACHE_LINE);

    ilka_ptr node = trie_kvs_can_add_inplace(info, kv) ?
        add_kv_inplace(r, &info, kv) : add_kv(r, &info, kv);

    ilka_region_unpin_read(r);
}
