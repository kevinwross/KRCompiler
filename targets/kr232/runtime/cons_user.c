/* The user-mode side of <kr232/cons.h>: the screen console. Output and input both go
   through the kernel, which owns the display; the SYSCALL write stores characters on
   the terminal page and the SYSCALL read serves host display input. The signatures
   match the header exactly, so a program links against either implementation without
   changing its source. */
extern int __kr232_syscall6(int number, ...);

#define KR232_CALL_WRITE 1
#define KR232_CALL_READ 2

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

int cons_getc(void) {
    for (;;) {
        char byte = 0;
        const int got = __kr232_syscall6(KR232_CALL_READ, (int)&byte, 1, 0, 0, 0);
        if (got == 1) {
            return (int)(unsigned char)byte;
        }
        if (got == 0) {
            return -1; /* end of input */
        }
        /* A negative answer is the source still waiting for typed input: poll. */
    }
}
