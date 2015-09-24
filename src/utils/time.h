/* time.h
   Rémi Attab (remi.attab@gmail.com), 22 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

// -----------------------------------------------------------------------------
// timer
// -----------------------------------------------------------------------------

struct timespec ilka_now();
double ilka_elapsed(struct timespec *start);
size_t ilka_print_elapsed(char *buf, size_t n, double t);
