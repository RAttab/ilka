/* utils.c
   Rémi Attab (remi.attab@gmail.com), 25 May 2014
   FreeBSD-style copyright and disclaimer apply

   Utils compilation unit.
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>

#include "ilka.h"
#include "utils/utils.h"

#include "rand.c"
#include "thread.c"
#include "time.c"
