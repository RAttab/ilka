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

bool ilka_srand(uint32_t seed)
{
    ilka_assert(seed, "seed can't be 0 due to implementation details");

    if (!rand_state) {
        rand_state = calloc(rand_state_len, sizeof(char));
        if (initstate_r(seed, rand_state, rand_state_len, &rand_data) == -1) {
            ilka_fail_errno("unable to initialize the rng state");
            return false;
        }
    }

    else if (srandom_r(seed, &rand_data) == -1) {
        ilka_fail_errno("unable to seed rng");
        return false;
    }

    return true;
}

int32_t ilka_rand()
{
    int32_t value;
    if (random_r(&rand_data, &value) == -1) {
        ilka_fail_errno("unable to generate random value");
        return -1;
    }

    return value;
}

int32_t ilka_rand_range(uint32_t min, uint32_t max)
{
    ilka_assert(min < max, "max must be strictly greater then min");

    int32_t value = ilka_rand();
    if (value == -1) return -1;

    return value % (max - min) + min;
}
