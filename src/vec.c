/* vec.c
   Rémi Attab (remi.attab@gmail.com), 10 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "vec.h"

#include "utils/bits.h"
#include <stdlib.h>
#include <string.h>


// -----------------------------------------------------------------------------
// vec
// -----------------------------------------------------------------------------

struct ilka_vec
{
    struct ilka_region *r;
    ilka_off_t meta;
};

struct vec_meta
{
    size_t item_len;
    size_t len;
    size_t cap;
    ilka_off_t data;
};

static bool _vec_reserve(struct ilka_vec *v, struct vec_meta *meta, size_t cap);

// -----------------------------------------------------------------------------
// alloc & free
// -----------------------------------------------------------------------------

struct ilka_vec * ilka_vec_alloc(struct ilka_region *r, size_t item_len, size_t cap)
{
    if (!r) {
        ilka_fail("invalid nil value for region");
        return NULL;
    }
    if (!item_len) {
        ilka_fail("invalid nil value for item_len");
        return NULL;
    }

    struct ilka_vec *v = calloc(1, sizeof(struct ilka_vec));
    v->r = r;

    v->meta = ilka_alloc(v->r, sizeof(struct vec_meta));
    if (!v->meta) goto fail_meta;

    struct vec_meta *meta = ilka_write(v->r, v->meta, sizeof(struct vec_meta));
    *meta = (struct vec_meta) { .item_len = item_len };

    if (!_vec_reserve(v, meta, cap)) goto fail_data;

    return v;

  fail_data:
    ilka_free(v->r, v->meta, sizeof(struct vec_meta));
  fail_meta:
    free(v);
    return NULL;
}

bool ilka_vec_free(struct ilka_vec *v)
{
    const struct vec_meta *meta = ilka_read(v->r, v->meta, sizeof(struct vec_meta));

    if (meta->data)
        ilka_free(v->r, meta->data, meta->cap * meta->item_len);
    ilka_free(v->r, v->meta, sizeof(struct vec_meta));
    free(v);

    return true;
}


// -----------------------------------------------------------------------------
// open & close
// -----------------------------------------------------------------------------

struct ilka_vec * ilka_vec_open(struct ilka_region *r, ilka_off_t off)
{
    struct ilka_vec *v = calloc(1, sizeof(struct ilka_vec));
    v->r = r;
    v->meta = off;

    return v;
}

bool ilka_vec_close(struct ilka_vec *v)
{
    free(v);
    return true;
}


// -----------------------------------------------------------------------------
// len & cap
// -----------------------------------------------------------------------------

size_t ilka_vec_len(struct ilka_vec *v)
{
    const struct vec_meta *meta = ilka_read(v->r, v->meta, sizeof(struct vec_meta));
    return meta->len;
}

size_t ilka_vec_cap(struct ilka_vec *v)
{
    const struct vec_meta *meta = ilka_read(v->r, v->meta, sizeof(struct vec_meta));
    return meta->cap;
}

static bool _vec_reserve(struct ilka_vec *v, struct vec_meta *meta, size_t cap)
{
    if (cap <= meta->cap) return true;
    cap = ceil_pow2(cap);

    ilka_off_t data = ilka_alloc(v->r, cap * meta->item_len);
    if (!data) return false;

    if (meta->len) {
        size_t n = meta->len * meta->item_len;
        memcpy(ilka_write(v->r, data, n), ilka_read(v->r, meta->data, n), n);
    }

    meta->cap = cap;
    meta->data = data;
    return true;
}

static bool _vec_resize(struct ilka_vec *v, struct vec_meta *meta, size_t len)
{
    if (!_vec_reserve(v, meta, len)) return false;

    if (!len) {
        ilka_free(v->r, meta->data, meta->cap * meta->item_len);
        meta->cap = 0;
        meta->len = 0;
        meta->data = 0;
        return true;
    }

    if (len <= meta->cap / 4) {
        size_t cap = ceil_pow2(len);
        if (cap == len) cap *= 2;

        ilka_off_t data = ilka_alloc(v->r, cap * meta->item_len);
        if (!data) return false;

        if (meta->len) {
            size_t n = len * meta->item_len;
            memmove(ilka_write(v->r, data, n), ilka_read(v->r, meta->data, n), n);
        }

        meta->cap = cap;
        meta->data = data;
    }

    if (len > meta->len) {
        size_t n = (len - meta->len) * meta->item_len;
        void *p = ilka_write(v->r, meta->data + meta->len * meta->item_len, n);
        memset(p, 0, n);
    }

    meta->len = len;
    return true;
}

bool ilka_vec_reserve(struct ilka_vec *v, size_t cap)
{
    struct vec_meta *meta = ilka_write(v->r, v->meta, sizeof(struct vec_meta));
    return _vec_reserve(v, meta, cap);
}

bool ilka_vec_resize(struct ilka_vec *v, size_t len)
{
    struct vec_meta *meta = ilka_write(v->r, v->meta, sizeof(struct vec_meta));
    return _vec_resize(v, meta, len);
}


// -----------------------------------------------------------------------------
// access
// -----------------------------------------------------------------------------

ilka_off_t ilka_vec_off(struct ilka_vec *v)
{
    return v->meta;
}

static ilka_off_t _vec_get(const struct vec_meta *meta, size_t i, size_t n)
{
    if (ilka_unlikely(i + n > meta->len)) {
        ilka_fail("out-of-bound access: %lu > %lu", i + n, meta->len);
        return 0;
    }

    return meta->data + i * meta->item_len;
}

ilka_off_t ilka_vec_get(struct ilka_vec *v, size_t i)
{
    const struct vec_meta *meta = ilka_read(v->r, v->meta, sizeof(struct vec_meta));
    return _vec_get(meta, i, 1);
}

const void * ilka_vec_read(struct ilka_vec *v, size_t i, size_t n)
{
    const struct vec_meta *meta = ilka_read(v->r, v->meta, sizeof(struct vec_meta));
    return ilka_read(v->r, _vec_get(meta, i, n), n * meta->item_len);
}

void * ilka_vec_write(struct ilka_vec *v, size_t i, size_t n)
{
    const struct vec_meta *meta = ilka_read(v->r, v->meta, sizeof(struct vec_meta));
    return ilka_write(v->r, _vec_get(meta, i, n), n * meta->item_len);
}


// -----------------------------------------------------------------------------
// writes
// -----------------------------------------------------------------------------

bool ilka_vec_append(struct ilka_vec *v, const void *data, size_t n)
{
    struct vec_meta *meta = ilka_write(v->r, v->meta, sizeof(struct vec_meta));

    size_t i = meta->len;
    if (!_vec_resize(v, meta, meta->len + n)) return false;

    if (data) {
        size_t len = n * meta->item_len;
        memcpy(ilka_write(v->r, _vec_get(meta, i, n), len), data, len);
    }

    return true;
}

bool ilka_vec_insert(struct ilka_vec *v, const void *data, size_t i, size_t n)
{
    struct vec_meta *meta = ilka_write(v->r, v->meta, sizeof(struct vec_meta));

    if (i > meta->len) {
        ilka_fail("out-of-bound access: %lu > %lu", i, meta->len);
        return false;
    }

    if (!_vec_resize(v, meta, meta->len + n)) return false;

    uint8_t *p = ilka_write(v->r,
            meta->data + i * meta->item_len,
            (meta->len - i + n) * meta->item_len);

    size_t to_move = (meta->len - i) * meta->item_len;
    memmove(p + n * meta->item_len, p, to_move);

    if (data) memcpy(p, data, n * meta->item_len);

    return true;
}

bool ilka_vec_remove(struct ilka_vec *v, size_t i, size_t n)
{
    struct vec_meta *meta = ilka_write(v->r, v->meta, sizeof(struct vec_meta));

    if (i + n > meta->len) {
        ilka_fail("out-of-bound access: %lu > %lu", i + n, meta->len);
        return false;
    }

    if (n != meta->len) {
        uint8_t *p = ilka_write(v->r,
                meta->data + i * meta->item_len,
                (meta->len - i) * meta->item_len);

        size_t to_move = (meta->len - (i + n)) * meta->item_len;
        memmove(p, p + (n * meta->item_len), to_move);
    }

    // since we're shrinking the region, if resize fails then we can just keep
    // the current cap and update len.
    if (!_vec_resize(v, meta, meta->len - n)) meta->len -= n;

    return true;
}
