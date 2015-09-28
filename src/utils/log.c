/* log.c
   Rémi Attab (remi.attab@gmail.com), 20 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

// -----------------------------------------------------------------------------
// log
// -----------------------------------------------------------------------------

static size_t tick = 0;

void ilka_log(const char *title, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char *buf = alloca(1024);
    (void) vsnprintf(buf, 1024, fmt, args);

    size_t t = ilka_atomic_fetch_add(&tick, 1, morder_seq_cst);
    fprintf(stderr, "[%8lu] <%lu> %s: %s\n", t, ilka_tid(), title, buf);
}
