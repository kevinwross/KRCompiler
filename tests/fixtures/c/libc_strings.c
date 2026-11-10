int printf(const char *format, ...);

unsigned int strlen(const char *text);
char *strcat(char *destination, const char *source);
char *strncpy(char *destination, const char *source, unsigned int count);
char *strchr(const char *text, int character);
char *strstr(const char *text, const char *needle);
int atoi(const char *text);
int abs(int value);

int main(void) {
    char buffer[32];
    strncpy(buffer, "abc", 8);
    strcat(buffer, "def");
    int score = 0;
    if (strlen(buffer) == 6) {
        score = score + 1;
    }
    if (strchr(buffer, 'd') != 0) {
        score = score + 1;
    }
    if (strstr(buffer, "cde") != 0) {
        score = score + 1;
    }
    if (atoi("-42") == -42) {
        score = score + 1;
    }
    if (abs(-7) == 7) {
        score = score + 1;
    }
    printf("%s %d\n", buffer, score);
    return score == 5 ? 0 : 1;
}
