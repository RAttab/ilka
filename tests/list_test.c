/* list_test.c
   Rémi Attab (remi.attab@gmail.com), 01 Nov 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "struct/list.h"


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

struct node
{
    uint64_t value;
    struct ilka_list_node next;
    uint64_t off;
};

struct node * node_alloc(struct ilka_region *r, uint64_t value)
{
    ilka_off_t off = ilka_alloc(r, sizeof(struct node));
    struct node *node = ilka_write(r, off, sizeof(struct node));
    *node = (struct node) { value, { 0 }, off };
    return node;
}

void _check_list(
        struct ilka_region *r,
        struct ilka_list *list,
        const struct ilka_list_node *node,
        const uint64_t *exp, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        ilka_off_t next = ilka_list_next(list, node);
        ilka_assert(next, "i=%lu, next=%p", i, (void *) next);

        const struct node *data = ilka_read(r, next, sizeof(struct node));
        ck_assert_int_eq(exp[i], data->value);

        node = &data->next;
    }

    ck_assert(!ilka_list_next(list, node));
}

#define check_list(r, l, node, ...)                                     \
    do {                                                                \
        uint64_t exp[] = { __VA_ARGS__ };                               \
        _check_list(r, l, node, exp, sizeof(exp) / sizeof(uint64_t));   \
    } while (false)

void node_free(struct ilka_region *r, struct node *node)
{
    ilka_free(r, node->off, sizeof(struct node));
}

// -----------------------------------------------------------------------------
// basics
// -----------------------------------------------------------------------------

START_TEST(basic_seq_test)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);
    size_t epoch = ilka_enter(r);

    ilka_off_t root_off = ilka_alloc(r, sizeof(struct ilka_list_node));
    const struct ilka_list_node *root =
        ilka_read(r, root_off, sizeof(struct ilka_list_node));
    struct ilka_list_node *wroot =
        ilka_write(r, root_off, sizeof(struct ilka_list_node));

    struct ilka_list *l0 = ilka_list_alloc(r, root_off, offsetof(struct node, next));
    struct ilka_list *l1 = ilka_list_open(r, root_off, offsetof(struct node, next));

    ck_assert_int_eq(ilka_list_head(l0), 0);
    ck_assert_int_eq(ilka_list_head(l1), 0);
    ck_assert_int_eq(ilka_list_next(l0, root), 0);
    ck_assert_int_eq(ilka_list_next(l1, root), 0);

    struct node *n0 = node_alloc(r, 10);
    ilka_list_set(l0, wroot, n0->off);
    ck_assert_int_eq(ilka_list_head(l0), n0->off);
    ck_assert_int_eq(ilka_list_head(l1), n0->off);
    ck_assert_int_eq(ilka_list_next(l0, root), n0->off);
    ck_assert_int_eq(ilka_list_next(l1, root), n0->off);
    check_list(r, l0, root, 10);

    struct node *n1 = node_alloc(r, 20);
    ilka_list_set(l1, &n0->next, n1->off);
    ck_assert_int_eq(ilka_list_next(l0, &n0->next), n1->off);
    ck_assert_int_eq(ilka_list_next(l1, &n0->next), n1->off);
    check_list(r, l0, root, 10, 20);

    struct node *n2 = node_alloc(r, 30);
    ilka_list_insert(l0, &n0->next, n2->off);
    ck_assert_int_eq(ilka_list_next(l0, &n0->next), n2->off);
    ck_assert_int_eq(ilka_list_next(l0, &n2->next), n1->off);
    check_list(r, l0, root, 10, 30, 20);

    ck_assert_int_eq(ilka_list_del(l0, &n2->next), 1);
    ck_assert_int_eq(ilka_list_next(l0, &n0->next), n1->off);
    check_list(r, l0, root, 10, 20);

    ck_assert_int_eq(ilka_list_del(l0, &n2->next), 0);
    check_list(r, l0, root, 10, 20);

    ck_assert_int_eq(ilka_list_del(l0, &n0->next), 1);
    ck_assert_int_eq(ilka_list_next(l0, root), n1->off);
    check_list(r, l0, root, 20);

    ilka_list_clear(l0);
    ck_assert_int_eq(ilka_list_head(l0), 0);
    ck_assert_int_eq(ilka_list_next(l0, root), 0);

    ilka_list_close(l1);
    ilka_list_close(l0);
    ilka_free(r, root_off, sizeof(struct ilka_list_node));

    ilka_exit(r, epoch);
    if (!ilka_close(r)) ilka_abort();
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, basic_seq_test, true);
}

int main(void)
{
    return ilka_tests("list_test", &make_suite);
}
