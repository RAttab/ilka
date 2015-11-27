/* log.h
   Rémi Attab (remi.attab@gmail.com), 20 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once


// -----------------------------------------------------------------------------
// log
// -----------------------------------------------------------------------------

#ifndef ILKA_LOG_RING
# define ILKA_LOG_RING 0
#endif

#ifdef ILKA_LOG
# define ilka_log(t, ...) ilka_log_impl(t, __VA_ARGS__)
#else
# define ilka_log(t, ...) do { (void) t; } while (false)
#endif

void ilka_log_impl(const char *title, const char *fmt, ...) ilka_printf(2, 3);
void ilka_log_dump();
