/* list.h
   Rémi Attab (remi.attab@gmail.com), 29 Oct 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

#include "ilka.h"

// -----------------------------------------------------------------------------
// list
// -----------------------------------------------------------------------------

struct ilka_list;
struct ilka_packed ilka_list_node { ilka_off_t next; };

struct ilka_list * ilka_list_alloc(
        struct ilka_region *r, ilka_off_t head, size_t off);
struct ilka_list * ilka_list_open(
        struct ilka_region *r, ilka_off_t head, size_t off);
void ilka_list_close(struct ilka_list *l);

ilka_off_t ilka_list_head(struct ilka_list *l);
ilka_off_t ilka_list_next(struct ilka_list *l, const struct ilka_list_node *node);
bool ilka_list_set(struct ilka_list *l, struct ilka_list_node *node, ilka_off_t next);
bool ilka_list_del(struct ilka_list *l, struct ilka_list_node *node);
ilka_off_t ilka_list_clear(struct ilka_list *l);
