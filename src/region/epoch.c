/* epoch.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply

   Note: there can be no stale defers on startup because persistance is a
   stop-to-world event which requires all epochs to be vacated and defers are
   executed when the last thread leaves an epoch.
*/

#include "epoch.h"


// -----------------------------------------------------------------------------
// epoch
// -----------------------------------------------------------------------------

union epoch_state
{
    uint64_t packed;
    struct {
        uint16_t lock;
        ilka_epoch_t epoch;
        uint16_t epochs[2];
    } unpacked;
};

struct ilka_epoch
{
    struct ilka_region *region;

    ilka_off_t off;
    union epoch_state state;
};

struct epoch_node
{
    size_t len;
    ilka_off_t off;
    ilka_off_t next;
}

struct epoch_region
{
    ilka_off_t epochs[2];
};

struct ilka_epoch * epoch_init(struct ilka_region *r, ilka_off_t *off)
{
    struct ilka_epoch *e = calloc(1, struct(ilka_epoch));

    if (!*off)
        *off = ilka_alloc(r, sizeof(struct epoch_region));

    e->region = r;
    e->off = *off;

    return e;
}

void epoch_defer_free(struct ilka_epoch *e, ilka_off_t off, size_t len)
{
    ilka_off_t node_off = ilka_alloc(sizeof(struct epoch_node));
    struct epoch_node *node =
        ilka_write(e->region, node_off, sizeof(struct epoch_node));
    *node = {off, len, 0};

    struct epoch_region *region =
        ilka_write(e->region, e->off, sizeof(struct epoch_region));

    union epoch_state state;
    state.packed = ilka_atomic_load(&e->state.packed, memory_order_relaxed);

    size_t i = state.epoch & 0x1;
    ilka_off_t old = region->epochs[i];
    do {
        node->next = old;
    } while (!ilka_atomic_cmp_xchg(&region->epochs[i], old, node_off, memory_order_relaxed));
}

ilka_epoch_t epoch_enter(struct ilka_epoch *e)
{
    ilka_epoch_t epoch;
    union epoch_state old, new;

  restart:
    old.packed = ilka_atomic_load(&e->state.packed, memory_order_relaxed);

    do {
        if (old.unpacked.lock) goto restart;

        new.packed = old.packed;

        ilka_epoch_t epoch = new.unpacked.epoch;
        if (!new.unpacked.epochs[(epoch & 0x1) ^ 0x1])
            epoch = ++new.unpacked.epoch;

        new.unpacked.epochs[epoch & 0x1]++;

    } while (!ilka_atomic_cmp_xchg(&e->state.packed, old.packed, new.packed, memory_order_acquire));

    return epoch;
}

void epoch_exit(struct ilka_epoch *e, ilka_epoch_t epoch)
{
    ilka_off_t defers = 0;

    union epoch_state old, new;
    old.packed = ilka_atomic_load(&e->state.packed, memory_order_relaxed);

    do {
        new.packed = old.packed;
        uint16_t count = --new.unpacked.epochs[epoch & 0x1];

        if (!defers && !count && epoch != new.unpacked.epoch) {
            struct epoch_region *region =
                ilka_write(e->region, e->off, sizeof(struct epoch_region));

            defers = ilka_atomic_xchg(&region->epochs[epoch & 0x1], NULL, memory_order_relaxed);
        }

    } while (!ilka_atomic_cmp_xchg(&e->state.packed, old.packed, new.packed, memory_order_release));

    while (defers) {
        struct epoch_node node =
            *ilka_read(e->region, defers, sizeof(struct epoch_node));

        ilka_free(node.off, node.len);
        ilka_free(defers);
        defers = node.next;
    }
}

void epoch_world_stop(struct ilka_epoch *e)
{
    union epoch_state old, new;
    old.packed = ilka_atomic_load(&e->state.packed, memory_order_relaxed);

    do {
        new.packed = old.packed;
        new.unpacked.lock++;
    } while (!ilka_atomic_cmp_xchg(&e->state.packed, old.packed, new.packed, memory_order_relaxed));

    while (old.unpacked.epochs[0] || old.unpacked.epochs[1])
        old.packed = ilka_atomic_load(&e->state.packed, memory_order_relaxed);

    ilka_atomic_fence(memory_order_acquire);
}

void epoch_world_resume(struct ilka_epoch *e)
{
    union epoch_state old, new;
    old.packed = ilka_atomic_load(&e->state.packed, memory_order_relaxed);

    do {
        new.packed = old.packed;
        new.unpacked.lock--;
    } while (!ilka_atomic_cmp_xchg(&e->state.packed, old.packed, new.packed, memory_order_release));
}
