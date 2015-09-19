/* rand.h
   Rémi Attab (remi.attab@gmail.com), 12 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>


// -----------------------------------------------------------------------------
// rand
// -----------------------------------------------------------------------------

bool ilka_srand(uint32_t seed);
int32_t ilka_rand();
int32_t ilka_rand_range(uint32_t min, uint32_t max);
