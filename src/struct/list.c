/* list.c
   Rémi Attab (remi.attab@gmail.com), 29 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "list.h"

#include "utils/utils.h"
#include <stdlib.h>


// -----------------------------------------------------------------------------
// config
// -----------------------------------------------------------------------------

static const ilka_off_t list_mark = 1UL << 63;


// -----------------------------------------------------------------------------
// list
// -----------------------------------------------------------------------------

struct ilka_list
{
    struct ilka_region *region;
    ilka_off_t head;
    size_t off;
};


// -----------------------------------------------------------------------------
// basics
// -----------------------------------------------------------------------------

struct ilka_list * ilka_list_alloc(
        struct ilka_region *region, ilka_off_t head_off, size_t off)
{
    struct ilka_list *list = ilka_list_open(region, head_off, off);

    struct ilka_list_node *head =
        ilka_write(region, head_off, sizeof(struct ilka_list_node));
    head->next = 0;

    return list;
}

struct ilka_list * ilka_list_open(
        struct ilka_region *region, ilka_off_t head, size_t off)
{
    struct ilka_list *list = calloc(1, sizeof(struct ilka_list));
    *list = (struct ilka_list) { region, head, off };
    return list;
}

void ilka_list_close(struct ilka_list *list)
{
    free(list);
}


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static const struct ilka_list_node * list_read(
        struct ilka_list *list, ilka_off_t off)
{
    return ilka_read(list->region, off + list->off, sizeof(struct ilka_list_node));
}


// -----------------------------------------------------------------------------
// read
// -----------------------------------------------------------------------------

ilka_off_t ilka_list_head(struct ilka_list *list)
{
    const struct ilka_list_node *head =
        ilka_read(list->region, list->head, sizeof(struct ilka_list_node));
    return ilka_list_next(list, head);
}

ilka_off_t ilka_list_next(
        struct ilka_list *list, const struct ilka_list_node *node)
{
    ilka_off_t off = ilka_atomic_load(&node->next, morder_relaxed);

    while (off) {
        node = list_read(list, off);

        ilka_off_t next = ilka_atomic_load(&node->next, morder_relaxed);
        if (!(next & list_mark)) return off;

        off = next;
    }

    return 0;
}


// -----------------------------------------------------------------------------
// write
// -----------------------------------------------------------------------------

bool ilka_list_set(struct ilka_list *list, struct ilka_list_node *node, ilka_off_t next)
{
    (void) list;
    ilka_assert(!(next & list_mark), "invalid offset: %p", (void *) next);

    return ilka_atomic_cmp_xchg(&node->next, 0, next, morder_release);
}

bool ilka_list_del(struct ilka_list *list, struct ilka_list_node *node)
{
    ilka_off_t new_next;
    ilka_off_t old_next = node->next;
    do {
        if (old_next & list_mark) return false;
        new_next = old_next | list_mark;

        // morder_release: linearilization point where the node is removed from
        // the list.
    } while (!ilka_atomic_cmp_xchg(&node->next, &old_next, new_next, morder_release));

  restart: (void) 0;

    ilka_off_t prev_off = list->head;
    const struct ilka_list_node *prev =
        ilka_read(list->region, prev_off, sizeof(struct ilka_list_node));

    while (true) {
        ilka_off_t cur_off = ilka_atomic_load(&prev->next, morder_relaxed);
        if (!cur_off) return false;

        const struct ilka_list_node *cur = list_read(list, cur_off);

        ilka_off_t next = ilka_atomic_load(&cur->next, morder_relaxed);
        if (!(next & list_mark)) {
            prev_off = cur_off + list->off;
            prev = cur;
            continue;
        }

        struct ilka_list_node *wprev =
            ilka_write(list->region, prev_off, sizeof(struct ilka_list_node));

        ilka_off_t clean_next = next & ~list_mark;

        // morder_relaxed: we're just cleaning up at this point and ordering
        // doesn't matter much.
        if (ilka_atomic_cmp_xchg(&wprev->next, &cur_off, clean_next, morder_relaxed)) {
            // Note that we bail after removing only a single value because it
            // doesn't really matter if we removed our node or node so long as
            // we remove a node. This works out because assuming that there's n
            // marked nodes in the list then there are n threads currently
            // removing nodes and so our node will eventually be removed by one
            // of the n threads.
            //
            // So what happens if the user frees the node after we return? Well
            // the interface stipulates that this should be done through
            // ilka_defer_free and since all the other del ops are taking place
            // in an ilka epoch then the frees will all be defered until all the
            // currently ongoing del ops have terminated. Works out all neat and
            // tidy like.
            return true;
        }

        // our prev pointer is no longer valid so we have to restart from
        // scratch.
        goto restart;
    }
}

ilka_off_t ilka_list_clear(struct ilka_list *list)
{
    struct ilka_list_node *head =
        ilka_write(list->region, list->head, sizeof(struct ilka_list_node));
    return ilka_atomic_xchg(&head->next, 0, morder_relaxed);
}
