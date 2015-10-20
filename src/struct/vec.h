/* vec.h
   Rémi Attab (remi.attab@gmail.com), 10 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

#include "ilka.h"


// -----------------------------------------------------------------------------
// vec
// -----------------------------------------------------------------------------

struct ilka_vec;

struct ilka_vec * ilka_vec_alloc(struct ilka_region *r, size_t item_len, size_t cap);
bool ilka_vec_free(struct ilka_vec *v);

struct ilka_vec * ilka_vec_open(struct ilka_region *r, ilka_off_t off);
bool ilka_vec_close(struct ilka_vec *v);

ilka_off_t ilka_vec_off(struct ilka_vec *v);

size_t ilka_vec_len(struct ilka_vec *v);
size_t ilka_vec_cap(struct ilka_vec *v);
bool ilka_vec_resize(struct ilka_vec *v, size_t len);
bool ilka_vec_reserve(struct ilka_vec *v, size_t cap);

ilka_off_t ilka_vec_get(struct ilka_vec *v, size_t i);
void * ilka_vec_write(struct ilka_vec *v, size_t i, size_t n);
const void * ilka_vec_read(struct ilka_vec *v, size_t i, size_t n);

bool ilka_vec_append(struct ilka_vec *v, const void *data, size_t n);
bool ilka_vec_insert(struct ilka_vec *v, const void *data, size_t i, size_t n);
bool ilka_vec_remove(struct ilka_vec *v, size_t i, size_t n);
