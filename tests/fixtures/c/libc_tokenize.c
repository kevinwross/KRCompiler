int printf(const char *format, ...);
unsigned int strspn(const char *text, const char *accept);
unsigned int strcspn(const char *text, const char *reject);
char *strpbrk(const char *text, const char *accept);
char *strtok(char *text, const char *delimiters);
char *strcpy(char *destination, const char *source);

int main(void) {
    char text[32];
    strcpy(text, "alpha,beta;gamma");
    int count = 0;
    char *token = strtok(text, ",;");
    while (token) {
        count++;
        token = strtok(0, ",;");
    }
    unsigned int span = strspn("aaab", "a");
    unsigned int cross = strcspn("aaab", "b");
    char *found = strpbrk("alpha", "ph");
    printf("%d %u %u %c\n", count, span, cross, found ? found[0] : '?');
    return count == 3 && span == 3 && cross == 3 && found && found[0] == 'p'
               ? 0
               : 1;
}
