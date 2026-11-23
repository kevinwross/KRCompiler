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
