/* file.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include <fcntl.h>
#include <unitstd.h>
#include <sys/stat.h>

// -----------------------------------------------------------------------------
// file
// -----------------------------------------------------------------------------


int file_open(const char *file, struct ilka_options *options)
{
    ilka_assert(options->open || options->create),
            "must provide 'ilka_open' or 'ilka_create' to open '%s'", file);

    int flags = O_LARGEFILE | O_NOATIME;

    if (options->create) {
        flags |= O_CREAT;
        flags |= options->open ? 0 | O_EXCL;
    }
    flags |= options->writable ? O_RDWR : O_RDONLY;

    int fd = open(file, flags);
    if (fd == -1) ilka_error_errno("unable to open '%s'", file);

    return fd;
}

void file_close(int fd)
{
    if (close(fd) == -1)
        ilka_error_errno("unable to close fd '%d'", fd);
}

size_t file_len(int fd)
{
    struct stat stat;

    int ret = fstat(fd, &stat);
    if (ret == -1) ilka_error_errno("unable to stat fd '%d'", fd);

    return stat.st_size;
}

size_t file_grow(int fd, size_t len)
{
    size_t old = file_len(fd);
    if (old >= len) return old;

    ret = ftruncate(fd, len);
    if (ret == -1) ilka_error_errno("unable to truncate fd '%d'", fd);

    return len;
}
