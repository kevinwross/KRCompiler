/* The user-mode half of the kr232 C runtime. A user program cannot reach a
   device register, so it asks the kernel through SYSCALL: number 1 writes a
   buffer and number 0 exits with a status (kr232-isa.md §18). The kernel-mode
   path in libc.c is unchanged and still writes the console register directly. */
extern int __kr232_syscall(int number, int argument0, int argument1, int argument2);

#define KR232_CALL_EXIT 0
#define KR232_CALL_WRITE 1

int putchar_(int ch) {
    char byte = (char)ch;
    (void)__kr232_syscall(KR232_CALL_WRITE, (int)&byte, 1, 0);
    return ch;
}

int puts_(const char* text) {
    while (*text != 0) {
        putchar_(*text);
        ++text;
    }
    return 0;
}

void kr232_exit(int status) {
    (void)__kr232_syscall(KR232_CALL_EXIT, status, 0, 0);
}