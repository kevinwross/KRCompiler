int printf(const char *format, ...);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
void srand(unsigned int seed);
int rand(void);

int main(void) {
    char *end = 0;
    long hex = strtol("0x1f", &end, 0);
    long negative = strtol("-42", &end, 10);
    unsigned long octal = strtoul("17", &end, 8);
    srand(7);
    int first = rand();
    int second = rand();
    printf("%ld %ld %lu %d %d\n", hex, negative, octal, first, second);
    int random_ok = first >= 0 && first <= 32767 && second >= 0 &&
                    second <= 32767 && first != second;
    return hex == 31 && negative == -42 && octal == 15 && random_ok ? 0 : 1;
}
