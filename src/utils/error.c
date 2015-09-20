/* error.c
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply
*/

#include "error.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>


// -----------------------------------------------------------------------------
// error
// -----------------------------------------------------------------------------

__thread struct ilka_error ilka_err = { 0 };

void ilka_abort()
{
    ilka_perror(&ilka_err);
    abort();
}

void ilka_perror(struct ilka_error *err)
{
    if (!err->errno_)
        printf("%s:%d: %s\n", err->file, err->line, err->msg);
    else {
        printf("%s:%d: %s - %s(%d)\n",
                err->file, err->line, err->msg,
                strerror(err->errno_), err->errno_);
    }
}


// -----------------------------------------------------------------------------
// fail
// -----------------------------------------------------------------------------

static bool abort_on_fail = 0;
void ilka_dbg_abort_on_fail() { abort_on_fail = true; }

void ilka_vfail(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    ilka_err = (struct ilka_error) { .errno_ = 0, .file = file, .line = line };
    (void) vsnprintf(ilka_err.msg, ILKA_ERR_MSG_CAP, fmt, args);

    if (abort_on_fail) {
        ilka_perror(&ilka_err);
        ilka_abort();
    }
}

void ilka_vfail_errno(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    ilka_err = (struct ilka_error) { .errno_ = errno, .file = file, .line = line };
    (void) vsnprintf(ilka_err.msg, ILKA_ERR_MSG_CAP, fmt, args);

    if (abort_on_fail) {
        ilka_perror(&ilka_err);
        ilka_abort();
    }
}
