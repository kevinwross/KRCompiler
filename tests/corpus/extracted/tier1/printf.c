/* Tier 1 — printf. The conversions the tier definition names, with the expected
 * text checked by the run test rather than only the exit status.
 *
 * Integer-only: KR16's IP16 profile rejects floating point, and the locale-aware
 * set is deliberately out of scope. */

#include <stdio.h>

int main(void) {
    printf("signed %d\n", 42);
    printf("negative %d\n", -17);
    printf("char %c\n", 'K');
    printf("string %s\n", "text");
    printf("hex %x\n", 255);
    printf("upper %X\n", 255);
    printf("octal %o\n", 8);
    printf("unsigned %u\n", 4000);
    printf("width [%5d] left [%-5d] zero [%05d]\n", 42, 42, 42);

    /* A literal percent, and a string with no conversions. */
    printf("percent 100%%\n");
    printf("plain\n");

    /* Several conversions in one call, which is where a variadic argument walk
     * across more than three arguments matters on KR16. */
    printf("mixed %d %c %s %d\n", 1, 'b', "c", 4);

    return 0;
}
