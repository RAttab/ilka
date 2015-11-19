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
    enum { buf_len = 80 * ILKA_ERR_BACKTRACE_CAP };
    char buf[buf_len];
    size_t buf_i = 0;

    if (!err->errno_) {
        buf_i = snprintf(buf, buf_len, "<%lu> %s:%d: %s\n",
                ilka_tid(), err->file, err->line, err->msg);
    }
    else {
        buf_i = snprintf(buf, buf_len, "<%lu> %s:%d: %s - %s(%d)\n",
                ilka_tid(), err->file, err->line, err->msg,
                strerror(err->errno_), err->errno_);
    }

    if (ilka_err.backtrace_len > 0) {
        char **symbols = backtrace_symbols(ilka_err.backtrace, ilka_err.backtrace_len);
        for (int i = 0; i < ilka_err.backtrace_len; ++i) {
            buf_i += snprintf(buf + buf_i, buf_len - buf_i,
                    "  {%d} %s\n", i, symbols[i]);
        }
    }

    if (write(2, buf, buf_i) == -1) fprintf(stderr, "ilka_perror failed");
}


// -----------------------------------------------------------------------------
// fail
// -----------------------------------------------------------------------------

static bool abort_on_fail = 0;
void ilka_dbg_abort_on_fail() { abort_on_fail = true; }

static void fail_backtrace()
{
    ilka_err.backtrace_len = backtrace(ilka_err.backtrace, ILKA_ERR_BACKTRACE_CAP);
    if (ilka_err.backtrace_len == -1)
        printf("unable to sample backtrace: %s", strerror(errno));
}

void ilka_vfail(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    ilka_err = (struct ilka_error) { .errno_ = 0, .file = file, .line = line };
    (void) vsnprintf(ilka_err.msg, ILKA_ERR_MSG_CAP, fmt, args);

    fail_backtrace();
    if (abort_on_fail) ilka_abort();
}

void ilka_vfail_errno(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    ilka_err = (struct ilka_error) { .errno_ = errno, .file = file, .line = line };
    (void) vsnprintf(ilka_err.msg, ILKA_ERR_MSG_CAP, fmt, args);

    fail_backtrace();
    if (abort_on_fail) ilka_abort();
}
