/* The kr232 C runtime's output path. Device registers are kernel-only and there is
   no kernel yet, so a program running in kernel mode writes the console transmit
   register directly. Once the kernel exists this becomes a SYS write stub. */
#define CONSOLE_TX (*(volatile unsigned int*)0xFFF08000u)

int putchar_(int ch) {
    CONSOLE_TX = (unsigned int)ch;
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