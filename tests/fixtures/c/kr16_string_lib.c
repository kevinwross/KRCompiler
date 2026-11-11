unsigned int strlen(const char *text);
int strcmp(const char *left, const char *right);
char *strcpy(char *destination, const char *source);
char *strstr(const char *text, const char *needle);
char *strchr(const char *text, int character);
int atoi(const char *text);
long strtol(const char *text, char **end, int base);
int abs(int value);
void *malloc(unsigned int size);
void free(void *pointer);
void *calloc(unsigned int count, unsigned int size);
char *strdup(const char *source);
void srand(unsigned int seed);
int rand(void);
char *strtok(char *text, const char *delimiters);

int main(void) {
    char buffer[8];
    char *end = 0;
    strcpy(buffer, "abc");
    if (strcmp(buffer, "abc") != 0) {
        return 1;
    }
    if (strcmp(strstr("hello", "ll"), "llo") != 0) {
        return 2;
    }
    if (strchr(buffer, 'b') == 0) {
        return 3;
    }
    if (atoi("-7") != -7) {
        return 4;
    }
    if (strtol("1f", &end, 16) != 31 || *end != 0) {
        return 5;
    }
    if (abs(-9) != 9) {
        return 6;
    }
    char *copy = strdup("hello");
    if (copy == 0 || strcmp(copy, "hello") != 0) {
        return 7;
    }
    free(copy);
    int *values = (int *)calloc(3, 2);
    if (values == 0 || values[0] != 0 || values[1] != 0) {
        return 8;
    }
    values[0] = 7;
    free(values);
    srand(1);
    if (rand() == 0) {
        return 9;
    }
    char text[6];
    strcpy(text, "a,b,c");
    if (strcmp(strtok(text, ","), "a") != 0) {
        return 10;
    }
    return (int)strlen(buffer);
}
