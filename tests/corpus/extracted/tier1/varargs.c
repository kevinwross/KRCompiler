/* Tier 1 — varargs. A variadic function summing its arguments, plus one that counts
 * them, driven by <stdarg.h>.
 *
 * KR16 passes every argument of a variadic call on the stack and makes va_list a
 * char* that advances two bytes, so this fixture exercises the path ADR 0029
 * describes rather than a register convention the profile does not have. */

#include <stdarg.h>

int sum(int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int total = 0;
    int index = 0;
    while (index < count) {
        total = total + va_arg(arguments, int);
        index = index + 1;
    }
    va_end(arguments);
    return total;
}

/* Variadic arguments after several named ones, which is where a callee's slot
 * arithmetic is easiest to get wrong. */
int weighted(int first, int second, int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int total = first * 100 + second * 10;
    int index = 0;
    while (index < count) {
        total = total + va_arg(arguments, int);
        index = index + 1;
    }
    va_end(arguments);
    return total;
}

int main(void) {
    if (sum(0) != 0) { return 1; }
    if (sum(1, 5) != 5) { return 2; }
    if (sum(3, 1, 2, 3) != 6) { return 3; }
    if (sum(4, 10, 20, 30, 40) != 100) { return 4; }

    /* More variable arguments than fit in the first three argument registers, so
     * some of them are pushed. */
    if (sum(6, 1, 2, 3, 4, 5, 6) != 21) { return 5; }

    if (weighted(1, 2, 0) != 120) { return 6; }
    if (weighted(1, 2, 2, 3, 4) != 127) { return 7; }

    return 0;
}
