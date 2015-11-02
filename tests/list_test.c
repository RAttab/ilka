/* list_test.c
   Rémi Attab (remi.attab@gmail.com), 01 Nov 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "check.h"
#include "struct/list.h"


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

struct list_test
{
    struct ilka_region *r;
    struct ilka_list *list;
    struct ilka_list_node *root;

    size_t runs;
};

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

START_TEST(basic_test_st)
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
// insert
// -----------------------------------------------------------------------------

void run_insert_test(size_t id, void *data)
{
    struct list_test *t = data;

    for (size_t i = 0; i < t->runs; ++i) {
        struct node *node = node_alloc(t->r, id << 32 | i);
        ilka_list_insert(t->list, t->root, node->off);
    }

    size_t exp = t->runs;
    ilka_off_t off = ilka_list_head(t->list);
    while (off) {
        const struct node *node = ilka_read(t->r, off, sizeof(struct node));

        if ((node->value >> 32) == id) {
            size_t i = node->value & ((1UL << 32) - 1);
            ilka_assert(i == --exp, "id=%lu, node=%p, v=%p",
                    id, (void *) node->off, (void *) node->value);
        }

        off = ilka_list_next(t->list, &node->next);
    }

    ilka_assert(exp == 0, "id=%lu, exp=%lu", id, exp);
}

START_TEST(insert_test_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    ilka_off_t root_off = ilka_alloc(r, sizeof(struct ilka_list_node));
    struct ilka_list_node *root =
        ilka_write(r, root_off, sizeof(struct ilka_list_node));
    struct ilka_list *list =
        ilka_list_alloc(r, root_off, offsetof(struct node, next));

    struct list_test data = {
        .r = r,
        .list = list,
        .root = root,
        .runs = 1000000,
    };
    ilka_run_threads(run_insert_test, &data);
}
END_TEST

// -----------------------------------------------------------------------------
// insert
// -----------------------------------------------------------------------------

void run_del_test(size_t id, void *data)
{
    struct list_test *t = data;

    enum { n = 10 };
    struct node *nodes[n] = { 0 };

    for (size_t run = 0; run < t->runs; ++run) {
        size_t epoch = ilka_enter(t->r);

        for (size_t i = 0; i < n; ++i) {
            nodes[i] = node_alloc(t->r, i);
            ilka_list_insert(t->list, t->root, nodes[i]->off);
        }

        for (size_t i = 0; i < n; ++i) {
            int ret = ilka_list_del(t->list, &nodes[i]->next);
            ilka_assert(ret == 1, "id=%lu, node=%p, ret=%d",
                    id, (void *) nodes[i]->off, ret);

            ilka_defer_free(t->r, nodes[i]->off, sizeof(struct node));
        }

        ilka_exit(t->r, epoch);
    }
}

START_TEST(del_test_mt)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);

    ilka_off_t root_off = ilka_alloc(r, sizeof(struct ilka_list_node));
    struct ilka_list_node *root =
        ilka_write(r, root_off, sizeof(struct ilka_list_node));
    struct ilka_list *list =
        ilka_list_alloc(r, root_off, offsetof(struct node, next));

    struct list_test data = {
        .r = r,
        .list = list,
        .root = root,
        .runs = 10,
    };
    ilka_run_threads(run_del_test, &data);

    ck_assert_int_eq(ilka_list_head(list), 0);
}
END_TEST


// -----------------------------------------------------------------------------
// setup
// -----------------------------------------------------------------------------

void make_suite(Suite *s)
{
    ilka_tc(s, basic_test_st, true);
    ilka_tc(s, insert_test_mt, true);
    ilka_tc(s, del_test_mt, true);
}

int main(void)
{
    return ilka_tests("list_test", &make_suite);
}
