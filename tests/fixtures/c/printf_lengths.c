int printf(const char *format, ...);

int main(void) {
    int value = 5;
    printf("%p %ld %zu %u\n", (void *)&value, 123456L, sizeof(value),
           4000000000u);
    return 0;
}
