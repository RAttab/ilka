/* check.h
   Rémi Attab (remi.attab@gmail.com), 28 May 2014
   FreeBSD-style copyright and disclaimer apply

   Wrappers and utils for the check lib.
*/

#pragma once

#include <check.h>

typedef void (*ilka_make_suite_t)(Suite *);
int ilka_tests(const char *name, ilka_make_suite_t make_suite);


