int printf(const char *format, ...);
int sprintf(char *destination, const char *format, ...);

int main(void) {
    char text[64];
    int length = sprintf(text, "%d:%s:%03d", 7, "ok", 5);
    printf("[%s] %d\n", text, length);
    return length == 8 ? 0 : 1;
}
