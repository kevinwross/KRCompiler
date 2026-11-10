int putchar(int c);

int main(void) {
    char *message = "Hello, KR32!\n";
    while (*message) {
        putchar(*message);
        message = message + 1;
    }
    return 0;
}
