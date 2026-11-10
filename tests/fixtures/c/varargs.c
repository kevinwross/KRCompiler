#include <stdarg.h>

int printf(const char *format, ...);

int sum(int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int total = 0;
    for (int index = 0; index < count; index++) {
        total = total + va_arg(arguments, int);
    }
    va_end(arguments);
    return total;
}

int main(void) {
    printf("%d\n", sum(3, 1, 2, 3));
    return 0;
}
