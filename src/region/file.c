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
    if (options->truncate) flags |= O_TRUNC;
    flags |= options->read_only ? O_RDONLY : O_RDWR;

    int mode = options->mode ? options->mode : 0600;

    int fd = open(file, flags, mode);
    if (fd == -1) {
        ilka_fail_errno("unable to open '%s'", file);
        return -1;
    }

    return fd;
}

static int file_open_shm(const char *name, struct ilka_options *options)
{
    if (!options->open && !options->create) {
        ilka_fail("must provide 'ilka_open' or 'ilka_create' to open '%s'", name);
        return -1;
    }

    int flags = 0;

    if (options->create) {
        flags |= O_CREAT;
        flags |= options->open ? 0 : O_EXCL;
    }
    if (options->truncate) flags |= O_TRUNC;
    flags |= options->read_only ? O_RDONLY : O_RDWR;

    int mode = options->mode ? options->mode : 0600;

    int fd = shm_open(name, flags, mode);
    if (fd == -1) {
        ilka_fail_errno("unable to open shm '%s'", name);
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

static bool file_rm(const char *file)
{
    if (!unlink(file)) return true;

    ilka_fail_errno("unable to unlink '%s'", file);
    return false;
}

static bool file_rm_shm(const char *file)
{
    if (!shm_unlink(file)) return true;

    ilka_fail_errno("unable to unlink shm '%s'", file);
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
