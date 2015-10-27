/* error.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   Error reporting utility.
*/

#pragma once

// -----------------------------------------------------------------------------
// error
// -----------------------------------------------------------------------------

#define ILKA_ERR_MSG_CAP 1024UL

struct ilka_error
{
    const char *file;
    int line;

    int errno_; // errno can be a macro hence the underscore.
    char msg[ILKA_ERR_MSG_CAP];
};

extern __thread struct ilka_error ilka_err;

void ilka_abort() ilka_noreturn;
void ilka_perror(struct ilka_error *err);


// -----------------------------------------------------------------------------
// fail
// -----------------------------------------------------------------------------

void ilka_dbg_abort_on_fail();

void ilka_vfail(const char *file, int line, const char *fmt, ...)
    ilka_printf(3, 4);

void ilka_vfail_errno(const char *file, int line, const char *fmt, ...)
    ilka_printf(3, 4);

#define ilka_fail(...)                          \
    ilka_vfail(__FILE__, __LINE__, __VA_ARGS__)

#define ilka_fail_errno(...)                            \
    ilka_vfail_errno(__FILE__, __LINE__, __VA_ARGS__)


// -----------------------------------------------------------------------------
// assert
// -----------------------------------------------------------------------------

#define ilka_assert(p, ...)                     \
    do {                                        \
        if (ilka_likely(p)) break;              \
        ilka_fail(__VA_ARGS__);                 \
        ilka_abort();                           \
    } while (0)

#define ilka_todo(msg)                          \
    do {                                        \
        ilka_fail("TODO: " msg);                \
        ilka_abort();                           \
    } while (0)
