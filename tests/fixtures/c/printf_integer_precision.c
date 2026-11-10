int printf(const char *format, ...);

int main(void) {
    printf("[%.5d][%+.2d][% 4d]\n", 42, 7, 9);
    return 0;
}
