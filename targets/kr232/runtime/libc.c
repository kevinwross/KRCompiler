/* The kernel-mode kr232 C runtime's output path. Standard output is the host text
   display (kr232-devices.md §7); device registers are kernel-only, so this writes
   the terminal page's cell port at 0xFFF08000 directly and the page maps the byte
   onto the shared display. The console page (0xFFF02000) is the debug serial stream
   and stdio does not use it. A user program cannot reach the page and uses the SYS
   write path in libc_user.c, which the kernel serves from the same display. */
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