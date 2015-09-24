/* file.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// file
// -----------------------------------------------------------------------------


static int file_open(const char *file, struct ilka_options *options)
{
    if (!options->open && !options->create) {
        ilka_fail("must provide 'ilka_open' or 'ilka_create' to open '%s'", file);
        return -1;
    }

    int flags = O_NOATIME;

    if (options->create) {
        flags |= O_CREAT;
        flags |= options->open ? 0 : O_EXCL;
    }
    flags |= options->read_only ? O_RDONLY : O_RDWR;

    int fd = open(file, flags, 0764);
    if (fd == -1) {
        ilka_fail_errno("unable to open '%s'", file);
        return -1;
    }

    return fd;
}

static bool file_close(int fd)
{
    if (close(fd) != -1) return true;

    ilka_fail_errno("unable to close fd '%d'", fd);
    return false;
}

static ssize_t file_len(int fd)
{
    struct stat stat;

    int ret = fstat(fd, &stat);
    if (ret == -1) {
        ilka_fail_errno("unable to stat fd '%d'", fd);
        return -1;
    }

    return stat.st_size;
}

static ssize_t file_grow(int fd, size_t len)
{
    ssize_t old = file_len(fd);
    if (old == -1) return -1;
    if ((size_t) old >= len) return old;

    int ret = ftruncate(fd, len);
    if (ret == -1) {
        ilka_fail_errno("unable to truncate fd '%d'", fd);
        return -1;
    }

    return len;
}
