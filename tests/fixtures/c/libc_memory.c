int printf(const char *format, ...);
void *memmove(void *destination, const void *source, unsigned int count);
void *memchr(const void *memory, int character, unsigned int count);
unsigned int strnlen(const char *text, unsigned int limit);
int strcmp(const char *left, const char *right);

int main(void) {
    char text[] = "abcde";
    memmove(text + 1, text, 4);
    text[5] = 0;
    char *found = (char *)memchr("hello", 'l', 5);
    unsigned int bounded = strnlen("abcdef", 3);
    printf("%s %c %u\n", text, found ? *found : '?', bounded);
    return strcmp(text, "aabcd") == 0 && found && *found == 'l' &&
                   bounded == 3
               ? 0
               : 1;
}
