/* The user-mode side of <kr232/input.h>: the keyboard queue is the kernel's. */
extern int __kr232_syscall6(int number, ...);

#define KR232_CALL_INPUT_NEXT 23
#define KR232_CALL_INPUT_PENDING 24

unsigned input_next(void) {
    return (unsigned)__kr232_syscall6(KR232_CALL_INPUT_NEXT, 0, 0, 0, 0, 0);
}

unsigned input_pending(void) {
    return (unsigned)__kr232_syscall6(KR232_CALL_INPUT_PENDING, 0, 0, 0, 0, 0);
}
