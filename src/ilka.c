/* ilka.c
   Rémi Attab (remi.attab@gmail.com), 25 May 2014
   FreeBSD-style copyright and disclaimer apply

   Ilka's compilation unit.
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <execinfo.h> // backtrace

#include "ilka.h"
#include "utils/utils.h"

#include "log.c"
#include "error.c"
#include "key.c"
