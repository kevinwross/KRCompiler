/* ADR 0059: a guest function declared with `...` must be able to read its own
 * variable arguments.
 *
 * Freestanding, so it reports through the exit status: 0 when both readers agree
 * and a distinct code when one does not, which names the failure without libc.
 */

#include <stdarg.h>

/* Reads `count` integers from the slots after the named parameter. */
static int sum_ints(int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int total = 0;
    int index;
    for (index = 0; index < count; ++index) {
        total += va_arg(arguments, int);
    }
    va_end(arguments);
    return total;
}

/* Reads an int and then a double: a mixed call needs no type tag because each
 * argument has its own eight-byte slot. */
static int mix(int tag, ...) {
    va_list arguments;
    va_start(arguments, tag);
    int first = va_arg(arguments, int);
    double second = va_arg(arguments, double);
    va_end(arguments);
    return tag + first + (int)(second * 10.0);
}

int main(void) {
    if (sum_ints(3, 10, 20, 12) != 42) {
        return 1;
    }
    if (mix(1, 5, 2.5) != 31) {
        return 2;
    }
    return 0;
}
