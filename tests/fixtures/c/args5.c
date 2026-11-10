int printf(const char *format, ...);

int sum5(int a, int b, int c, int d, int e) {
    return a + b + c + d + e;
}

int main(void) {
    int value = sum5(1, 2, 3, 4, 5);
    printf("%d\n", value);
    return value == 15 ? 0 : 1;
}
