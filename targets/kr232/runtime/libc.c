/* The kernel-mode kr232 C runtime's output path. Device registers are kernel-only,
   so this writes the terminal page's cell port at 0xFFF08000 directly; the page
   advances the cursor, which is why `krc_kr232_run_transcript` reads `Hi!` back
   out of the terminal grid. A program running under the kernel uses the packages
   in targets/kr232/runtime/cons_user.c, which go through SYSCALL instead. */
#define TERMINAL_CELL_OUT (*(volatile unsigned int*)0xFFF08000u)

int putchar_(int ch) {
    TERMINAL_CELL_OUT = (unsigned int)ch;
    return ch;
}

int puts_(const char* text) {
    while (*text != 0) {
        putchar_(*text);
        ++text;
    }
    return 0;
}

/* Byte loops for now. The blitter-backed versions are the kernel's to provide:
   a user program reaches the blitter only through a SYSCALL. */
void* memcpy(void* destination, const void* source, unsigned count) {
    unsigned char* to = (unsigned char*)destination;
    const unsigned char* from = (const unsigned char*)source;
    for (unsigned i = 0; i < count; ++i) {
        to[i] = from[i];
    }
    return destination;
}

void* memset(void* destination, int value, unsigned count) {
    unsigned char* to = (unsigned char*)destination;
    for (unsigned i = 0; i < count; ++i) {
        to[i] = (unsigned char)value;
    }
    return destination;
}