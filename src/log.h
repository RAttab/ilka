/* log.h
   Rémi Attab (remi.attab@gmail.com), 20 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once


// -----------------------------------------------------------------------------
// log
// -----------------------------------------------------------------------------

#define ILKA_LOG_RING 0

void ilka_log(const char *title, const char *fmt, ...) ilka_printf(2, 3);
void ilka_log_dump();
