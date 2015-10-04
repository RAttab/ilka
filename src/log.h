/* log.h
   Rémi Attab (remi.attab@gmail.com), 20 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

// -----------------------------------------------------------------------------
// log
// -----------------------------------------------------------------------------

void ilka_logt(const char *title);
void ilka_log(const char *title, const char *fmt, ...) ilka_printf(2, 3);
