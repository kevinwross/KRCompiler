// The standard integer-division helpers return structs, which exercises the
// register return for the small ones and the hidden-destination return for
// `lldiv`; `vsprintf` writes through the same format engine as `vsnprintf`,
// and the stream flag accessors report and clear a stream's state.
// Returns 42 when every check holds.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int format_into(char *buffer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int written = vsprintf(buffer, format, arguments);
    va_end(arguments);
    return written;
}

int main(void) {
    const div_t first = div(17, 5);
    if (first.quot != 3 || first.rem != 2) {
        return 1;
    }

    const div_t negative = div(-17, 5);
    if (negative.quot != -3 || negative.rem != -2) {
        return 2;
    }

    const ldiv_t wide = ldiv(100000, 7);
    if (wide.quot != 14285 || wide.rem != 5) {
        return 3;
    }

    // A sixteen-byte result travels through the memory-class return path.
    const lldiv_t doubled = lldiv(1000000007LL, 100000LL);
    if (doubled.quot != 10000LL || doubled.rem != 7LL) {
        return 4;
    }

    char buffer[64];
    const int written = format_into(buffer, "%d|%s|%x", 42, "ok", 255);
    if (written != 8 || strcmp(buffer, "42|ok|ff") != 0) {
        return 5;
    }

    const int signed_value = format_into(buffer, "%05d", -42);
    if (signed_value != 5 || strcmp(buffer, "-0042") != 0) {
        return 6;
    }

    // strtold parses to the same value strtod does on this target.
    char *end = 0;
    const long double parsed = strtold("2.5x", &end);
    if (parsed != 2.5L || end == 0 || *end != 'x') {
        return 7;
    }

    // A stream reports its end state and clearerr resets it. The file is
    // created here because the VM resolves relative paths against the test's
    // working directory.
    FILE *stream = fopen("krc_div_vsprintf.tmp", "w+");
    if (stream == 0) {
        return 8;
    }
    fputs("abc", stream);
    fseek(stream, 0, SEEK_SET);
    if (fgetc(stream) != 'a') {
        return 9;
    }
    while (fgetc(stream) != EOF) {
    }
    if (!feof(stream) || ferror(stream)) {
        return 10;
    }
    clearerr(stream);
    if (feof(stream)) {
        return 11;
    }
    fclose(stream);
    remove("krc_div_vsprintf.tmp");

    return 42;
}
