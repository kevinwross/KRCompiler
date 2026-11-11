int printf(const char *format, ...);

int main(void) {
    printf("[%8.2f][%-8.2f][%+7.1f][%08.2f]\n", 3.5, 3.5, 2.25, 1.5);
    return 0;
}
