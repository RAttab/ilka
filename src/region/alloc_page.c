/* alloc_page.c
   Rémi Attab (remi.attab@gmail.com), 05 Dec 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// allocator
// -----------------------------------------------------------------------------

struct alloc_page_node
{
    ilka_off_t next;
    ilka_off_t off;
    size_t len;
};

static ilka_off_t alloc_page_new(
        struct ilka_alloc *alloc,
        ilka_off_t prev_off,
        size_t len)
{
    len = ceil_div(len, ILKA_PAGE_SIZE) * ILKA_PAGE_SIZE;

    const ilka_off_t *prev =
        ilka_read_sys(alloc->region, prev_off, sizeof(ilka_off_t));
    ilka_off_t node_off = *prev;

    while (node_off) {
        const struct alloc_page_node *node =
            ilka_read_sys(alloc->region, node_off, sizeof(struct alloc_page_node));

        if (node->len < len) {
            prev_off = node_off;
            node_off = node->next;
            continue;
        }

        if (node->len == len) {
            ilka_off_t *wprev =
                ilka_write_sys(alloc->region, prev_off, sizeof(ilka_off_t));
            *wprev = node->next;
            return node->off;
        }

        if (node->len > len) {
            struct alloc_page_node *wnode =
                ilka_write_sys(alloc->region, node_off, sizeof(struct alloc_page_node));
            wnode->len -= len;
            return wnode->off + wnode->len;
        }

        ilka_unreachable();
    }

    return ilka_grow(alloc->region, len);
}

static void alloc_page_free(
        struct ilka_alloc *alloc,
        ilka_off_t prev_off,
        ilka_off_t off,
        size_t len)
{
    len = ceil_div(len, ILKA_PAGE_SIZE) * ILKA_PAGE_SIZE;

    const ilka_off_t *prev =
        ilka_read_sys(alloc->region, prev_off, sizeof(ilka_off_t));
    ilka_off_t node_off = *prev;

    while (node_off) {
        const struct alloc_page_node *node =
            ilka_read_sys(alloc->region, node_off, sizeof(struct alloc_page_node));

        if (off + len == node->off && !ilka_is_edge(alloc->region, node->off)) {
            struct alloc_page_node *wnode =
                ilka_write_sys(alloc->region, off, sizeof(struct alloc_page_node));

            *wnode = (struct alloc_page_node) {
                .next = node->next,
                .off = off,
                .len = node->len + len,
            };

            ilka_off_t *wprev =
                ilka_write_sys(alloc->region, prev_off, sizeof(ilka_off_t));
            *wprev = off;

            return;
        }

        if (node->off + node->len == off && !ilka_is_edge(alloc->region, off)) {
            struct alloc_page_node *wnode =
                ilka_write_sys(alloc->region, node_off, sizeof(struct alloc_page_node));

            wnode->len += len;

            if (wnode->next) {
                const struct alloc_page_node *next =
                    ilka_read_sys(alloc->region, wnode->next, sizeof(struct alloc_page_node));
                if (wnode->off + wnode->len == next->off) {
                    if (!ilka_is_edge(alloc->region, next->off)) {
                        wnode->len += next->len;
                        wnode->next = next->next;
                    }
                }
            }

            return;
        }

        if (off > node->off) {
            prev_off = node_off;
            node_off = node->next;
            continue;
        }

        break;
    }

    ilka_off_t *wprev = ilka_write_sys(alloc->region, prev_off, sizeof(ilka_off_t));

    struct alloc_page_node *node =
        ilka_write_sys(alloc->region, off, sizeof(struct alloc_page_node));
    *node = (struct alloc_page_node) {*wprev, off, len};

    *wprev = off;
}
