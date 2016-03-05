/* epoch.c
   Rémi Attab (remi.attab@gmail.com), 28 Feb 2016
   FreeBSD-style copyright and disclaimer apply
*/


// -----------------------------------------------------------------------------
// defer
// -----------------------------------------------------------------------------

struct epoch_defer
{
    size_t epoch;

    void *data;
    void (*fn) (void *);

    ilka_off_t off;
    size_t len;
    size_t area;

    struct epoch_defer *next;
};


// -----------------------------------------------------------------------------
// implementations
// -----------------------------------------------------------------------------

#include "epoch_threads.c"
#include "epoch_gc.c"

#include "epoch_private.c"
#include "epoch_shared.c"


// -----------------------------------------------------------------------------
// common structs
// -----------------------------------------------------------------------------

union ilka_epoch
{
    struct epoch_priv priv;
    struct epoch_shrd shrd;
};
