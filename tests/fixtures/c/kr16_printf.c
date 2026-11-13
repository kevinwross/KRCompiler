// The KR16 runtime formats integer and string conversions; there is no
// floating point on this profile, so `%f`-style conversions do not exist.
// Returns 42 when every formatted string matches, and prints one line for the
// console check in the test runner.

#include <stdarg.h>

unsigned int strlen(const char *text);
int strcmp(const char *left, const char *right);
int snprintf(char *buffer, unsigned int size, const char *format, ...);
int sprintf(char *buffer, const char *format, ...);
int printf(const char *format, ...);

static int expect(const char *actual, const char *expected, int code) {
    if (strcmp(actual, expected) != 0) {
        return code;
    }
    return 0;
}

int main(void) {
    char buffer[64];
    int code = 0;

    snprintf(buffer, sizeof(buffer), "%d", -42);
    code = expect(buffer, "-42", 1);
    if (code != 0) {
        return code;
    }

    snprintf(buffer, sizeof(buffer), "%5d|%-5d|%05d", 42, 42, 42);
    code = expect(buffer, "   42|42   |00042", 2);
    if (code != 0) {
        return code;
    }

    snprintf(buffer, sizeof(buffer), "%+d % d", 7, 7);
    code = expect(buffer, "+7  7", 3);
    if (code != 0) {
        return code;
    }

    snprintf(buffer, sizeof(buffer), "%x %X %#x", 6699, 6699, 6699);
    code = expect(buffer, "1a2b 1A2B 0x1a2b", 4);
    if (code != 0) {
        return code;
    }

    snprintf(buffer, sizeof(buffer), "%o %#o", 8, 8);
    code = expect(buffer, "10 010", 5);
    if (code != 0) {
        return code;
    }

    snprintf(buffer, sizeof(buffer), "%u", (unsigned int)-1);
    code = expect(buffer, "65535", 6);
    if (code != 0) {
        return code;
    }

    snprintf(buffer, sizeof(buffer), "%.3d|%*d", 7, 4, 7);
    code = expect(buffer, "007|   7", 7);
    if (code != 0) {
        return code;
    }

    snprintf(buffer, sizeof(buffer), "%c%c", 'o', 'k');
    code = expect(buffer, "ok", 8);
    if (code != 0) {
        return code;
    }

    snprintf(buffer, sizeof(buffer), "%s|%.3s|%6s|%-6s|", "hello", "hello",
             "hi", "hi");
    code = expect(buffer, "hello|hel|    hi|hi    |", 9);
    if (code != 0) {
        return code;
    }

    snprintf(buffer, sizeof(buffer), "%%|%p", (void *)0x1234);
    code = expect(buffer, "%|0x1234", 10);
    if (code != 0) {
        return code;
    }

    // A truncated write keeps the terminator and reports the full length.
    const int written = snprintf(buffer, 4, "%s", "hello");
    if (written != 5) {
        return 11;
    }
    code = expect(buffer, "hel", 12);
    if (code != 0) {
        return code;
    }

    sprintf(buffer, "%d:%d", 1, 2);
    code = expect(buffer, "1:2", 13);
    if (code != 0) {
        return code;
    }

    printf("kr16 printf %d %s %d\n", 7, "ok", -3);

    return 42;
}
