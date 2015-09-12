/* persist.c
   Rémi Attab (remi.attab@gmail.com), 02 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "region.h"

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

static struct ilka_persist * persist_init(
        struct ilka_region *r, const char *file)
{
    struct ilka_persist *p = calloc(1, sizeof(struct ilka_persist));

    p->region = r;
    p->file = file;
    slock_init(&p->lock);

    return p;
}

static void persist_close(struct ilka_persist *p)
{
    struct persist_node *head = p->head;
    while (head) {
        struct persist_node *next = head->next;
        free(head);
        head = next;
    }

    free(p);
}

static void persist_mark(struct ilka_persist *p, ilka_off_t off, size_t len)
{
    struct persist_node *node = calloc(1, sizeof(struct persist_node));
    node->off = off;
    node->len = len;

    struct persist_node *head = ilka_atomic_load(&p->head, morder_relaxed);
    do {
        node->next = head;
    } while (ilka_atomic_cmp_xchg(&p->head, &head, node, morder_relaxed));
}

static void _persist_wait(pid_t pid) 
{
    int status;
    do {
        if (waitpid(pid, &status, WUNTRACED) == -1)
            ilka_error_errno("unable to wait on persist process: %d", pid);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    if (WIFEXITED(status)) {
        if (!WEXITSTATUS(status)) return;
        ilka_error("persist process returned error: %d", WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status)) {
        ilka_error("persist process signaled: %d", WTERMSIG(status));
    }
}

static void persist_save(struct ilka_persist *p)
{
    struct persist_node *head = ilka_atomic_xchg(&p->head, NULL, morder_relaxed);
    if (!head) return;

    slock_lock(&p->lock);
    ilka_world_stop(p->region);

    pid_t pid = fork();
    if (pid == -1) ilka_error_errno("unable to fork for persist");

    ilka_world_resume(p->region);

    if (pid) {
        while (head) {
            struct persist_node *next = head->next;
            free(head);
            head = next;
        }

        _persist_wait(pid);
        slock_unlock(&p->lock);
    }

    else {
        struct ilka_journal *j = journal_init(p->region, p->file);

        while (head) {
            journal_add(j, head->off, head->len);
            head = head->next;
        }

        journal_finish(j);
        exit(0);
    }
}
