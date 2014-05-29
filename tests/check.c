/* check.c
   Rémi Attab (remi.attab@gmail.com), 28 May 2014
   FreeBSD-style copyright and disclaimer apply

   Utilities for the check based tests.
*/

#include "check.h"

int ilka_tests(const char *name, ilka_make_suite_t make_suite)
{
    Suite *suite = suite_create(name);
    make_suite(suite);

    SRunner *runner = srunner_create(suite);
    /* srunner_set_fork_status(runner, CK_NOFORK); */
    srunner_run_all(runner, CK_NORMAL);
    return srunner_ntests_failed(runner);
}

