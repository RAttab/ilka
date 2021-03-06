/* check.c
   Rémi Attab (remi.attab@gmail.com), 28 May 2014
   FreeBSD-style copyright and disclaimer apply

   Utilities for the check based tests.
*/

#include "check.h"
#include "utils/utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>


// -----------------------------------------------------------------------------
// utils
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
    ilka_dbg_abort_on_fail();

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


// -----------------------------------------------------------------------------
// fixture
// -----------------------------------------------------------------------------

static void deltree(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) {
        if (errno == ENOENT) return;
        ilka_fail_errno("unable to opendir: %s", path);
        ilka_abort();
    }

    size_t path_len = strlen(path) + 1;

    char *child = alloca(path_len + NAME_MAX + 1);
    memcpy(child, path, path_len);
    child[path_len - 1] = '/';


    struct dirent *entry;
    while ((entry = readdir(dir))) {
        memcpy(child + path_len, entry->d_name, sizeof(entry->d_name));

        switch (entry->d_type) {

        case DT_DIR:
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
            deltree(child);
            break;

        case DT_REG:
            if (unlink(child) == -1) {
                ilka_fail_errno("unable to unlink: %s", child);
                ilka_abort();
            }
            break;

        default:
            ilka_fail("unknown dirent type '%d' for '%s'", entry->d_type, path);
            ilka_abort();
        };
    }

    if (closedir(dir) == -1) {
        ilka_fail_errno("unable to closedir: %s", path);
        ilka_abort();
    }
    if (rmdir(path) == -1 && errno != ENOENT) {
        ilka_fail_errno("unable to rmdir: %s", path);
        ilka_abort();
    }
}


static const char *tpath = "./tdata";
static char cwd[1024] = {0};

void ilka_setup()
{
    deltree(tpath);
    if (mkdir(tpath, 0755) == -1 && errno != EEXIST) {
        ilka_fail_errno("unable to mkdir: %s", tpath);
        ilka_abort();
    }

    if (!getcwd(cwd, sizeof(cwd))) {
        ilka_fail_errno("unable to getcwd");
        ilka_abort();
    }

    if (chdir(tpath) == -1) {
        ilka_fail_errno("unable to chdir: %s", tpath);
        ilka_abort();
    }
}

void ilka_teardown()
{
    if (chdir(cwd) == -1) {
        ilka_fail_errno("unable to chdir: %s", tpath);
        ilka_abort();
    }
    deltree(tpath);
}
