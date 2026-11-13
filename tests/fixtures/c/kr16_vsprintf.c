// `vsprintf` takes the argument list the variadic entry points build, so a
// helper can format without a size bound. Returns 42 when the string and the
// reported length agree.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int format_into(char *buffer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int written = vsprintf(buffer, format, arguments);
    va_end(arguments);
    return written;
}

int main(void) {
    char buffer[32];
    const int written = format_into(buffer, "%d|%s|%x", 42, "ok", 255);
    if (written != 8) {
        return 1;
    }
    if (strcmp(buffer, "42|ok|ff") != 0) {
        return 2;
    }

    const int signed_value = format_into(buffer, "%d", -7);
    if (signed_value != 2 || strcmp(buffer, "-7") != 0) {
        return 3;
    }

    const int padded = format_into(buffer, "%04u", 42u);
    if (padded != 4 || strcmp(buffer, "0042") != 0) {
        return 4;
    }

    return 42;
}
