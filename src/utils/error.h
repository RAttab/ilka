/* error.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   Error reporting utility.
*/

#pragma once

#include "compiler.h"


// -----------------------------------------------------------------------------
// error
// -----------------------------------------------------------------------------

void ilka_verror(const char *file, int line, const char *fmt, ...)
    ilka_noreturn ilka_printf(3, 4);

void ilka_verror_errno(const char *file, int line, const char *fmt, ...)
    ilka_noreturn ilka_printf(3, 4);

#define ilka_error(...)                                 \
    ilka_verror(__FILE__, __LINE__, __VA_ARGS__)

#define ilka_error_errno(...)                           \
    ilka_verror_errno(__FILE__, __LINE__, __VA_ARGS__)

#define ilka_assert(p, ...)                                     \
    do {                                                        \
        if (ilka_unlikely(!(p)))                                \
            ilka_error(__VA_ARGS__);                            \
    } while (0)

#define ilka_todo()                                     \
    ilka_error(__FUNCTION__ ": not yet implemented")
