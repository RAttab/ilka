/* thread.h
   Rémi Attab (remi.attab@gmail.com), 20 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

// -----------------------------------------------------------------------------
// tid
// -----------------------------------------------------------------------------

size_t ilka_cpus();
size_t ilka_tid();

void ilka_run_threads(void (*fn) (size_t, void *), void *data, size_t n);
