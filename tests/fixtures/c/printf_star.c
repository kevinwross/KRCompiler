int printf(const char *format, ...);

int main(void) {
    printf("[%*d][%-*d][%.*s]\n", 5, 42, 5, 7, 3, "abcdef");
    return 0;
}
