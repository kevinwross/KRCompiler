// A KR16 variadic call pushes every argument on the stack, one two-byte slot
// each, so a va_list is a plain ascending pointer and va_start names the first
// variable argument (docs/abi-kr16.md, ADR 0029). Returns 42 when every form
// reads back the values its call passed.

#include <stdarg.h>

static int total(int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int sum = 0;
    int index = 0;
    while (index < count) {
        sum += va_arg(arguments, int);
        index++;
    }
    va_end(arguments);
    return sum;
}

// The named parameters are in the same pushed area as the variable ones.
static int after_three(int first, int second, int third, int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int sum = first + second + third + count;
    int index = 0;
    while (index < count) {
        sum += va_arg(arguments, int);
        index++;
    }
    va_end(arguments);
    return sum;
}

static int mixed(int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int sum = 0;
    int index = 0;
    while (index < count) {
        sum += va_arg(arguments, int);
        index++;
    }
    return sum;
}

int main(void) {
    if (total(3, 10, 20, 12) != 42) {
        return 1;
    }
    // A call with no variable arguments still forms a valid list.
    if (total(0) != 0) {
        return 2;
    }
    if (total(5, 1, 2, 3, 4, 5) != 15) {
        return 3;
    }

    // Four named parameters, two of them beyond the register bank.
    // The fourth named parameter is the variable-argument count, so this call
    // passes two of them.
    if (after_three(1, 2, 3, 2, 10, 20) != 38) {
        return 4;
    }

    // A char argument is promoted into its slot.
    char small = 5;
    if (mixed(2, small, 7) != 12) {
        return 5;
    }

    // Consecutive calls keep the stack area balanced.
    const int first = total(2, 1, 2);
    const int second = total(3, 3, 4, 5);
    if (first != 3 || second != 12) {
        return 6;
    }

    // A variadic call made through a function pointer uses the same rule.
    int (*pointer)(int, ...) = total;
    if (pointer(4, 1, 2, 3, 36) != 42) {
        return 7;
    }

    return 42;
}
