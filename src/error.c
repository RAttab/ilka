/* error.c
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

// -----------------------------------------------------------------------------
// error
// -----------------------------------------------------------------------------

__thread struct ilka_error ilka_err = { 0 };

void ilka_abort()
{
    ilka_perror(&ilka_err);
    ilka_log_dump();
    abort();
}

void ilka_perror(struct ilka_error *err)
{
    if (!err->errno_)
        printf("<%lu> %s:%d: %s\n", ilka_tid(), err->file, err->line, err->msg);
    else {
        printf("<%lu> %s:%d: %s - %s(%d)\n",
                ilka_tid(), err->file, err->line, err->msg,
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

    if (abort_on_fail) ilka_abort();
}

void ilka_vfail_errno(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    ilka_err = (struct ilka_error) { .errno_ = errno, .file = file, .line = line };
    (void) vsnprintf(ilka_err.msg, ILKA_ERR_MSG_CAP, fmt, args);

    if (abort_on_fail) ilka_abort();
}
