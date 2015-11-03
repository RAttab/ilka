/* preproc.h
   Rémi Attab (remi.attab@gmail.com), 03 Nov 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once


// -----------------------------------------------------------------------------
// stringify
// -----------------------------------------------------------------------------

#define ilka_stringify_impl(v) #v
#define ilka_stringify(v) ilka_stringify_impl(v)
