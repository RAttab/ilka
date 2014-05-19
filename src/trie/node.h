/* node.h
   Rémi Attab (remi.attab@gmail.com), 04 May 2014
   FreeBSD-style copyright and disclaimer apply

   trie node.
*/

#pragma once

#include "key.h"
#include "region/region.h"

// -----------------------------------------------------------------------------
// node
// -----------------------------------------------------------------------------

int trie_node_get(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, uint64_t *value);

int trie_node_lb(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, struct ilka_key_it lb, uint64_t *value);

int trie_node_ub(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, struct ilka_key_it ub, uint64_t *value);

int trie_node_next(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, struct ilka_key_it next, uint64_t *value);

int trie_node_prev(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, struct ilka_key_it prev, uint64_t *value);

int trie_node_add(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, uint64_t value);

int trie_node_cmp_xchg(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, uint64_t *expected, uint64_t desired);

int trie_node_cmp_rmv(
        struct ilka_region *r, ilka_ptr_t node,
        struct ilka_key_it key, uint64_t *expected);
