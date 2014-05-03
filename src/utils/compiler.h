/* compiler.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   Compiler related utilities and attributes.
*/

#pragma once


// attributes
// -----------------------------------------------------------------------------

#define ilka_unused       __attribute__((unused))
#define ilka_noreturn     __attribute__((noreturn))
#define ilka_align(x)     __attribute__((aligned(x)))
#define ilka_packed       __attribute__((__packed__))
#define ilka_pure         __attribute__((pure))
#define ilka_printf(x,y)  __attribute__((format(printf, x, y)))
#define ilka_malloc       __attribute__((malloc))
#define ilka_likely(x)    __builtin_expect(x,true)
#define ilka_unlikely(x)  __builtin_expect(x,false)


// builtin
// -----------------------------------------------------------------------------

#define ilka_unreachable() __builtin_unreachable()

