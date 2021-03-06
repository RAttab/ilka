/* compiler.h
   Rémi Attab (remi.attab@gmail.com), 03 May 2014
   FreeBSD-style copyright and disclaimer apply

   Compiler related utilities and attributes.
*/

#pragma once

// -----------------------------------------------------------------------------
// attributes
// -----------------------------------------------------------------------------

#define ilka_unused       __attribute__((unused))
#define ilka_noreturn     __attribute__((noreturn))
#define ilka_align(x)     __attribute__((aligned(x)))
#define ilka_packed       __attribute__((__packed__))
#define ilka_pure         __attribute__((pure))
#define ilka_printf(x,y)  __attribute__((format(printf, x, y)))
#define ilka_malloc       __attribute__((malloc))
#define ilka_noinline     __attribute__((noinline))
#define ilka_likely(x)    __builtin_expect(x, 1)
#define ilka_unlikely(x)  __builtin_expect(x, 0)


// -----------------------------------------------------------------------------
// builtin
// -----------------------------------------------------------------------------

#define ilka_unreachable() __builtin_unreachable()


// -----------------------------------------------------------------------------
// asm
// -----------------------------------------------------------------------------

#define ilka_asm __asm__

#define ilka_no_opt()         ilka_asm volatile ("")
#define ilka_no_opt_val(x)    ilka_asm volatile ("" : "+r" (x))
#define ilka_no_opt_clobber() ilka_asm volatile ("" : : : "memory")
