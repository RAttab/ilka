/* rand.c
   Rémi Attab (remi.attab@gmail.com), 12 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#include "rand.h"
#include "error.h"

#include <stdlib.h>


// -----------------------------------------------------------------------------
// rand
// -----------------------------------------------------------------------------

enum { rand_state_len = 8 };
static __thread char *rand_state = NULL;
static __thread struct random_data rand_data;

void ilka_srand(uint32_t seed)
{
    ilka_assert(seed, "seed can't be 0 due to implementation details");

    if (!rand_state) {
        rand_state = calloc(rand_state_len, sizeof(char));
        if (initstate_r(seed, rand_state, rand_state_len, &rand_data) == -1)
            ilka_error_errno("unable to initialize the rng state");
    }

    else if (srandom_r(seed, &rand_data) == -1) {
        ilka_error_errno("unable to seed rng");
    }
}

uint32_t ilka_rand()
{
    int32_t value;
    if (random_r(&rand_data, &value) == -1)
        ilka_error_errno("unable to generate random value");

    return value;
}

uint32_t ilka_rand_range(uint32_t min, uint32_t max)
{
    ilka_assert(min < max, "max must be strictly greater then min");
    return ilka_rand() % (max - min) + min;
}
