int printf(const char *format, ...);

int main(void) {
    printf("[%.3s][%8s][%-8s]\n", "abcdef", "hi", "hi");
    return 0;
}
