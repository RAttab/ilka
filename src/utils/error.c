/* error.c
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply
*/

#include "error.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>


// -----------------------------------------------------------------------------
// error
// -----------------------------------------------------------------------------

void ilka_verror(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buf[1024];
    (void) vsnprintf(buf, sizeof(buf) - 1, fmt, args);

    printf("<%s:%d> %s\n", file, line, buf);
    abort();
}

void ilka_verror_errno(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buf[1024];
    (void) vsnprintf(buf, sizeof(buf) - 1, fmt, args);

    printf("<%s:%d> %s - %s(%d)\n", file, line, buf, strerror(errno), errno);
    abort();
}


