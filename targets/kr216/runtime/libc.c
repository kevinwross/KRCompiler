int __krc_write(int fd, const char *buffer, int count);

int putchar(int c) {
    char byte = (char)c;
    __krc_write(1, &byte, 1);
    return c;
}

int puts(const char *text) {
    int length = 0;
    while (text[length] != 0) {
        length++;
    }
    __krc_write(1, text, length);
    putchar('\n');
    return length + 1;
}
