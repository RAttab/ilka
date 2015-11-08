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
    if (!head_off) {
        ilka_fail("invalid nil head offset");
        return NULL;
    }

    struct ilka_list *list = ilka_list_open(region, head_off, off);
    if (!list) return NULL;

    struct ilka_list_node *head =
        ilka_write(region, head_off, sizeof(struct ilka_list_node));
    head->next = 0;

    return list;
}

struct ilka_list * ilka_list_open(
        struct ilka_region *region, ilka_off_t head, size_t off)
{
    if (!head) {
        ilka_fail("invalid nil head offset");
        return NULL;
    }

    struct ilka_list *list = calloc(1, sizeof(struct ilka_list));
    if (!list) {
        ilka_fail("out-of-memory for list struct");
        return NULL;
    }

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

static inline bool check_node(const struct ilka_list_node *node, const char *name)
{
    if (!node) {
        ilka_fail("invalid nil node for '%s'", name);
        return false;
    }

    return true;
}

static inline bool check_off(ilka_off_t off, const char *name)
{
    if (!off) {
        ilka_fail("invalid nil offset for '%s'", name);
        return false;
    }

    if (off & list_mark) {
        ilka_fail("invalid offset for '%s': %p", name, (void *) off);
        return false;
    }

    return true;
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
    if (!check_node(node, "node")) return ILKA_LIST_ERROR;

    ilka_off_t off = ilka_atomic_load(&node->next, morder_relaxed);
    off &= ~list_mark;

    while (off) {
        node = list_read(list, off);

        ilka_off_t next = ilka_atomic_load(&node->next, morder_relaxed);
        if (!(next & list_mark)) return off;

        ilka_assert(off != (next & ~list_mark),
                "node self-reference: off=%p, next=%p", (void *) off, (void *) next);

        off = next & ~list_mark;
    }

    return 0;
}


// -----------------------------------------------------------------------------
// write
// -----------------------------------------------------------------------------

int ilka_list_insert(
        struct ilka_list *list, struct ilka_list_node *prev, ilka_off_t node_off)
{
    if (!check_node(prev, "prev")) return -1;
    if (!check_off(node_off, "node_off")) return -1;

    struct ilka_list_node *node =
        ilka_write(list->region, node_off + list->off, sizeof(struct ilka_list_node));

    ilka_off_t next = ilka_atomic_load(&prev->next, morder_relaxed);
    do {
        if (next & list_mark) return 0;

        node->next = next;
    } while (!ilka_atomic_cmp_xchg(&prev->next, &next, node_off, morder_release));

    return 1;
}

int ilka_list_set(
        struct ilka_list *list, struct ilka_list_node *node, ilka_off_t next)
{
    (void) list;

    if (!check_node(node, "node")) return -1;
    if (!check_off(next, "next")) return -1;

    ilka_off_t old = 0;
    return ilka_atomic_cmp_xchg(&node->next, &old, next, morder_release);
}

static int list_clean(
        struct ilka_list *list,
        const struct ilka_list_node *target,
        ilka_off_t prev_off,
        ilka_off_t node_off)
{
    const struct ilka_list_node *node = list_read(list, node_off);
    ilka_off_t next = ilka_atomic_load(&node->next, morder_relaxed);

    while (true) {

        ilka_off_t clean_next = next & ~list_mark;
        ilka_assert(node_off != clean_next,
                "node self-reference: off=%p, next=%p",
                (void *) node_off, (void *) next);

        if (node == target) {
            struct ilka_list_node *prev =
                ilka_write(list->region, prev_off, sizeof(struct ilka_list_node));

            if (ilka_atomic_cmp_xchg(&prev->next, &node_off, clean_next, morder_relaxed))
                return 0;
            return 1;
        }

        if (!clean_next) {
            ilka_fail("unable to find node '%p' in list", (void *) target);
            return -1;
        }

        ilka_off_t new_prev = next & list_mark ? prev_off : node_off + list->off;
        int ret = list_clean(list, target, new_prev, clean_next);
        if (ret <= 0) return ret;

        next = ilka_atomic_load(&node->next, morder_relaxed);
        if (next & list_mark) return 1;
    }

    ilka_unreachable();
}

int ilka_list_del(struct ilka_list *list, struct ilka_list_node *node)
{
    if (!check_node(node, "node")) return -1;

    const struct ilka_list_node *head =
        ilka_read(list->region, list->head, sizeof(struct ilka_list_node));

    // morder_acquire: we have to the current head before we mark in case it
    // gets cleared mid-op.
    ilka_off_t first = ilka_atomic_load(&head->next, morder_acquire);

    ilka_off_t new_next;
    ilka_off_t old_next = ilka_atomic_load(&node->next, morder_relaxed);
    do {
        if (old_next & list_mark) return 1;
        new_next = old_next | list_mark;

        // morder_release: linearilization point where the node is removed from
        // the list.
    } while (!ilka_atomic_cmp_xchg(&node->next, &old_next, new_next, morder_release));

    int ret;
    while ((ret = list_clean(list, node, list->head, first)) > 0);
    return ret;
}

ilka_off_t ilka_list_clear(struct ilka_list *list)
{
    struct ilka_list_node *head =
        ilka_write(list->region, list->head, sizeof(struct ilka_list_node));
    return ilka_atomic_xchg(&head->next, 0, morder_relaxed);
}
