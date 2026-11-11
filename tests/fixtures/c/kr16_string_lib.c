unsigned int strlen(const char *text);
int strcmp(const char *left, const char *right);
char *strcpy(char *destination, const char *source);
char *strstr(const char *text, const char *needle);
char *strchr(const char *text, int character);
int atoi(const char *text);
long strtol(const char *text, char **end, int base);
int abs(int value);

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
    return (int)strlen(buffer);
}
