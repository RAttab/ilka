/* log.c
   Rémi Attab (remi.attab@gmail.com), 20 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "log.h"
#include "thread.h"

#include <stdio.h>
#include <stdarg.h>

// -----------------------------------------------------------------------------
// log
// -----------------------------------------------------------------------------

void ilka_log(const char *title, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char *buf = alloca(1024);
    (void) vsnprintf(buf, 1024, fmt, args);

    fprintf(stderr, "<%lu> %s: %s\n", ilka_tid(), title, buf);
}
