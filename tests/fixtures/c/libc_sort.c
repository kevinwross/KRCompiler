int printf(const char *format, ...);
void qsort(void *base, unsigned int count, unsigned int size,
           int (*compare)(const void *, const void *));
char *strrchr(const char *text, int character);
char *strncat(char *destination, const char *source, unsigned int count);

int compare_int(const void *left, const void *right) {
    int a = *(const int *)left;
    int b = *(const int *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

int main(void) {
    int values[5];
    values[0] = 4;
    values[1] = 1;
    values[2] = 3;
    values[3] = 0;
    values[4] = 2;
    qsort(values, 5, sizeof(int), compare_int);
    int sorted = 1;
    for (int index = 0; index < 5; index++) {
        if (values[index] != index) {
            sorted = 0;
        }
    }
    char text[16];
    text[0] = 0;
    strncat(text, "alpha", 16);
    strncat(text, "/beta", 16);
    int hit = strrchr(text, '/') != 0;
    printf("%s %d %d\n", text, sorted, hit);
    return sorted && hit ? 0 : 1;
}
