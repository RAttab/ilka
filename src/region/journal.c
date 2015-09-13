/* journal.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "region.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

// -----------------------------------------------------------------------------
// config
// -----------------------------------------------------------------------------

static const size_t journal_min_size = 64;
static const char *journal_ext = ".journal";
static const uint64_t journal_magic = 0xB0E9C4032E414824;

// -----------------------------------------------------------------------------
// journal
// -----------------------------------------------------------------------------

struct journal_node
{
    ilka_off_t off;
    size_t len;
};

struct ilka_journal
{
    struct ilka_region *region;
    const char *file;
    char *journal_file;

    struct journal_node *nodes;
    size_t len;
    size_t cap;
};

static char * _journal_file(const char* file)
{
    size_t n = strlen(file) + strlen(journal_ext) + 1;
    char *buf = malloc(n);
    snprintf(buf, n, "%s%s", file, journal_ext);
    return buf;
}

static struct ilka_journal * journal_init(
        struct ilka_region *r, const char* file)
{
    struct ilka_journal *j = calloc(1, sizeof(struct ilka_journal));

    j->region = r;
    j->file = file;
    j->journal_file = _journal_file(file);
    j->cap = journal_min_size;
    j->nodes = calloc(j->cap, sizeof(struct journal_node));

    return j;
}

static void journal_add(struct ilka_journal *j, ilka_off_t off, size_t len)
{
    if (j->len >= j->cap) {
        j->cap *= 2;

        struct journal_node *new = calloc(j->cap, sizeof(struct journal_node));
        for (size_t i = 0; i < j->len; ++i) new[i] = j->nodes[i];

        free(j->nodes);
        j->nodes = new;
    }

    size_t i = 0;
    for (; i < j->len; ++i) {
        if (j->nodes[i].off > off) break;
    }

    j->len++;
    struct journal_node tmp;
    struct journal_node node = { off, len };

    for (; i < j->len; ++i) {
        tmp = j->nodes[i];
        j->nodes[i] = node;
        node = tmp;
    }
}

static size_t _journal_next(
        struct ilka_journal *j, size_t i, struct journal_node *node)
{
    *node = j->nodes[i++];

    for (; i < j->len; i++) {
        ilka_off_t end = node->off + node->len;
        if (j->nodes[i].off > end) break;
        if (end <= j->nodes[i].off + j->nodes[i].len) continue;

        node->len += end - j->nodes[i].off + j->nodes[i].len;
    }

    return i;
}

static void _journal_write(int fd, const void *ptr, size_t len)
{
    ssize_t ret = write(fd, ptr, len);
    if (ret == -1) ilka_error_errno("unable to write to journal");
    if ((size_t) ret != len)
        ilka_error("incomplete write to journal: %lu != %lu", ret, len);
}

static void _journal_write_log(struct ilka_journal *j)
{
    const char *file = j->journal_file;

    int fd = open(file, O_CREAT | O_EXCL | O_APPEND | O_WRONLY, 0764);
    if (fd == -1) ilka_error_errno("unable to create journal: %s", file);

    struct journal_node node;
    size_t i = _journal_next(j, 0, &node);
    for (; i < j->len; i = _journal_next(j, i, &node)) {
        _journal_write(fd, &node, sizeof(node));
        _journal_write(fd, ilka_read(j->region, node.off, node.len), node.len);
    }

    node = (struct journal_node) {0, 0};
    _journal_write(fd, &node, sizeof(node));

    if (fdatasync(fd) == -1) ilka_error_errno("unable to fsync journal: %s", file);

    _journal_write(fd, &journal_magic, sizeof(journal_magic));
    if (fdatasync(fd) == -1) ilka_error_errno("unable to fsync journal: %s", file);

    if (close(fd) == -1) ilka_error_errno("unable to close journal: %s", file);
}

static void _journal_write_region(struct ilka_journal *j)
{
    int fd = open(j->file, O_WRONLY);
    if (fd == -1) ilka_error_errno("unable to open region: %s", j->file);

    struct journal_node node;
    size_t i = _journal_next(j, 0, &node);
    for (; i < j->len; i = _journal_next(j, i, &node)) {
        const void *ptr = ilka_read(j->region, node.off, node.len);

        ssize_t ret = pwrite(fd, ptr, node.len, node.off);
        if (ret == -1) ilka_error_errno("unable to write to region");
        if ((size_t) ret != node.len)
            ilka_error("incomplete write to region: %lu != %lu", ret, node.len);
    }

    if (fdatasync(fd) == -1) ilka_error_errno("unable to fsync region: %s", j->file);
    if (close(fd) == -1) ilka_error_errno("unable to close region: %s", j->file);
}

static void journal_finish(struct ilka_journal *j)
{
    _journal_write_log(j);
    _journal_write_region(j);

    if (unlink(j->journal_file) == -1)
        ilka_error_errno("unable to unlink journal: %s", j->journal_file);

    free(j->nodes);
    free(j->journal_file);
}

static int _journal_check(const char *file)
{
    int fd = open(file, O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT) return -1;
        ilka_error_errno("unable to open journal: %s", file);
    }

    uint64_t magic = 0;

    size_t len = file_len(fd);
    if (len > sizeof(magic)) {
        ssize_t ret = pread(fd, &magic, sizeof(magic), len - sizeof(magic));
        if (ret == -1) ilka_error_errno("unable to read from journal");
        if ((size_t) ret != len)
            ilka_error("incomplete read from journal: %lu != %lu", ret, sizeof(magic));
    }

    if (magic != journal_magic) {
        if (close(fd) == -1) ilka_error_errno("unable to close journal: %s", file);
        if (unlink(file) == -1) ilka_error_errno("unable to unlink journal: %s", file);
        return -1;
    }

    if (lseek(fd, 0, SEEK_SET) == -1) {
        ilka_error_errno("unable to seek journal: %s", file);
    }

    return fd;
}

static void _journal_read(int fd, void *ptr, size_t len)
{
    ssize_t ret = read(fd, ptr, len);
    if (ret == -1) ilka_error_errno("unable to read from journal");
    if ((size_t) ret != len)
        ilka_error("incomplete read from journal: %lu != %lu", ret, len);
}

static void journal_recover(const char *file)
{
    char *journal_file = _journal_file(file);
    int journal_fd = _journal_check(journal_file);
    if (journal_fd == -1) goto done;

    int region_fd = open(file, O_WRONLY);
    if (region_fd == -1) ilka_error_errno("unable to open region: %s", file);

    struct journal_node node = {0, 0};

    size_t cap = ILKA_PAGE_SIZE;
    void *buf = malloc(cap);

    while (true) {
        _journal_read(journal_fd, &node, sizeof(node));
        if (node.off == 0 && node.len == 0) break;

        if (cap < node.len) {
            free(buf);
            cap = node.len;
            buf = malloc(cap);
        }

        _journal_read(journal_fd, buf, node.len);

        ssize_t ret = pwrite(region_fd, buf, node.len, node.off);
        if (ret == -1) ilka_error_errno("unable to write to region: %s", file);
        if ((size_t) ret != node.len)
            ilka_error("incomplete write to region: %lu != %lu", ret, node.len);
    }

    free(buf);

    if (fdatasync(region_fd) == -1) ilka_error_errno("unable to fsync region: %s", file);
    if (close(region_fd) == -1) ilka_error_errno("unable to close region: %s", file);

    if (close(journal_fd) == -1) ilka_error_errno("unable to close journal: %s", journal_file);
    if (unlink(journal_file) == -1) ilka_error_errno("unable to unlink journal: %s", journal_file);

  done:
    free(journal_file);
}
