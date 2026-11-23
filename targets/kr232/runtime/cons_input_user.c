/* The console's input half, in its own translation unit: one object holding both
   halves mis-links the second function's sibling call (recorded in
   kr232-impl.md, 2026-11-24T14:25), so the halves are built separately. */
extern int __kr232_syscall6(int number, ...);

#define KR232_CALL_READ 2

int cons_getc(void) {
    char byte = 0;
    const int got = __kr232_syscall6(KR232_CALL_READ, (int)&byte, 1, 0, 0, 0);
    if (got != 1) {
        return -1;
    }
    return (int)(unsigned char)byte;
}
