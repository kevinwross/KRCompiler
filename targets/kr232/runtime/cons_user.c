/* The user-mode side of <kr232/cons.h>: output and input go through the kernel,
   which owns the console page. */
extern int __kr232_syscall6(int number, ...);

#define KR232_CALL_WRITE 1
#define KR232_CALL_READ 2

int cons_putc(int ch) {
    char byte = (char)ch;
    (void)__kr232_syscall6(KR232_CALL_WRITE, (int)&byte, 1, 0, 0, 0);
    return ch;
}

int cons_puts(const char* text) {
    while (*text != 0) {
        cons_putc(*text);
        ++text;
    }
    return 0;
}

int cons_getc(void) {
    char byte = 0;
    const int got = __kr232_syscall6(KR232_CALL_READ, (int)&byte, 1, 0, 0, 0);
    if (got != 1) {
        return -1;
    }
    return (int)(unsigned char)byte;
}
