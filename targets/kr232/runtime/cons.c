#include <kr232/cons.h>

/* The screen console package: CON1 is the debug serial stream, so the terminal page
   is the display standard output writes to. Device registers are kernel-only, which
   is why this mode touches the terminal page directly. Input is host-mediated through
   SYSCALL, so a kernel-mode program reads display input with the C runtime's getchar
   rather than through a device register; the console page's IN register is the debug
   serial input and is not the screen console's input. */
#define TERMINAL_CELL_OUT (*(volatile unsigned*)0xFFF08000u)

void cons_putc(int ch) { TERMINAL_CELL_OUT = (unsigned)ch; }

void cons_puts(const char* text) {
    while (*text != 0) {
        cons_putc(*text);
        ++text;
    }
}

int cons_getc(void) {
    /* No kernel-mode register carries display input. A kernel program that needs the
       screen console's input uses getchar() from the C runtime, whose SYS 2 read is
       served by the host line editor. -1 is end of input for a register that cannot
       answer. */
    return -1;
}
