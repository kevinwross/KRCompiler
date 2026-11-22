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
