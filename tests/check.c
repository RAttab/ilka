/* check.c
   Rémi Attab (remi.attab@gmail.com), 28 May 2014
   FreeBSD-style copyright and disclaimer apply

   Utilities for the check based tests.
*/

#include "check.h"

#include <stdlib.h>
#include <string.h>


// -----------------------------------------------------------------------------
// test utils
// -----------------------------------------------------------------------------

static void config_runner(SRunner *runner)
{
    printf("Fork test case(s): ");
    if (getenv("ILKA_NOFORK")) {
        srunner_set_fork_status(runner, CK_NOFORK);
        printf("no\n");
    }
    else printf("yes\n");
}

int ilka_tests(const char *name, ilka_make_suite_t make_suite)
{
    Suite *suite = suite_create(name);
    make_suite(suite);

    SRunner *runner = srunner_create(suite);
    config_runner(runner);

    srunner_run_all(runner, CK_NORMAL);
    return srunner_ntests_failed(runner);
}


void ilka_print_title(const char *title)
{
    char buf[81];
    int n = snprintf(buf, 81, "[ %s ]", title);

    memset(buf + n, '=', 80 - n);
    buf[80] = '\0';

    printf("\n%s\n", buf);
}

