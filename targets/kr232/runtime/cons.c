#include <kr232/cons.h>

#define CONSOLE_OUT (*(volatile unsigned*)0xFFF02000u)
#define CONSOLE_IN (*(volatile unsigned*)0xFFF02004u)

void cons_putc(int ch) { CONSOLE_OUT = (unsigned)ch; }

void cons_puts(const char* text) {
    while (*text != 0) {
        cons_putc(*text);
        ++text;
    }
}

int cons_getc(void) {
    const unsigned value = CONSOLE_IN;
    return (int)(value & 0xFFu);
}
