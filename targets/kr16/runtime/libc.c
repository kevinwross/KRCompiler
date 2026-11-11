unsigned int strlen(const char *text) {
    unsigned int length = 0;
    while (text[length] != 0) {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left != 0 && *left == *right) {
        left++;
        right++;
    }
    return (int)*left - (int)*right;
}

int strncmp(const char *left, const char *right, unsigned int count) {
    while (count > 0 && *left != 0 && *left == *right) {
        left++;
        right++;
        count--;
    }
    if (count == 0) {
        return 0;
    }
    return (int)*left - (int)*right;
}

char *strcpy(char *destination, const char *source) {
    char *result = destination;
    while ((*destination++ = *source++) != 0) {
    }
    return result;
}

char *strcat(char *destination, const char *source) {
    char *result = destination;
    while (*destination != 0) {
        destination++;
    }
    while ((*destination++ = *source++) != 0) {
    }
    return result;
}

void *memcpy(void *destination, const void *source, unsigned int count) {
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    while (count > 0) {
        *out++ = *in++;
        count--;
    }
    return destination;
}

void *memset(void *destination, int value, unsigned int count) {
    unsigned char *out = (unsigned char *)destination;
    while (count > 0) {
        *out++ = (unsigned char)value;
        count--;
    }
    return destination;
}

int memcmp(const void *left, const void *right, unsigned int count) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    while (count > 0 && *a == *b) {
        a++;
        b++;
        count--;
    }
    if (count == 0) {
        return 0;
    }
    return (int)*a - (int)*b;
}

int putchar(int character) {
    *(unsigned char *)0xF000 = (unsigned char)character;
    return character;
}

char *strncpy(char *destination, const char *source, unsigned int count) {
    char *result = destination;
    while (count > 0 && *source != 0) {
        *destination++ = *source++;
        count--;
    }
    while (count > 0) {
        *destination++ = 0;
        count--;
    }
    return result;
}

char *strncat(char *destination, const char *source, unsigned int count) {
    char *result = destination;
    while (*destination != 0) {
        destination++;
    }
    while (count > 0 && *source != 0) {
        *destination++ = *source++;
        count--;
    }
    *destination = 0;
    return result;
}

char *strchr(const char *text, int character) {
    while (*text != 0) {
        if (*text == (char)character) {
            return (char *)text;
        }
        text++;
    }
    return character == 0 ? (char *)text : 0;
}

char *strrchr(const char *text, int character) {
    const char *last = 0;
    while (*text != 0) {
        if (*text == (char)character) {
            last = text;
        }
        text++;
    }
    if (character == 0) {
        return (char *)text;
    }
    return (char *)last;
}

char *strstr(const char *text, const char *needle) {
    if (*needle == 0) {
        return (char *)text;
    }
    while (*text != 0) {
        const char *a = text;
        const char *b = needle;
        while (*b != 0 && *a == *b) {
            a++;
            b++;
        }
        if (*b == 0) {
            return (char *)text;
        }
        text++;
    }
    return 0;
}

static int digit_value(char character) {
    if (character >= '0' && character <= '9') {
        return (int)(character - '0');
    }
    if (character >= 'a' && character <= 'z') {
        return (int)(character - 'a') + 10;
    }
    if (character >= 'A' && character <= 'Z') {
        return (int)(character - 'A') + 10;
    }
    return 99;
}

long strtol(const char *text, char **end, int base) {
    const char *cursor = text;
    int negative = 0;
    unsigned long value = 0;
    if (base == 0) {
        base = 10;
        if (*cursor == '0') {
            base = 8;
            cursor++;
            if (*cursor == 'x' || *cursor == 'X') {
                base = 16;
                cursor++;
            }
        }
    }
    if (*cursor == '-') {
        negative = 1;
        cursor++;
    } else if (*cursor == '+') {
        cursor++;
    }
    while (*cursor != 0) {
        const int digit = digit_value(*cursor);
        if (digit >= base) {
            break;
        }
        value = value * (unsigned long)base + (unsigned long)digit;
        cursor++;
    }
    if (end != 0) {
        *end = (char *)cursor;
    }
    return negative ? (long)(0ul - value) : (long)value;
}

unsigned long strtoul(const char *text, char **end, int base) {
    return (unsigned long)strtol(text, end, base);
}

int atoi(const char *text) {
    return (int)strtol(text, 0, 10);
}

int abs(int value) {
    return value < 0 ? -value : value;
}

static unsigned char *heap_next;
#define HEAP_BEGIN ((unsigned char *)0x2000)
#define HEAP_END ((unsigned char *)0xE000)

void *malloc(unsigned int size) {
    size = (size + 1) & ~1u;
    if (heap_next == 0) {
        heap_next = HEAP_BEGIN;
    }
    if (size == 0 || heap_next + size > HEAP_END) {
        return 0;
    }
    void *result = heap_next;
    heap_next += size;
    return result;
}

void free(void *pointer) {
    (void)pointer;
}

void *calloc(unsigned int count, unsigned int size) {
    unsigned int total = count * size;
    unsigned char *memory = (unsigned char *)malloc(total);
    if (memory == 0) {
        return 0;
    }
    unsigned int index = 0;
    while (index < total) {
        memory[index++] = 0;
    }
    return memory;
}

void *realloc(void *pointer, unsigned int size) {
    if (pointer == 0) {
        return malloc(size);
    }
    void *result = malloc(size);
    if (result == 0) {
        return 0;
    }
    unsigned char *source = (unsigned char *)pointer;
    unsigned char *destination = (unsigned char *)result;
    unsigned int index = 0;
    while (index < size) {
        destination[index] = source[index];
        index++;
    }
    return result;
}

char *strdup(const char *source) {
    const unsigned int size = strlen(source) + 1;
    char *copy = (char *)malloc(size);
    if (copy != 0) {
        strcpy(copy, source);
    }
    return copy;
}

static unsigned int random_state = 1;

void srand(unsigned int seed) {
    random_state = seed;
}

int rand(void) {
    random_state = random_state * 1103515245u + 12345u;
    return (int)((random_state >> 16) & 0x7FFF);
}

static char *token_cursor;

static int is_delimiter(char character, const char *delimiters) {
    while (*delimiters != 0) {
        if (*delimiters == character) {
            return 1;
        }
        delimiters++;
    }
    return 0;
}

char *strtok(char *text, const char *delimiters) {
    if (text != 0) {
        token_cursor = text;
    }
    if (token_cursor == 0) {
        return 0;
    }
    while (*token_cursor != 0 && is_delimiter(*token_cursor, delimiters)) {
        token_cursor++;
    }
    if (*token_cursor == 0) {
        token_cursor = 0;
        return 0;
    }
    char *token = token_cursor;
    while (*token_cursor != 0 && !is_delimiter(*token_cursor, delimiters)) {
        token_cursor++;
    }
    if (*token_cursor != 0) {
        *token_cursor++ = 0;
    }
    return token;
}
