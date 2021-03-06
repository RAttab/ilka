/* check.h
   Rémi Attab (remi.attab@gmail.com), 28 May 2014
   FreeBSD-style copyright and disclaimer apply

   Wrappers and utils for the check lib.
*/

#pragma once

#include <check.h>
#include <stdio.h>

#include "ilka.h"
#include "utils/utils.h"

typedef void (*ilka_make_suite_t)(Suite *);
int ilka_tests(const char *name, ilka_make_suite_t make_suite);

void ilka_setup();
void ilka_teardown();

void ilka_print_title(const char *title);

enum { ilka_tc_timeout = 10 };

#define ilka_tc_pre(suite, name, enabled)                       \
    do {                                                        \
    TCase *tc = tcase_create(#name);                            \
    (void) name;                                                \
    if (!enabled) break;                                        \
    tcase_add_unchecked_fixture(tc, ilka_setup, ilka_teardown); \

#define ilka_tc_post(suite, name)               \
    suite_add_tcase(suite, tc);                 \
    } while (false)

#define ilka_tc(s, n, t)                        \
    ilka_tc_pre(s, n, t);                       \
    tcase_set_timeout(tc, ilka_tc_timeout);     \
    tcase_add_test(tc, n);                      \
    ilka_tc_post(s, n)

#define ilka_tc_signal(s, n, sig, t)            \
    ilka_tc_pre(s, n, t);                       \
    tcase_set_timeout(tc, ilka_tc_timeout);     \
    tcase_add_test_raise_signal(tc, n, sig);    \
    ilka_tc_post(s, n)


#define ilka_tc_timeout(s, n, timeout, t)       \
    ilka_tc_pre(s, n, t);                       \
    tcase_set_timeout(tc, timeout);             \
    tcase_add_test(tc, n);                      \
    ilka_tc_post(s, n)
