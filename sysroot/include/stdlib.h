#ifndef KRC_STDLIB_H
#define KRC_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void exit(int status);
void abort(void);
void _Exit(int status);

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);

int atoi(const char *text);
long atol(const char *text);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);

int abs(int value);
long labs(long value);

/* This profile returns aggregates in registers, which IP16 cannot do, so the
 * struct-returning division helpers are absent there. */
#ifndef __kr16__
typedef struct div_t {
    int quot;
    int rem;
} div_t;

typedef struct ldiv_t {
    long quot;
    long rem;
} ldiv_t;

#if KRC_HAS_LONG_LONG
typedef struct lldiv_t {
    long long quot;
    long long rem;
} lldiv_t;
#endif

div_t div(int numerator, int denominator);
ldiv_t ldiv(long numerator, long denominator);
#if KRC_HAS_LONG_LONG
lldiv_t lldiv(long long numerator, long long denominator);
#endif
#endif

/* The IP16 profile has no 64-bit integer or floating-point type, so those
 * declarations are absent there and using the type is diagnosed. A profile with
 * floating point but no 64-bit integer -- kr232 -- keeps double and loses the
 * long-long entries. */
#ifndef __kr16__
double atof(const char *text);
double strtod(const char *text, char **end);
long double strtold(const char *text, char **end);
#endif
#if KRC_HAS_LONG_LONG
long long atoll(const char *text);
long long strtoll(const char *text, char **end, int base);
unsigned long long strtoull(const char *text, char **end, int base);
long long llabs(long long value);
#endif

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));

char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int atexit(void (*function)(void));
int at_quick_exit(void (*function)(void));
void quick_exit(int status);

void srand(unsigned int seed);
int rand(void);
int sys_random(void);

#ifdef __kr32__
int _heapwalk(void);
#endif

#endif
