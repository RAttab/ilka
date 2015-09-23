/* persist.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "region.h"
#include "utils/compiler.h"

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>


// -----------------------------------------------------------------------------
// persist
// -----------------------------------------------------------------------------

struct persist_node
{
    ilka_off_t off;
    size_t len;

    struct persist_node *next;
};

struct ilka_persist
{
    struct ilka_region *region;
    const char *file;

    ilka_slock lock;
    struct persist_node *head;
};

static bool persist_init(
        struct ilka_persist *p, struct ilka_region *r, const char *file)
{
    memset(p, 0, sizeof(struct ilka_persist));

    p->region = r;
    p->file = file;
    slock_init(&p->lock);

    return true;
}

static void persist_close(struct ilka_persist *p)
{
    struct persist_node *head = p->head;
    while (head) {
        struct persist_node *next = head->next;
        free(head);
        head = next;
    }
}

static bool persist_mark(struct ilka_persist *p, ilka_off_t off, size_t len)
{
    struct persist_node *node = calloc(1, sizeof(struct persist_node));
    if (!node) {
        ilka_fail("out-of-memory for persist node: %lu", sizeof(struct persist_node));
        return false;
    }

    node->off = off;
    node->len = len;

    struct persist_node *head = ilka_atomic_load(&p->head, morder_relaxed);
    do {
        node->next = head;
    } while (!ilka_atomic_cmp_xchg(&p->head, &head, node, morder_relaxed));

    return true;
}

static bool _persist_wait(pid_t pid)
{
    int status;
    do {
        if (waitpid(pid, &status, WUNTRACED) == -1) {
            ilka_fail_errno("unable to wait on persist process: %d", pid);
            return false;
        }
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    if (WIFEXITED(status)) {
        if (!WEXITSTATUS(status)) return false;

        ilka_fail("persist process returned error: %d", WEXITSTATUS(status));
        return false;
    }
    else if (WIFSIGNALED(status)) {
        ilka_fail("persist process signaled: %d", WTERMSIG(status));
        return false;
    }

    return true;
}

static bool persist_save(struct ilka_persist *p)
{
    struct persist_node *head = ilka_atomic_xchg(&p->head, NULL, morder_relaxed);
    if (!head) return true;

    slock_lock(&p->lock);

    ilka_world_stop(p->region);

    pid_t pid = fork();

    ilka_world_resume(p->region);

    if (pid == -1) {
        ilka_fail_errno("unable to fork for persist");
        slock_unlock(&p->lock);
        return false;
    }


    if (pid) {
        while (head) {
            struct persist_node *next = head->next;
            free(head);
            head = next;
        }

        bool ret = _persist_wait(pid);
        slock_unlock(&p->lock);

        return ret;
    }

    else {
        struct ilka_journal j;
        if (!journal_init(&j, p->region, p->file)) ilka_abort();

        while (head) {
            if (!journal_add(&j, head->off, head->len)) ilka_abort();
            head = head->next;
        }

        if (!journal_finish(&j)) ilka_abort();

        exit(0);
    }

    ilka_unreachable();
}
