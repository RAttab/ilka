/* alloc_test.c
   Rémi Attab (remi.attab@gmail.com), 12 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "region/region.h"
#include "check.h"


// -----------------------------------------------------------------------------
// page test
// -----------------------------------------------------------------------------

START_TEST(page_test)
{
    struct ilka_options options = { .open = true, .create = true };
    struct ilka_region *r = ilka_open("blah", &options);


    ilka_close(r);
}
END_TEST

void make_suite(Suite *s)
{
    ilka_tc(s, page_test);
}

int main(void)
{
    return ilka_tests("alloc_test", &make_suite);
}
