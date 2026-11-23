/* The user-mode side of <kr232/cons.h>: output goes through the kernel, which
   owns the console page. The signatures match the header exactly, so a program
   links against either implementation without changing its source. Input lives
   in cons_input_user.c. */
extern int __kr232_syscall6(int number, ...);

#define KR232_CALL_WRITE 1

void cons_putc(int ch) {
    char byte = (char)ch;
    (void)__kr232_syscall6(KR232_CALL_WRITE, (int)&byte, 1, 0, 0, 0);
}

void cons_puts(const char* text) {
    while (*text != 0) {
        cons_putc(*text);
        ++text;
    }
}
