/* time.h
   Rémi Attab (remi.attab@gmail.com), 22 Sep 2015
   FreeBSD-style copyright and disclaimer apply
*/

#pragma once

// -----------------------------------------------------------------------------
// timer
// -----------------------------------------------------------------------------

struct timespec ilka_now();
double ilka_elapsed(struct timespec *start);
size_t ilka_print_elapsed(char *buf, size_t n, double t);


// -----------------------------------------------------------------------------
// sleep
// -----------------------------------------------------------------------------

bool ilka_nsleep(uint64_t nanos);


// -----------------------------------------------------------------------------
// prof
// -----------------------------------------------------------------------------

enum { ilka_prof_max_children = 128 };

struct ilka_prof
{
    const char *title;

    size_t hits;
    uint64_t elapsed;

    struct {
        struct ilka_prof *p;
        size_t hits;
        uint64_t elapsed;
    } children[ilka_prof_max_children];

    struct ilka_prof *next;
};

struct ilka_prof_data
{
    struct ilka_prof *parent;
    struct timespec start;
    size_t index;
};

struct ilka_prof_data ilka_prof_enter(struct ilka_prof *p, const char *title);
void ilka_prof_exit(struct ilka_prof *p, struct ilka_prof_data *data);
void ilka_prof_dump();


#define ILKA_PROF_ENTER(title)                          \
    static struct ilka_prof _ilka_prof_ ## title;       \
    struct ilka_prof_data _ilka_data_ ## title =        \
        ilka_prof_enter(&_ilka_prof_ ## title, #title)

#define ILKA_PROF_EXIT(title)                   \
    ilka_prof_exit(&_ilka_prof_ ## title, &_ilka_data_ ## title)
