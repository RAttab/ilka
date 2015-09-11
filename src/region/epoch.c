/* epoch.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply

   Note: there can be no stale defers on startup because persistance is a
   stop-to-world event which requires all epochs to be vacated and defers are
   executed when the last thread leaves an epoch.
*/

#include "region.h"

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

struct epoch_node
{
    size_t len;
    ilka_off_t off;
    struct epoch_node *next;
};

struct ilka_epoch
{
    struct ilka_region *region;

    union epoch_state state;
    struct epoch_node *defers[2];
};

struct ilka_epoch * epoch_init(struct ilka_region *r)
{
    struct ilka_epoch *e = calloc(1, sizeof(struct ilka_epoch));
    e->region = r;
    return e;
}

void epoch_close(struct ilka_epoch *e)
{
    for (size_t i = 0; i < 2; ++i) {

        struct epoch_node *head = e->defers[i];
        while (head) {
            struct epoch_node *next = head->next;
            free(head);
            head = next;
        }
    }

    free(e);
}

void epoch_defer_free(struct ilka_epoch *e, ilka_off_t off, size_t len)
{
    struct epoch_node *node = malloc(sizeof(struct epoch_node));
    *node = (struct epoch_node) {off, len, 0};

    union epoch_state state;
    state.packed = ilka_atomic_load(&e->state.packed, memory_order_relaxed);

    size_t i = state.unpacked.epoch & 0x1;
    struct epoch_node *old = e->defers[i];
    do {
        node->next = old;
    } while (!ilka_atomic_cmp_xchg(&e->defers[i], &old, node, memory_order_relaxed));
}

ilka_epoch_t epoch_enter(struct ilka_epoch *e)
{
    ilka_epoch_t epoch = 0;
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

    } while (!ilka_atomic_cmp_xchg(&e->state.packed, &old.packed, new.packed, memory_order_acquire));

    return epoch;
}

void epoch_exit(struct ilka_epoch *e, ilka_epoch_t epoch)
{
    struct epoch_node *defers = NULL;

    union epoch_state old, new;
    old.packed = ilka_atomic_load(&e->state.packed, memory_order_relaxed);

    do {
        new.packed = old.packed;
        uint16_t count = --new.unpacked.epochs[epoch & 0x1];

        if (!defers && !count && epoch != new.unpacked.epoch)
            defers = ilka_atomic_xchg(&e->defers[epoch & 0x1], NULL, memory_order_relaxed);

    } while (!ilka_atomic_cmp_xchg(&e->state.packed, &old.packed, new.packed, memory_order_release));

    while (defers) {
        ilka_free(e->region, defers->off, defers->len);

        struct epoch_node *next = defers->next;
        free(defers);
        defers = next;
    }
}

void epoch_world_stop(struct ilka_epoch *e)
{
    union epoch_state old, new;
    old.packed = ilka_atomic_load(&e->state.packed, memory_order_relaxed);

    do {
        new.packed = old.packed;
        new.unpacked.lock++;
    } while (!ilka_atomic_cmp_xchg(&e->state.packed, &old.packed, new.packed, memory_order_relaxed));

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
    } while (!ilka_atomic_cmp_xchg(&e->state.packed, &old.packed, new.packed, memory_order_release));
}
