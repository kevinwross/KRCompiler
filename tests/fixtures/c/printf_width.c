int printf(const char *format, ...);

int main(void) {
    printf("%05d %-5d %8x %o\n", 42, 7, 255, 8);
    return 0;
}
