int printf(const char *format, ...);
int snprintf(char *destination, unsigned int size, const char *format, ...);

int main(void) {
    char text[5];
    int length = snprintf(text, sizeof(text), "%s", "abcdef");
    printf("[%s] %d\n", text, length);
    return length == 6 ? 0 : 1;
}
