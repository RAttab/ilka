/* node.c
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   trie node implementation.
*/

#include "node.h"
#include "kv.h"


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
        struct ilka_region *r,
        const struct trie_kv *kvs, size_t n)
{
    struct trie_kvs_burst_info burst;
    trie_kvs_burst(&burst, kvs, n);

    for (size_t i = 0; i < burst.size; ++i) {
        if (burst.prefix[i].is_terminal) continue;

        if (!burst[i].size)
            ilka_error("empty non-terminal bursted bucket");

        ilka_ptr dest = burst_write(burst[i].kvs, burst[i].size);
        burst.prefix[i].v = dest;
        ilka_region_unpin(dest);
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
    else dest = burst(r, kvs, n);

    return dest;
}

static ilka_ptr add_kv_inplace(
        struct ilka_region *r, const struct trie_kvs_info *info,
        struct trie_kv kv, ilka_ptr node)
{
    {
        void *node_p = ilka_region_pin_write(r, node);
        trie_kvs_add_inplace(info, kv, node_p, ILKA_CACHE_LINE);
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
