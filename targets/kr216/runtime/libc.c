#include <stdarg.h>

int __krc_write(int fd, const char *buffer, int count);

extern char __heap_start[];
extern char __heap_end[];

static char *heap_cursor;

unsigned int strlen(const char *text) {
    unsigned int length = 0;
    while (text[length] != 0) {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    int index = 0;
    while (left[index] != 0 && left[index] == right[index]) {
        index++;
    }
    return (unsigned char)left[index] - (unsigned char)right[index];
}

char *strcpy(char *destination, const char *source) {
    int index = 0;
    while (source[index] != 0) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = 0;
    return destination;
}

void *memcpy(void *destination, const void *source, unsigned int count) {
    char *to = (char *)destination;
    const char *from = (const char *)source;
    unsigned int index = 0;
    while (index < count) {
        to[index] = from[index];
        index++;
    }
    return destination;
}

void *memmove(void *destination, const void *source, unsigned int count) {
    char *to = (char *)destination;
    const char *from = (const char *)source;
    if (to < from) {
        unsigned int index = 0;
        while (index < count) {
            to[index] = from[index];
            index++;
        }
    } else {
        unsigned int index = count;
        while (index > 0) {
            index--;
            to[index] = from[index];
        }
    }
    return destination;
}

void *memset(void *destination, int value, unsigned int count) {
    char *to = (char *)destination;
    unsigned int index = 0;
    while (index < count) {
        to[index] = (char)value;
        index++;
    }
    return destination;
}

void *malloc(unsigned int size) {
    if (heap_cursor == 0) {
        heap_cursor = __heap_start;
    }
    unsigned int rounded = (size + 1) & 0xFFFEu;
    if (heap_cursor + rounded > __heap_end) {
        return 0;
    }
    void *result = heap_cursor;
    heap_cursor += rounded;
    return result;
}

void free(void *pointer) {
    (void)pointer;
}

int putchar(int c) {
    char byte = (char)c;
    __krc_write(1, &byte, 1);
    return c;
}

int puts(const char *text) {
    const unsigned int length = strlen(text);
    __krc_write(1, text, (int)length);
    putchar('\n');
    return (int)(length + 1);
}

static int print_unsigned(unsigned int value, unsigned int base) {
    char buffer[16];
    int length = 0;
    do {
        const unsigned int digit = value % base;
        buffer[length++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= base;
    } while (value != 0);
    for (int index = length - 1; index >= 0; index--) {
        putchar(buffer[index]);
    }
    return length;
}

int printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int written = 0;
    int index = 0;
    while (format[index] != 0) {
        if (format[index] != '%') {
            putchar(format[index++]);
            written++;
            continue;
        }
        index++;
        if (format[index] == '%') {
            putchar('%');
            index++;
            written++;
            continue;
        }
        if (format[index] == 'c') {
            putchar(va_arg(arguments, int));
            index++;
            written++;
            continue;
        }
        if (format[index] == 's') {
            const char *text = va_arg(arguments, const char *);
            const unsigned int length = strlen(text);
            __krc_write(1, text, (int)length);
            written += (int)length;
            index++;
            continue;
        }
        if (format[index] == 'd' || format[index] == 'i') {
            int value = va_arg(arguments, int);
            if (value < 0) {
                putchar('-');
                written++;
                value = -value;
            }
            written += print_unsigned((unsigned int)value, 10);
            index++;
            continue;
        }
        if (format[index] == 'u') {
            written += print_unsigned(va_arg(arguments, unsigned int), 10);
            index++;
            continue;
        }
        if (format[index] == 'x') {
            written += print_unsigned(va_arg(arguments, unsigned int), 16);
            index++;
            continue;
        }
        putchar('%');
        putchar(format[index]);
        written += 2;
        index++;
    }
    va_end(arguments);
    return written;
}
