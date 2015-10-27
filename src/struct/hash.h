/* hash.h
   Rémi Attab (remi.attab@gmail.com), 20 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

#include "ilka.h"


// -----------------------------------------------------------------------------
// hash
// -----------------------------------------------------------------------------

struct ilka_hash;

struct ilka_hash_key
{
    size_t len;
    const void *data;
};

struct ilka_hash * ilka_hash_alloc(struct ilka_region *r);
bool ilka_hash_free(struct ilka_hash *h);

struct ilka_hash * ilka_hash_open(struct ilka_region *r, ilka_off_t off);
bool ilka_hash_close(struct ilka_hash *h);

ilka_off_t ilka_hash_off(struct ilka_hash *h);

size_t ilka_hash_cap(struct ilka_hash *h);
bool ilka_hash_reserve(struct ilka_hash *h, size_t cap);

size_t ilka_hash_len(struct ilka_hash *h);
bool ilka_hash_resize(struct ilka_hash *h, size_t len);

int ilka_hash_has(struct ilka_hash *h, struct ilka_hash_key key);
ilka_off_t ilka_hash_get(struct ilka_hash *h, struct ilka_hash_key key);


struct ilka_hash_ret
{
    int code;
    ilka_off_t off;
};

struct ilka_hash_ret ilka_hash_del(
        struct ilka_hash *h, struct ilka_hash_key key);

struct ilka_hash_ret ilka_hash_put(
        struct ilka_hash *h, struct ilka_hash_key key, ilka_off_t value);

struct ilka_hash_ret ilka_hash_xchg(
        struct ilka_hash *h, struct ilka_hash_key key, ilka_off_t value);

struct ilka_hash_ret ilka_hash_cmp_xchg(
        struct ilka_hash *h,
        struct ilka_hash_key key,
        ilka_off_t expected,
        ilka_off_t value);
