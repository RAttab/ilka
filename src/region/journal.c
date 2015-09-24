/* journal.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

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
    if (!buf) {
        ilka_fail("out-of-memory to construct journal file: %lu", n);
        return NULL;
    }

    snprintf(buf, n, "%s%s", file, journal_ext);
    return buf;
}

static bool journal_init(
        struct ilka_journal *j, struct ilka_region *r, const char* file)
{
    memset(j, 0, sizeof(struct ilka_journal));

    j->region = r;
    j->file = file;
    j->cap = journal_min_size;

    j->journal_file = _journal_file(file);
    if (!j->journal_file) goto fail_journal;

    j->nodes = calloc(j->cap, sizeof(struct journal_node));
    if (!j->nodes) {
        ilka_fail("out-of-memory for journal nodes: %lu",
                j->cap * sizeof(struct journal_node));
        goto fail_nodes;
    }

    return true;

  fail_nodes:
    free(j->journal_file);
  fail_journal:
    free(j);
    return false;
}

static bool journal_add(struct ilka_journal *j, ilka_off_t off, size_t len)
{
    if (j->len >= j->cap) {
        j->cap *= 2;

        struct journal_node *new = calloc(j->cap, sizeof(struct journal_node));
        if (!new) {
            ilka_fail("out-of-memory for journal nodes: %lu",
                    j->cap * sizeof(struct journal_node));
            return false;
        }

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

    return true;
}

static size_t _journal_next(
        struct ilka_journal *j, size_t i, struct journal_node *node)
{
    *node = j->nodes[i++];

    for (; i < j->len; i++) {
        ilka_off_t end = node->off + node->len;
        if (j->nodes[i].off > end) break;
        if (j->nodes[i].off + j->nodes[i].len <= end) continue;

        node->len += end - j->nodes[i].off + j->nodes[i].len;
    }

    return i;
}

static bool _journal_write(int fd, const void *ptr, size_t len)
{
    ssize_t ret = write(fd, ptr, len);
    if (ret == -1) {
        ilka_fail_errno("unable to write to journal: %p, %p", ptr, (void *) len);
        return false;
    }

    if ((size_t) ret != len) {
        ilka_fail("incomplete write to journal: %lu != %lu", ret, len);
        return false;
    }

    return true;
}

static bool _journal_write_log(struct ilka_journal *j)
{
    const char *file = j->journal_file;

    int fd = open(file, O_CREAT | O_EXCL | O_APPEND | O_WRONLY, 0764);
    if (fd == -1) {
        ilka_fail_errno("unable to create journal: %s", file);
        return false;
    }

    struct journal_node node;
    size_t i = _journal_next(j, 0, &node);
    for (; i < j->len; i = _journal_next(j, i, &node)) {
        if (!_journal_write(fd, &node, sizeof(node))) goto fail;
        if (!_journal_write(fd, ilka_read(j->region, node.off, node.len), node.len))
            goto fail;
    }

    node = (struct journal_node) {0, 0};
    _journal_write(fd, &node, sizeof(node));

    if (fdatasync(fd) == -1) {
        ilka_fail_errno("unable to fsync journal: %s", file);
        goto fail;
    }

    if (!_journal_write(fd, &journal_magic, sizeof(journal_magic)))
        goto fail;

    if (fdatasync(fd) == -1) {
        ilka_fail_errno("unable to fsync journal: %s", file);
        goto fail;
    }

    if (close(fd) == -1) {
        ilka_fail_errno("unable to close journal: %s", file);
        return false;
    }

    return true;

  fail:
    close(fd);
    unlink(file);
    return false;
}

static bool _journal_write_region(struct ilka_journal *j)
{
    int fd = open(j->file, O_WRONLY);
    if (fd == -1) {
        ilka_fail_errno("unable to open region: %s", j->file);
        return false;
    }

    struct journal_node node;
    size_t i = _journal_next(j, 0, &node);
    for (; i < j->len; i = _journal_next(j, i, &node)) {
        const void *ptr = ilka_read(j->region, node.off, node.len);

        ssize_t ret = pwrite(fd, ptr, node.len, node.off);
        if (ret == -1) {
            ilka_fail_errno("unable to write to region");
            goto fail;
        }

        if ((size_t) ret != node.len) {
            ilka_fail("incomplete write to region: %lu != %lu", ret, node.len);
            goto fail;
        }
    }

    if (fdatasync(fd) == -1) {
        ilka_fail_errno("unable to fsync region: %s", j->file);
        goto fail;
    }

    if (close(fd) == -1) {
        ilka_fail_errno("unable to close region: %s", j->file);
        return false;
    }

    return true;

  fail:
    close(fd);
    return false;
}

static bool journal_finish(struct ilka_journal *j)
{
    bool result = false;

    if (!_journal_write_log(j)) goto fail;
    if (!_journal_write_region(j)) goto fail;

    if (unlink(j->journal_file) == -1) {
        ilka_fail_errno("unable to unlink journal: %s", j->journal_file);
        goto fail;
    }

    result = true;

  fail:
    free(j->nodes);
    free(j->journal_file);
    return result;
}

static int _journal_check(const char *file)
{
    int fd = open(file, O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT) return 0;
        ilka_fail_errno("unable to open journal: %s", file);
        return -1;
    }

    uint64_t magic = 0;

    size_t len = file_len(fd);
    if (len <= sizeof(magic)) goto bad_file;

    ssize_t ret = pread(fd, &magic, sizeof(magic), len - sizeof(magic));
    if (ret == -1) {
        ilka_fail_errno("unable to read from journal");
        goto fail;
    }
    else if ((size_t) ret != len) {
        ilka_fail("incomplete read from journal: %lu != %lu", ret, sizeof(magic));
        goto fail;
    }

    if (magic != journal_magic) goto bad_file;

    if (lseek(fd, 0, SEEK_SET) == -1) {
        ilka_fail_errno("unable to seek journal: %s", file);
        goto fail;
    }

    return fd;

  bad_file:
    close(fd);
    unlink(file);
    return 0;

  fail:
    close(fd);
    return -1;
}

static bool _journal_read(int fd, void *ptr, size_t len)
{
    ssize_t ret = read(fd, ptr, len);
    if (ret == -1) {
        ilka_fail_errno("unable to read from journal");
        return false;
    }
    if ((size_t) ret != len) {
        ilka_fail("incomplete read from journal: %lu != %lu", ret, len);
        return false;
    }
    return true;
}

static bool journal_recover(const char *file)
{
    void *buf = NULL;

    char *journal_file = _journal_file(file);
    int journal_fd = _journal_check(journal_file);
    if (journal_fd == -1) goto fail;
    if (!journal_fd) goto done;

    int region_fd = open(file, O_WRONLY);
    if (region_fd == -1) {
        ilka_fail_errno("unable to open region: %s", file);
        goto fail;
    }

    struct journal_node node = {0, 0};

    size_t cap = ILKA_PAGE_SIZE;
    buf = malloc(cap);
    if (!buf) {
        ilka_fail("out-of-memory recover buffer: %lu", cap);
        goto fail;
    }

    while (true) {
        if (!_journal_read(journal_fd, &node, sizeof(node))) goto fail;
        if (node.off == 0 && node.len == 0) break;

        if (cap < node.len) {
            free(buf);
            cap = node.len;
            buf = malloc(cap);
            if (!buf) {
                ilka_fail("out-of-memory recover buffer: %lu", cap);
                goto fail;
            }
        }

        if (!_journal_read(journal_fd, buf, node.len)) goto fail;

        ssize_t ret = pwrite(region_fd, buf, node.len, node.off);
        if (ret == -1) {
            ilka_fail_errno("unable to write to region: %s", file);
            goto fail;
        }
        if ((size_t) ret != node.len) {
            ilka_fail("incomplete write to region: %lu != %lu", ret, node.len);
            goto fail;
        }
    }

    free(buf);

    if (fdatasync(region_fd) == -1) ilka_fail_errno("unable to fsync region: %s", file);
    if (close(region_fd) == -1) ilka_fail_errno("unable to close region: %s", file);

    if (close(journal_fd) == -1) ilka_fail_errno("unable to close journal: %s", journal_file);
    if (unlink(journal_file) == -1) ilka_fail_errno("unable to unlink journal: %s", journal_file);

  done:
    if (buf) free(buf);
    free(journal_file);
    return true;

  fail:
    if (buf) free(buf);
    free(journal_file);
    return false;
}
