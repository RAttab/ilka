/* check.h
   Rémi Attab (remi.attab@gmail.com), 28 May 2014
   FreeBSD-style copyright and disclaimer apply

   Wrappers and utils for the check lib.
*/

#pragma once

#include <check.h>
#include <stdio.h>

typedef void (*ilka_make_suite_t)(Suite *);
int ilka_tests(const char *name, ilka_make_suite_t make_suite);

#define ilka_tc_pre(suite, name) do { TCase *tc = tcase_create(#name)
#define ilka_tc_post(suite, name) suite_add_tcase(suite, tc); } while(0)

#define ilka_tc(s, n)                           \
    ilka_tc_pre(s, n);                          \
    tcase_add_test(tc, n);                      \
    ilka_tc_post(s, n)

#define ilka_tc_signal(s, n, sig)               \
    ilka_tc_pre(s, n);                          \
    tcase_add_test_raise_signal(tc, n, sig);    \
    ilka_tc_post(s, n)

#define ilka_tc_exit(s, n, exp)                 \
    ilka_tc_pre(s, n);                          \
    tcase_add_exit_test(tc, n, exp);            \
    ilka_tc_post(s, n)

#define ilka_tc_loop(s, n, start, end)          \
    ilka_tc_pre(s, n);                          \
    tcase_add_loop_test(tc, n, start, end);     \
    ilka_tc_post(s, n)

#define ilka_tc_loop_signal(s, n, start, end, sig)      \
    ilka_tc_pre(s, n);                                  \
    tcase_add_loop_test(tc, n, sig, start, end);        \
    ilka_tc_post(s, n)

#define ilka_tc_loop_exit(s, n, start, end, exp)        \
    ilka_tc_pre(s, n);                                  \
    tcase_add_loop_test(tc, n, exp, start, end);        \
    ilka_tc_post(s, n)
