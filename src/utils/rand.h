/* rand.h
   Rémi Attab (remi.attab@gmail.com), 12 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

#include <stdint.h>

// -----------------------------------------------------------------------------
// rand
// -----------------------------------------------------------------------------

void ilka_srand(uint32_t seed);
uint32_t ilka_rand();
