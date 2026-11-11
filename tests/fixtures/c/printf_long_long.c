int printf(const char *format, ...);

int main(void) {
    long long negative = -123LL;
    unsigned long long large = 456ULL;
    printf("%lld %llu %llx\n", negative, large, large);
    return 0;
}
